#include "render/emote_model.h"
#include "render/block_decode.h"
#include "log/logger.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <stdexcept>

namespace artc {
namespace {
double Number(const PsbValue& v,double fallback=0) {
    double n=fallback;
    if(v.type==PsbValue::Number || v.type==PsbValue::Boolean)n=v.number;
    else if(v.type==PsbValue::String) {
        char* end=nullptr;n=std::strtod(v.string.c_str(),&end);
        if(end==v.string.c_str() || end!=v.string.c_str()+v.string.size())throw std::runtime_error("invalid E-mote numeric string");
    } else if(v.type!=PsbValue::Null)throw std::runtime_error("invalid E-mote number type");
    if(!std::isfinite(n))throw std::runtime_error("non-finite E-mote value");return n;
}
}
bool EmoteModel::Load(PsbDocument document,std::string& error) {
    try {
        EmoteModel next;const auto& root=document.root;const auto& metadata=root.At("metadata");
        const auto& base=metadata.At("base");
        if(root.At("object").At(base.At("chara").string).At("motion").At(base.At("motion").string).type!=PsbValue::Object)
            throw std::runtime_error("E-mote base motion not found");
        for(const auto& v:metadata.At("variableList").array) {
            if(v.At("label").string.empty())throw std::runtime_error("E-mote variable has no label");
            next.variables_.insert(v.At("label").string);
        }
        for(const auto& e:metadata.At("eyeControl").array) {
            EmoteBlink blink;
            blink.variable=e.At("label").string;
            if(blink.variable.empty())throw std::runtime_error("E-mote eye control has no label");
            blink.begin=Number(e.At("beginFrame"));
            blink.end=Number(e.At("endFrame"),blink.begin);
            blink.frames=std::max(1.0,Number(e.At("blinkFrameCount"),1));
            blink.interval_min=std::max(0.0,Number(e.At("blinkIntervalMin")));
            blink.interval_max=std::max(blink.interval_min,Number(e.At("blinkIntervalMax")));
            blink.enabled=Number(e.At("enabled"),1)!=0;
            blink.blink_enabled=Number(e.At("blinkEnabled"),1)!=0;
            next.blinks_.push_back(std::move(blink));
        }
        next.mirror_=Number(metadata.At("mirror"))!=0;
        const auto& mirror_matches=metadata.At("mirrorControl").At("variableMatchList");
        if(mirror_matches.type!=PsbValue::Null && mirror_matches.type!=PsbValue::Array)
            throw std::runtime_error("invalid E-mote mirror variable match list");
        for(const auto& match:mirror_matches.array) {
            if(match.type!=PsbValue::String)
                throw std::runtime_error("invalid E-mote mirror variable match string");
            next.mirror_variable_matches_.push_back(match.string);
        }
        for(const auto& s:metadata.At("selectorControl").array) {
            EmoteSelector selector;
            selector.label=s.At("label").string;
            if(selector.label.empty())throw std::runtime_error("E-mote selector has no label");
            selector.enabled=Number(s.At("enabled"),1)!=0;
            for(const auto& o:s.At("optionList").array) {
                EmoteSelectorItem item;
                item.label=o.At("label").string;
                if(item.label.empty())throw std::runtime_error("E-mote selector option has no label");
                // Selector targets are variables; models normally declare them
                // in variableList, but some variants rely on the selector to
                // introduce them (the reference host inserts on demand).
                next.variables_.insert(item.label);
                item.on=Number(o.At("onValue"),1);
                item.off=Number(o.At("offValue"));
                selector.items.push_back(std::move(item));
            }
            if(selector.items.empty())throw std::runtime_error("E-mote selector has no options");
            // The selector label itself is frequently also a variableList
            // entry (the game writes the option index through it).
            next.variables_.insert(selector.label);
            next.selectors_.push_back(std::move(selector));
        }
        for(const auto& a:metadata.At("attrcomp").array) {
            for(const auto& r:a.At("data").At("remove").array) {
                if(Number(r.At("value"))>0)continue;  // only removal rules are applied
                EmoteRemoval removal;
                removal.chara=r.At("id").At("chara").string;
                removal.motion=r.At("id").At("motion").string;
                removal.layer=r.At("id").At("layer").string;
                if(removal.layer.empty())throw std::runtime_error("invalid E-mote attrcomp removal");
                next.removals_.push_back(std::move(removal));
            }
        }
        // clampControl has no established algorithm (type 0) and type 1 (pair
        // circle projection) is not implemented. Neither is applied or faked by a
        // scalar clamp: the control is ignored and reported, the model still loads.
        const auto& clamps=metadata.At("clampControl");
        if(clamps.type==PsbValue::Array)
            for(const auto& clamp:clamps.array) {
                const auto& enabled=clamp.At("enabled");
                if(!(enabled.type==PsbValue::Null || enabled.number!=0))continue;
                const auto& type=clamp.At("type");
                if(type.type!=PsbValue::Number)continue;
                const bool whole=std::isfinite(type.number) && type.number==std::floor(type.number) &&
                    std::abs(type.number)<1e9;
                next.features_.Add(whole?"clampControl.type"+std::to_string(static_cast<long long>(type.number))
                                        :std::string("clampControl.typeOther"));
            }
        // E10 configuration + registry plan. Only controllers actually registered
        // here occupy labels. The effective instant set (authored + successful
        // absent-key defaults) MUST exist before timeline tracks are parsed.
        if(!next.control_.Load(metadata,error))throw std::runtime_error(error);
        std::set<std::string> registered;
        for(const auto& blink:next.blinks_)if(blink.enabled)registered.insert(blink.variable);
        for(const auto& selector:next.selectors_)if(selector.enabled)registered.insert(selector.label);
        if(!BuildEmoteControlRegistryPlan(next.control_,registered,next.control_plan_,error))
            throw std::runtime_error(error);
        const std::set<std::string>& instant=next.control_plan_.instant_variables;
        for(const auto& t:metadata.At("timelineControl").array) {
            EmoteTimeline timeline;const auto label=t.At("label").string;
            if(label.empty())throw std::runtime_error("E-mote timeline has no label");
            timeline.last_time=Number(t.At("lastTime"));timeline.loop_begin=Number(t.At("loopBegin"));
            timeline.loop_end=Number(t.At("loopEnd"));timeline.difference=Number(t.At("diff"))!=0;
            if(!(timeline.loop_begin==-1 && timeline.loop_end==-1) &&
               (timeline.loop_begin<0 || timeline.loop_end<timeline.loop_begin))
                throw std::runtime_error("invalid E-mote timeline loop");
            if(timeline.loop_begin<0 && timeline.last_time<0)next.features_.Add("timeline.openEnded");
            for(const auto& track:t.At("variableList").array) {
                EmoteTrack result;result.variable=track.At("label").string;result.instant=instant.count(result.variable);
                // A variable may have multiple ordered tracks (e.g. eyebrow
                // ranges in the public Vanilla model); preserve all of them.
                if(result.variable.empty())
                    throw std::runtime_error("invalid E-mote timeline variable");
                for(const auto& f:track.At("frameList").array) {
                    EmoteFrame frame;frame.time=Number(f.At("time"));frame.terminal=Number(f.At("type"))==0;
                    if(frame.time<0 || (!result.frames.empty() && frame.time<result.frames.back().time))
                        throw std::runtime_error("unordered E-mote keyframes");
                    if(!frame.terminal) {
                        if(Number(f.At("type"))!=2 || f.At("content").At("value").type==PsbValue::Null)
                            throw std::runtime_error("unsupported E-mote variable keyframe");
                        frame.value=Number(f.At("content").At("value"));frame.easing=Number(f.At("content").At("easing"));
                    }
                    result.frames.push_back(frame);
                }
                timeline.tracks.push_back(std::move(result));
            }
            if(!next.timelines_.emplace(label,std::move(timeline)).second)
                throw std::runtime_error("duplicate E-mote timeline");
        }
        // Motion-tag sync is distinct from the legacy recursive derivation below.
        // Android MMotionPlayer::SkipToSync (0x6ab794), normal non-loop branch:
        // select the first qualifying authored tag, else lastTime. Shape/finite
        // checks and rejection of unordered tags are OUR local safety policy,
        // not a claim about official malformed-input handling. Do not sort tags
        // or infer that the official implementation minimizes an unordered list.
        for(const auto& obj:document.root.At("object").object)
            for(const auto& mtn:obj.second.At("motion").object) {
                const auto& m=mtn.second;
                if(m.type!=PsbValue::Object)continue;
                MotionTagSync result;
                result.last_time=Number(m.At("lastTime"));
                result.loop=Number(m.At("loopTime"),-1)>=0;
                const auto& tags=m.At("tag");
                if(tags.type!=PsbValue::Null && tags.type!=PsbValue::Array)
                    throw std::runtime_error("invalid E-mote motion tag array");
                const auto tag_number=[](const PsbValue& v) {
                    if(v.type!=PsbValue::Number || !std::isfinite(v.number))
                        throw std::runtime_error("invalid E-mote motion tag number");
                    return v.number;
                };
                double previous=0;bool first=true;
                for(const auto& tag:tags.array) {
                    if(tag.type!=PsbValue::Object)
                        throw std::runtime_error("invalid E-mote motion tag object");
                    const double time=tag_number(tag.At("time"));
                    const double type=tag_number(tag.At("type"));
                    if(type!=std::floor(type))
                        throw std::runtime_error("invalid E-mote motion tag type");
                    if(!first && time<previous)
                        throw std::runtime_error("unordered E-mote motion tags (local safety policy)");
                    previous=time;first=false;
                    const auto& content=tag.At("content");
                    if(content.type!=PsbValue::Null && content.type!=PsbValue::Object)
                        throw std::runtime_error("invalid E-mote motion tag content");
                    const auto& sync=content.At("sync");
                    // Accept PSB booleans and numeric 0/1, not coerced strings
                    // or arbitrary numeric truthiness (a local shape policy).
                    if(sync.type!=PsbValue::Null &&
                       ((sync.type!=PsbValue::Boolean && sync.type!=PsbValue::Number) ||
                        !std::isfinite(sync.number) || (sync.number!=0 && sync.number!=1)))
                        throw std::runtime_error("invalid E-mote motion tag sync flag");
                    if(type==1 && sync.type!=PsbValue::Null && sync.number==1)
                        result.frames.push_back(time);
                }
                next.motion_tag_sync_.emplace(std::make_pair(obj.first,mtn.first),std::move(result));
            }
        // Legacy derived extension (reference host/krkrsdl3), NOT native Android
        // sync/completion: max content time over non-parameterized nodes,
        // following child motion references. Preserve existing consumers.
        std::set<std::pair<std::string,std::string>> visiting;
        std::function<double(const std::string&,const std::string&)> sync=
            [&](const std::string& chara,const std::string& motion)->double {
            const auto& m=document.root.At("object").At(chara).At("motion").At(motion);
            if(m.type!=PsbValue::Object)return -1;
            if(m.At("parameterize").type!=PsbValue::Null)return -1;
            if(!visiting.insert({chara,motion}).second)return -1;
            double best=-1;
            std::function<void(const PsbValue&)> walk=[&](const PsbValue& nodes) {
                for(const auto& n:nodes.array) {
                    const bool count_times=n.At("parameterize").type==PsbValue::Null;
                    for(const auto& f:n.At("frameList").array) {
                        if(Number(f.At("type"))==0)continue;
                        if(f.At("content").type==PsbValue::Null)continue;
                        const auto& src=f.At("content").At("src").string;
                        if(src.rfind("motion/",0)==0) {
                            const size_t slash=src.find('/',7);
                            if(slash!=std::string::npos) {
                                const double child=sync(src.substr(7,slash-7),src.substr(slash+1));
                                if(child>best)best=child;
                            }
                        }
                        if(!count_times)continue;
                        const double t=Number(f.At("time"));
                        if(t>best)best=t;
                    }
                    walk(n.At("children"));
                }
            };
            walk(m.At("layer"));
            visiting.erase({chara,motion});
            return best;
        };
        for(const auto& obj:document.root.At("object").object)
            for(const auto& mtn:obj.second.At("motion").object) {
                const double s=sync(obj.first,mtn.first);
                if(s>=0)next.sync_frames_[{obj.first,mtn.first}]=s;
            }
        next.document_=std::move(document);*this=std::move(next);error.clear();
        LogEmoteUnsupportedFeatures("model",features_);
        return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
double EmoteModel::SyncFrame(const std::string& chara,const std::string& motion) const {
    const auto it=sync_frames_.find({chara,motion});
    return it==sync_frames_.end()?-1:it->second;
}
std::optional<double> EmoteModel::NextMotionSyncFrame(const std::string& chara,
                                                     const std::string& motion,double current) const {
    if(!std::isfinite(current))return std::nullopt;
    const auto it=motion_tag_sync_.find({chara,motion});
    if(it==motion_tag_sync_.end() || it->second.loop)return std::nullopt;
    for(double time:it->second.frames)
        if(time>=current)return time;
    return it->second.last_time;
}
EmoteAtlasCache::Entry* EmoteAtlasCache::Find(const void* owner,const std::string& source) {
    if(owner_!=owner) {entries_.clear();bytes_=0;owner_=owner;return nullptr;}
    for(auto& entry:entries_)
        if(entry.source==source) {entry.stamp=++clock_;return &entry;}
    return nullptr;
}
EmoteAtlasCache::Entry* EmoteAtlasCache::Insert(const void* owner,std::string source,
                                                std::vector<uint8_t>&& rgba,bool exact) {
    if(owner_!=owner) {entries_.clear();bytes_=0;owner_=owner;}
    if(rgba.size()>limit_)return nullptr;
    while(!entries_.empty() && rgba.size()>limit_-bytes_) {
        auto oldest=entries_.begin();
        for(auto it=entries_.begin();it!=entries_.end();++it)if(it->stamp<oldest->stamp)oldest=it;
        bytes_-=oldest->rgba.size();entries_.erase(oldest);
    }
    bytes_+=rgba.size();
    entries_.push_back({std::move(source),std::move(rgba),exact,++clock_});
    return &entries_.back();
}
bool EmoteModel::Image(const std::string& source,const std::string& icon,EmoteImage& out,std::string& error,
                       EmoteAtlasCache* cache) const {
    try {
        const auto& root=document_.root;const auto& src=root.At("source").At(source);
        const auto& image=src.At("icon").At(icon);
        const auto spec=root.At("spec").string;
        if(spec!="krkr" && spec!="win" && spec!="common")throw std::runtime_error("unsupported E-mote texture platform");
        // win/common store one shared texture per source and crop every icon
        // out of it (left/top/width/height); krkr keeps a pixel resource per
        // icon. Reference host: art3m1s-core atlas.rs / krkrsdl3 readIconTobuffer.
        const auto& shared=src.At("texture");
        const bool atlas=shared.type==PsbValue::Object;
        const auto& size_src=atlas?shared:image;
        const double tex_w=Number(size_src.At("width")),tex_h=Number(size_src.At("height"));
        if(tex_w<1 || tex_h<1 || tex_w>8192 || tex_h>8192 ||
           tex_w!=std::floor(tex_w) || tex_h!=std::floor(tex_h) || tex_w*tex_h>16777216)
            throw std::runtime_error("invalid E-mote texture size");
        double left=0,top=0,crop_w=tex_w,crop_h=tex_h;
        if(atlas) {
            left=Number(image.At("left"));top=Number(image.At("top"));
            crop_w=Number(image.At("width"));crop_h=Number(image.At("height"));
            if(left<0 || top<0 || crop_w<1 || crop_h<1 ||
               left!=std::floor(left) || top!=std::floor(top) ||
               crop_w!=std::floor(crop_w) || crop_h!=std::floor(crop_h) ||
               left+crop_w>tex_w || top+crop_h>tex_h)
                throw std::runtime_error("invalid E-mote atlas crop");
        }
        EmoteImage result;result.width=int(crop_w);result.height=int(crop_h);
        result.origin_x=Number(image.At("originX"));result.origin_y=Number(image.At("originY"));
        // A mip chain appends levels after level 0; only level 0 is read. The
        // flag comes from the icon, so it can differ between icons of one atlas.
        const auto has_mip=[](const PsbValue& v) {
            if(v.type==PsbValue::Number)return v.number!=0;
            return v.type!=PsbValue::Null;
        };
        const bool mips=has_mip(image.At("mipMap")) || has_mip(image.At("mipMapLevel"));
        // Decode the whole texture (the shared atlas, or this icon's own pixels)
        // to RGBA8. `exact` reports that the resource held exactly level 0.
        const auto decode_full=[&](std::vector<uint8_t>& full,bool& exact) {
        std::vector<uint8_t> bytes,palette,decoded;
        exact=true;
        if(!document_.ReadResource(size_src.At("pixel"),bytes))throw std::runtime_error("missing E-mote pixel resource");
        const auto format=size_src.At("type").string;
        const bool indexed=!atlas && format=="CI8";
        const bool dxt5=format=="DXT5";
        const bool dxt1=format=="DXT1";
        const bool dxt3=format=="DXT3";
        const bool bc7=format=="BC7";
        const bool rgba8=format.empty() || format=="RGBA8";
        const bool rgbx8=format=="RGBX8";
        const bool a8l8=format=="A8L8";
        const bool rgba4444=format=="RGBA4444";
        const bool rgba5551=format=="RGBA5551";
        const bool rgba5650=format=="RGBA5650";
        if(!rgba8 && !rgbx8 && !indexed && !dxt5 && !dxt1 && !dxt3 && !bc7 &&
           !a8l8 && !rgba4444 && !rgba5551 && !rgba5650)
            throw std::runtime_error("unsupported E-mote pixel format");
        // Some RGBA models retain an unused pal reference. Format, not mere
        // presence of that field, determines whether pixels are indices.
        if(indexed && !image.At("palType").string.empty() && image.At("palType").string!="RGBA8")
            throw std::runtime_error("unsupported E-mote palette format");
        if(indexed && (!document_.ReadResource(image.At("pal"),palette) || palette.empty() || palette.size()%4 || palette.size()>1024))
            throw std::runtime_error("invalid E-mote palette");
        const size_t tex_count=size_t(tex_w)*size_t(tex_h);
        unsigned stride;
        if(indexed)stride=1;
        else if(a8l8 || rgba4444 || rgba5551 || rgba5650)stride=2;
        else stride=4;
        size_t raw_bytes;
        if(dxt1 || dxt3 || dxt5 || bc7) {
            const size_t blocks=size_t((tex_w+3)/4)*size_t((tex_h+3)/4);
            raw_bytes=blocks*((dxt1)?8:16); // 8/16 bytes per 4x4 block
        } else {
            raw_bytes=tex_count*stride;
        }
        const auto compress=atlas?std::string():image.At("compress").string;
        if(compress=="RL") {
            if(!DecodePsbRl(bytes,raw_bytes/stride,stride,decoded))throw std::runtime_error("invalid E-mote RL texture");
        } else if(compress.empty() || compress=="none") {
            if(bytes.size()<raw_bytes || (!mips && bytes.size()!=raw_bytes))
                throw std::runtime_error("invalid E-mote raw texture length");
            exact=bytes.size()==raw_bytes;
            bytes.resize(raw_bytes);
            decoded=std::move(bytes);
        } else throw std::runtime_error("unsupported E-mote texture compression");
        if(indexed) {
            full.resize(tex_count*4);
            for(size_t i=0;i<tex_count;++i) {
                const size_t at=size_t(decoded[i])*4;if(at+4>palette.size())throw std::runtime_error("E-mote palette index outside table");
                std::copy_n(palette.data()+at,4,full.data()+i*4);
            }
        } else if(dxt5) DecodeDxt5Blocks(decoded,int(tex_w),int(tex_h),full);
        else if(dxt1) DecodeDxt1Blocks(decoded,int(tex_w),int(tex_h),full);
        else if(dxt3) DecodeDxt3Blocks(decoded,int(tex_w),int(tex_h),full);
        else if(bc7) DecodeBc7Blocks(decoded,int(tex_w),int(tex_h),full);
        else if(a8l8) {
            full.resize(tex_count*4);
            for(size_t i=0;i<tex_count;++i) {
                const uint8_t a=decoded[i*2],l=decoded[i*2+1];
                full[i*4+0]=l;full[i*4+1]=l;full[i*4+2]=l;full[i*4+3]=a;
            }
        } else if(rgba4444 || rgba5551 || rgba5650) {
            full.resize(tex_count*4);
            for(size_t i=0;i<tex_count;++i) {
                const uint16_t v=uint16_t(decoded[i*2]|(uint16_t(decoded[i*2+1])<<8));
                uint8_t* o=full.data()+i*4;
                if(rgba4444) {
                    o[0]=uint8_t(((v>>12)&0xF)*17);o[1]=uint8_t(((v>>8)&0xF)*17);
                    o[2]=uint8_t(((v>>4)&0xF)*17);o[3]=uint8_t((v&0xF)*17);
                } else if(rgba5551) {
                    o[0]=uint8_t(((v>>11)&0x1F)*255/31);o[1]=uint8_t(((v>>6)&0x1F)*255/31);
                    o[2]=uint8_t(((v>>1)&0x1F)*255/31);o[3]=(v&1)?255:0;
                } else { // RGBA5650
                    o[0]=uint8_t(((v>>11)&0x1F)*255/31);o[1]=uint8_t(((v>>5)&0x3F)*255/63);
                    o[2]=uint8_t((v&0x1F)*255/31);o[3]=255;
                }
            }
        } else if(rgbx8) {
            full=std::move(decoded);
            for(size_t i=0;i<full.size();i+=4)full[i+3]=255;
        } else full=std::move(decoded);
        // Desktop PSB color words are A8R8G8B8, i.e. BGRA on little endian
        // (32-bit words and palette entries alike). Compressed atlases already
        // decode to natural RGBA and are not swapped; the 16-bit formats decode
        // naturally as well.
        if((spec=="krkr" || spec=="win") && (rgba8 || rgbx8 || indexed))
            for(size_t i=0;i<full.size();i+=4)std::swap(full[i],full[i+2]);
        };
        std::vector<uint8_t> local;const std::vector<uint8_t>* pixels=&local;
        if(atlas && cache) {
            auto* hit=cache->Find(this,source);
            if(hit) {
                // Same checks the uncached path would make for this icon.
                if(!mips && !hit->exact)throw std::runtime_error("invalid E-mote raw texture length");
                ++cache->hits_;pixels=&hit->rgba;
            } else {
                bool exact=true;decode_full(local,exact);++cache->decodes_;
                if(auto* kept=cache->Insert(this,source,std::move(local),exact))pixels=&kept->rgba;
                // else: too large to retain; `local` still owns the pixels (the
                // failed Insert left it untouched), used once for this icon.
            }
        } else {bool exact=true;decode_full(local,exact);}
        const std::vector<uint8_t>& full=*pixels;
        if(atlas && (left>0 || top>0 || crop_w!=tex_w || crop_h!=tex_h)) {
            result.rgba.resize(size_t(crop_w)*size_t(crop_h)*4);
            for(int y=0;y<int(crop_h);++y) {
                const size_t src_row=(size_t(top)+size_t(y))*size_t(tex_w)+size_t(left);
                std::copy_n(full.data()+src_row*4,size_t(crop_w)*4,
                            result.rgba.data()+size_t(y)*size_t(crop_w)*4);
            }
        } else if(pixels==&local)result.rgba=std::move(local);
        else result.rgba=full;
        out=std::move(result);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
bool EmoteModel::Sample(const std::string& label,double frame,std::map<std::string,double>& out) const {
    const auto it=timelines_.find(label);if(it==timelines_.end() || !std::isfinite(frame))return false;
    const auto& timeline=it->second;frame=std::max(frame,0.0);
    if(timeline.loop_end>timeline.loop_begin && timeline.loop_begin>=0 && frame>=timeline.loop_end)
        frame=timeline.loop_begin+std::fmod(frame-timeline.loop_begin,timeline.loop_end-timeline.loop_begin);
    else if(timeline.last_time>=0)frame=std::min(frame,timeline.last_time);
    std::map<std::string,double> result;
    for(const auto& track:timeline.tracks) {
        const EmoteFrame* left=nullptr;const EmoteFrame* right=nullptr;
        for(const auto& f:track.frames) {
            if(f.time<=frame){if(!f.terminal)left=&f;else break;}
            else {right=&f;break;}
        }
        if(!left)continue;
        double value=left->value;
        if(!track.instant && right && !right->terminal && right->time>left->time) {
            const double ratio=(frame-left->time)/(right->time-left->time);
            const double ease=right->easing,exponent=ease>=0?ease+1:1/(1-ease);
            value+=(right->value-value)*std::pow(ratio,exponent);
        }
        result[track.variable]=value;
    }
    out=std::move(result);return true;
}
}
