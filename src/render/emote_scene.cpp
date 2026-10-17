#include "render/emote_scene.h"
#include "render/compositor.h"
#include "render/emote_mesh.h"
#include "log/logger.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace artc {
namespace {
double Num(const PsbValue& v,double fallback=0) {
    double value=fallback;
    if(v.type==PsbValue::Number || v.type==PsbValue::Boolean)value=v.number;
    else if(v.type==PsbValue::String) {
        char* end=nullptr;value=std::strtod(v.string.c_str(),&end);
        if(end==v.string.c_str() || end!=v.string.c_str()+v.string.size())throw std::runtime_error("invalid scene number");
    } else if(v.type!=PsbValue::Null)throw std::runtime_error("invalid scene number type");
    if(!std::isfinite(value) || std::abs(value)>1e9)throw std::runtime_error("scene number outside supported range");
    return value;
}
std::pair<std::string,std::string> Path(const std::string& src,const char* prefix) {
    const size_t begin=std::char_traits<char>::length(prefix),slash=src.find('/',begin);
    if(src.rfind(prefix,0)!=0 || slash==std::string::npos || slash==begin || slash+1==src.size())
        throw std::runtime_error("invalid E-mote source: "+src);
    return {src.substr(begin,slash-begin),src.substr(slash+1)};
}
// `shape/<kind>` hit-test figures (SDK unit square is 16x16).
EmoteSceneLayer::Shape ParseShape(const std::string& src) {
    if(src.rfind("shape/",0)!=0)return EmoteSceneLayer::Shape::None;
    const std::string kind=src.substr(6);
    if(kind=="rect")return EmoteSceneLayer::Shape::Rect;
    if(kind=="circle")return EmoteSceneLayer::Shape::Circle;
    if(kind=="point")return EmoteSceneLayer::Shape::Point;
    if(kind=="quad")return EmoteSceneLayer::Shape::Quad;
    throw std::runtime_error("unsupported E-mote shape: "+src);
}
std::string Child(const std::string& key,size_t index) {
    char suffix[24];std::snprintf(suffix,sizeof(suffix),".%06zu",index+1);return key+suffix;
}
// Diagnostic selectors are complete icon or source/icon tokens, never substrings.
bool HideIcon(const char* selectors,const std::string& source,const std::string& icon) {
    if(!selectors)return false;
    const std::string_view list(selectors);
    for(size_t begin=0;begin<=list.size();) {
        const size_t end=list.find_first_of(",;",begin);
        auto token=list.substr(begin,end==std::string_view::npos?end:end-begin);
        const size_t first=token.find_first_not_of(" \t\r\n");
        if(first!=std::string_view::npos) {
            token=token.substr(first,token.find_last_not_of(" \t\r\n")-first+1);
            if(token==icon || (token.size()==source.size()+1+icon.size() &&
               token.substr(0,source.size())==source && token[source.size()]=='/' &&
               token.substr(source.size()+1)==icon))return true;
        }
        if(end==std::string_view::npos)break;
        begin=end+1;
    }
    return false;
}
// The compositor submits an unindexed triangle list in layer-local pixels.
// Signed edge tests are winding-independent (mirrors are valid) and include edges
// and vertices. Degenerate triangles have no rendered area. Work relative to
// the first vertex: tolerance must not depend on the mesh's absolute offset or
// have a unit-sized floor. Bound float subdivision/transform roundoff by a
// small barycentric fraction of area, so thin triangles cannot expand by a
// tolerance proportional to their longest edge.
bool InTriangle(double x,double y,const float* vertices) {
    const double ax=vertices[0],ay=vertices[1];
    const double bx=vertices[4]-ax,by=vertices[5]-ay;
    const double cx=vertices[8]-ax,cy=vertices[9]-ay;
    const double area=bx*cy-by*cx;
    if(!std::isfinite(area) || area==0)return false;
    const double px=x-ax,py=y-ay;
    const double epsilon=1e-6*std::abs(area);
    const double winding=area>0?1:-1;
    auto edge=[&](double ex,double ey,double dx,double dy) {
        return winding*(dx*(py-ey)-dy*(px-ex))>=-epsilon;
    };
    return edge(0,0,bx,by) && edge(bx,by,cx-bx,cy-by) && edge(cx,cy,-cx,-cy);
}
// `blank` frames describe their extent as "w:h:ox:oy" (or "blank:w:h:ox:oy").
bool ParseBlankDescriptor(const std::string& s,double* ox,double* oy) {
    int w=0,h=0;double x=0,y=0;
    if(s.rfind("blank:",0)==0) {
        if(std::sscanf(s.c_str(),"blank:%d:%d:%lf:%lf",&w,&h,&x,&y)!=4)return false;
    } else {
        if(std::sscanf(s.c_str(),"%d:%d:%lf:%lf",&w,&h,&x,&y)!=4)return false;
    }
    if(ox)*ox=x;if(oy)*oy=y;return true;
}
// Full blank descriptor including the extent size (E-mote mesh domains).
bool ParseBlankExtent(const std::string& s,double* w,double* h,double* ox,double* oy) {
    int wi=0,hi=0;double x=0,y=0;
    if(s.rfind("blank:",0)==0) {
        if(std::sscanf(s.c_str(),"blank:%d:%d:%lf:%lf",&wi,&hi,&x,&y)!=4)return false;
    } else {
        if(std::sscanf(s.c_str(),"%d:%d:%lf:%lf",&wi,&hi,&x,&y)!=4)return false;
    }
    if(w)*w=wi;if(h)*h=hi;if(ox)*ox=x;if(oy)*oy=y;return true;
}
// 2D affine mirroring the compositor's transform composition
// (T(x,y) T(anchor) R(angle) S(scale) about the anchor; E-mote nodes anchor at
// their own origin, so the plain T R S form is used here).
struct Affine2 {
    double m11=1,m12=0,m21=0,m22=1,tx=0,ty=0;
    static Affine2 TRS(double x,double y,double angle,double sx,double sy) {
        const double r=angle*3.14159265358979323846/180.0,c=std::cos(r),s=std::sin(r);
        Affine2 a;
        a.m11=c*sx;a.m12=-s*sy;a.m21=s*sx;a.m22=c*sy;a.tx=x;a.ty=y;
        return a;
    }
    std::pair<double,double> Apply(double x,double y) const {
        return {m11*x+m12*y+tx,m21*x+m22*y+ty};
    }
    std::pair<double,double> InverseApply(double x,double y) const {
        const double dx=x-tx,dy=y-ty,det=m11*m22-m12*m21;
        if(!std::isfinite(det)||std::abs(det)<=1e-12)return {dx,dy};
        const double inv=1.0/det;
        return {(m22*dx-m12*dy)*inv,(-m21*dx+m11*dy)*inv};
    }
    Affine2 Then(const Affine2& rhs) const {
        Affine2 o;
        o.m11=m11*rhs.m11+m12*rhs.m21;o.m12=m11*rhs.m12+m12*rhs.m22;
        o.m21=m21*rhs.m11+m22*rhs.m21;o.m22=m21*rhs.m12+m22*rhs.m22;
        o.tx=m11*rhs.tx+m12*rhs.ty+tx;o.ty=m21*rhs.tx+m22*rhs.ty+ty;
        return o;
    }
    Affine2 Inverse() const {
        Affine2 o;
        const double det=m11*m22-m12*m21;
        if(!std::isfinite(det)||std::abs(det)<=1e-12)return o;
        const double inv=1.0/det;
        o.m11=m22*inv;o.m12=-m12*inv;o.m21=-m21*inv;o.m22=m11*inv;
        o.tx=-(o.m11*tx+o.m12*ty);o.ty=-(o.m21*tx+o.m22*ty);
        return o;
    }
};
double LerpD(double a,double b,double t){return a+(b-a)*t;}
// Bezier (4x4) / bilinear (NxN) sampling of an E-mote normalized control grid.
std::pair<double,double> SampleGrid(const std::vector<double>& points,int side,double nx,double ny) {
    if(side==4) {
        const auto basis=[](double v){
            v=std::clamp(v,0.0,1.0);const double u=1.0-v;
            return std::array<double,4>{u*u*u,3*u*u*v,3*u*v*v,v*v*v};
        };
        const auto bx=basis(nx),by=basis(ny);
        double rx=0,ry=0;
        for(int y=0;y<4;++y)for(int x=0;x<4;++x){
            const double wgt=bx[size_t(x)]*by[size_t(y)];
            const size_t index=(size_t(y)*4+x)*2;
            rx+=points[index]*wgt;ry+=points[index+1]*wgt;
        }
        return {rx,ry};
    }
    auto axis=[](double v,int n){const double scaled=std::clamp(v,0.0,1.0)*(n-1);const int cell=std::min(int(scaled),n-2);return std::pair<int,double>{cell,scaled-cell};};
    const auto ax=axis(nx,side),ay=axis(ny,side);
    const auto pt=[&](int x,int y){const size_t i=(size_t(y)*side+x)*2;return std::pair<double,double>{points[i],points[i+1]};};
    const auto top=pt(ax.first,ay.first),bottom=pt(ax.first,ay.first+1);
    const auto top2=pt(ax.first+1,ay.first),bottom2=pt(ax.first+1,ay.first+1);
    return {LerpD(LerpD(top.first,top2.first,ax.second),LerpD(bottom.first,bottom2.first,ax.second),ay.second),
            LerpD(LerpD(top.second,top2.second,ax.second),LerpD(bottom.second,bottom2.second,ax.second),ay.second)};
}
// E-mote per-layer linear state (flip/rotate/scale/shear) with the official
// partial inheritMask semantics: a child only takes the parent channels its
// mask selects (reference host: inherit_linear_state / remove_motion_root).
struct LinearState {
    bool fx=false,fy=false;
    double rot=0,sx=1,sy=1,shx=0,shy=0;
};
LinearState InheritLinear(const LinearState& own,const LinearState& parent,int64_t mask) {
    LinearState out=own;
    out.fx=own.fx ^ ((mask&0x4)!=0 && parent.fx);
    out.fy=own.fy ^ ((mask&0x8)!=0 && parent.fy);
    if(mask&0x10)out.rot+=parent.rot;
    if(mask&0x20)out.sx*=parent.sx;
    if(mask&0x40)out.sy*=parent.sy;
    if(mask&0x80)out.shx+=parent.shx;
    if(mask&0x100)out.shy+=parent.shy;
    return out;
}
LinearState RemoveRootLinear(LinearState st,const LinearState& root,int64_t mask) {
    if(mask&0x4)st.fx=st.fx!=root.fx;
    if(mask&0x8)st.fy=st.fy!=root.fy;
    if(mask&0x10)st.rot-=root.rot;
    if(mask&0x20)if(std::abs(root.sx)>1e-12)st.sx/=root.sx;
    if(mask&0x40)if(std::abs(root.sy)>1e-12)st.sy/=root.sy;
    if(mask&0x80)st.shx-=root.shx;
    if(mask&0x100)st.shy-=root.shy;
    return st;
}
Affine2 BuildLinear(const std::vector<double>& order,const LinearState& st) {
    // Native files overwhelmingly omit transformOrder: flip, shear, scale,
    // rotate is the default stage order.
    std::array<int,4> stages{0,3,2,1};
    if(order.size()==4) {
        bool seen[4]={false,false,false,false};bool ok=true;
        for(size_t i=0;i<4;++i) {
            const int value=int(std::lround(order[i]));
            if(value<0||value>3||seen[value]){ok=false;break;}
            seen[value]=true;stages[i]=value;
        }
        if(!ok)stages={0,3,2,1};
    }
    Affine2 linear;
    for(const int stage:stages) {
        Affine2 op;
        switch(stage) {
            case 0: op.m11=st.fx?-1:1;op.m22=st.fy?-1:1;break;
            case 1: {const double r=st.rot*3.14159265358979323846/180.0;
                     const double c=std::cos(r),s=std::sin(r);
                     op.m11=c;op.m12=-s;op.m21=s;op.m22=c;break;}
            case 2: op.m11=st.sx;op.m22=st.sy;break;
            case 3: op.m12=st.shx;op.m21=st.shy;break;
        }
        linear=linear.Then(op);
    }
    return linear;
}
// Absolute per-node transform context. Children inherit the parent layer's
// affine unless the parent carries the transparent marker (0x400000); the
// linear itself composes per inheritMask, and partial masks rebuild relative
// to the enclosing motion root.
struct NodeSource {
    Affine2 linear;          // source linear (no translation)
    double lx=0,ly=0,lz=0;   // source location; Z is additive in static coordinates
    double opacity=1;
    LinearState state;       // source state (inheritance input)
    int64_t coordinate=0;
};
struct TransformCtx {
    NodeSource source;       // inheritance source for the current layer
    Affine2 root_linear;     // enclosing motion root (for partial masks)
    double root_lx=0,root_ly=0,root_opacity=1;
    LinearState root_state;
    bool independent=false;
};
// Frame colors are 0xRRGGBBAA in MODULATE2X space (0x80 = neutral); fold both
// the player tint and the frame color into the straight compositor factor.
uint32_t FrameTint(uint32_t rrggbbaa) {
    const auto dbl=[](uint32_t c){return std::min(0xFFu,c*2u);};
    return (dbl((rrggbbaa>>24)&0xFFu)<<16)|(dbl((rrggbbaa>>16)&0xFFu)<<8)|dbl((rrggbbaa>>8)&0xFFu);
}
uint32_t MultiplyColor(uint32_t a,uint32_t b) {
    const auto mix=[](uint32_t x,uint32_t y){return (x*y+127u)/255u;};
    return (mix((a>>16)&0xFFu,(b>>16)&0xFFu)<<16)|
           (mix((a>>8)&0xFFu,(b>>8)&0xFFu)<<8)|
           mix(a&0xFFu,b&0xFFu);
}
struct Frame {
    const PsbValue* left=nullptr;
    const PsbValue* right=nullptr;
    double ratio=0,time=0;
    bool visible=false;
};
Frame Sample(const PsbValue& list,double time) {
    Frame result;
    // Nested-motion clocks subtract keyframe times/offsets and accumulate
    // float error; a frame at t must not be missed by a hair (device bug:
    // a HOLD at t=61 was never reached at local 60.999, so the back hair
    // stayed drawn forever). 1 ms at 60 fps is far below one frame.
    const double frame_epsilon=1e-3;
    const PsbValue* last_live=nullptr;
    size_t left_index=0;
    for(size_t i=0;i<list.array.size();++i) {
        const auto& f=list.array[i];
        const auto at=Num(f.At("time"));
        if(at<=time+frame_epsilon){result.left=&f;result.time=at;left_index=i;if(Num(f.At("type"))!=0)last_live=&f;}
        else {result.right=&f;break;}
    }
    if(!result.left || Num(result.left->At("type"))==0) {
        // Trailing HOLD frame: the official shell keeps drawing the last live
        // frame's authored pose after the motion ends (device evidence: a 立绘
        // stays on screen minutes past lastTime), so hold its content instead
        // of hiding the layer. The held state also drives descendants (the
        // reference host visits children regardless of the live-frame cursor).
        if(last_live) {
            result.left=last_live;
            result.visible=true;
            // A HOLD gap that a later live frame ends is authored absence, not
            // the end of the motion: the node keeps its last pose for its
            // descendants but draws no content of its own. (Asset evidence:
            // an eye-white frame list ends its open pose exactly at the
            // fully-closed variable value with a type-0 key and resumes only
            // later; holding it drew the eye white above the closed lid.)
            for(size_t i=left_index+1;i<list.array.size();++i)
                if(Num(list.array[i].At("type"))!=0){result.visible=false;break;}
        } else {
            result.visible=false;
        }
        return result;
    }
    result.visible=true;
    if(Num(result.left->At("type"))==3 && result.right && Num(result.right->At("type"))!=0) {
        const auto duration=Num(result.right->At("time"))-result.time;
        if(duration>0)result.ratio=std::clamp((time-result.time)/duration,0.0,1.0);
    }
    return result;
}
double Value(const Frame& f,const char* key,double fallback) {
    const auto left=Num(f.left->At("content").At(key),fallback);
    return f.ratio>0?left+(Num(f.right->At("content").At(key),fallback)-left)*f.ratio:left;
}
double Coordinate(const Frame& f,size_t axis) {
    auto read=[axis](const PsbValue& frame) {
        const auto& c=frame.At("content").At("coord").array;return axis<c.size()?Num(c[axis]):0;
    };
    const double left=read(*f.left);return f.ratio>0?left+(read(*f.right)-left)*f.ratio:left;
}
struct Evaluator {
    const EmoteModel& model;
    const std::map<std::string,double>& variables;
    const std::set<std::string>& removed;
    const std::map<const PsbValue*,EmoteMeshCombinator>& combinators;
    const std::map<std::pair<std::string,std::string>,EmoteImage>& images;
    // Nodes whose meshCombine Load could not prove: they keep their own patch
    // and the ancestors' scopes (as if meshCombine were 0), never a guessed merge.
    const std::set<const PsbValue*>& combine_disabled;
    // Nodes whose combine was abandoned by this evaluation's exact checks.
    std::set<const PsbValue*> runtime_fallbacks;
    int mesh_side=kEmoteMeshSide;
    Evaluator(const EmoteModel& m,const std::map<std::string,double>& v,
              const std::set<std::string>& r,
              const std::map<const PsbValue*,EmoteMeshCombinator>& c,
              const std::map<std::pair<std::string,std::string>,EmoteImage>& i,
              const std::set<const PsbValue*>& disabled,int side)
        :model(m),variables(v),removed(r),combinators(c),images(i),combine_disabled(disabled),mesh_side(side) {}
    size_t output_bytes=0;
    void Charge(size_t bytes) {
        if(bytes>kEmoteSceneOutputBytes-output_bytes)
            throw std::runtime_error("E-mote scene output byte budget exceeded");
        output_bytes+=bytes;
    }
    std::vector<EmoteSceneLayer> layers;
    std::set<std::pair<std::string,std::string>> active;
    // Active E-mote stencil composite mask layer labels. A node with
    // stencilType bit 0x4 replaces the inherited list; descendants inherit it
    // (reference host: stencilCompositeMaskLayerList propagation).
    std::vector<std::string> masks;
    // Shape-sync deformation scopes (meshTransform + shape bit + control grid)
    // and the accumulated affine of the current node in the model space.
    struct Structural {
        bool combine=false,shape=false,inherit_shape=true,normal=true;
        // meshTransform==1 with unimplemented low sync channels (&1/&2/&4), and
        // a nonempty control-coordinate mesh: a combine scan meeting either
        // cannot be proven, so that node does not combine (degrade, not throw).
        bool low_sync=false,has_cc=false;
        int side=0;
        Affine2 domain; // complete normalized domain -> model coordinates
        std::array<double,32> own{}; // NEVER an ancestor-combined grid
    };
    struct DeformScope {
        const Structural* identity=nullptr;
        Affine2 acc;                 // scope node's accumulated transform
        std::vector<double> points;  // normalized control grid
        int side=0;
        double width=0,height=0,origin_x=0,origin_y=0;
        double off_x=0,off_y=0;      // content ox/oy
        std::string source,icon;     // icon extent fallback (non-blank nodes)
    };
    // Pointers into recursion-local records: snapshots copy only bounded pointer
    // lists, never the potentially large bp grids. Local records outlive children.
    std::vector<const DeformScope*> scopes;
    std::vector<const Structural*> structure; // same-motion direct-parent chain
    Affine2 acc;              // current node's absolute affine (deformers)
    TransformCtx ctx;         // inheritance source / motion root
    static std::string RemovalKey(const std::string& chara,const std::string& motion,const std::string& layer) {
        return chara+'\n'+motion+'\n'+layer;
    }
    double ParameterTime(const PsbValue& parameter,const PsbValue& param_list,double fallback) const {
        if(parameter.type==PsbValue::Null)return fallback;
        const PsbValue* p=nullptr;
        if(parameter.type==PsbValue::Number) {
            const double index=Num(parameter);
            if(index<0 || index!=std::floor(index) || index>=param_list.array.size())
                throw std::runtime_error("invalid E-mote parameter index");
            p=&param_list.array[size_t(index)];
        } else if(parameter.type==PsbValue::Object) {
            p=&parameter;  // inline parameter (motion-level only in practice)
        } else throw std::runtime_error("invalid E-mote parameterize");
        const auto found=variables.find(p->At("id").string);
        double value=found==variables.end()?0:found->second;
        if(!std::isfinite(value))throw std::runtime_error("non-finite E-mote variable");
        const double lo=Num(p->At("rangeBegin")),hi=Num(p->At("rangeEnd")),division=Num(p->At("division"));
        if(Num(p->At("discretization"))!=0)value=std::trunc(value);
        value=std::clamp(value,std::min(lo,hi),std::max(lo,hi));
        if(Num(p->At("enabled"),1)==0)return fallback;
        return hi!=lo?(value-lo)*division/(hi-lo):0;
    }
    static int SubtreeNodes(const PsbValue& layer) {
        int n=1;
        for(const auto& c:layer.At("children").array)n+=SubtreeNodes(c);
        return n;
    }
    // motion.priority frames list the flattened subtree indices in reverse draw
    // order (reference host: rank = position from the end; unknown entries get
    // the following ranks). Reorders this motion's direct children stably.
    void ApplyPriority(const PsbValue& motion,double time,const PsbValue& layers,
                       std::vector<size_t>* order) {
        const auto& prio=motion.At("priority");
        if(prio.type!=PsbValue::Array || prio.array.empty())return;
        const PsbValue* chosen=nullptr;
        for(const auto& p:prio.array) if(Num(p.At("time"))<=time) chosen=&p;
        if(!chosen)chosen=&prio.array.front();
        const auto& content=chosen->At("content");
        if(content.type!=PsbValue::Array || content.array.empty())return;
        Charge(layers.array.size()*sizeof(int));
        std::vector<int> base(layers.array.size());
        int total=0;
        for(size_t i=0;i<layers.array.size();++i) {
            base[i]=total;
            total+=SubtreeNodes(layers.array[i]);
        }
        if(total<0 || size_t(total)>kEmoteSceneOutputBytes/sizeof(int))
            throw std::runtime_error("E-mote scene priority byte budget exceeded");
        Charge(size_t(total)*sizeof(int));
        std::vector<int> ranks(size_t(total),-1);
        int rank=0;
        for(auto it=content.array.rbegin();it!=content.array.rend();++it,++rank) {
            const int idx=int(it->number);
            if(idx>=0 && idx<total && ranks[size_t(idx)]<0)ranks[size_t(idx)]=rank;
        }
        int next=rank;
        for(auto& r:ranks) if(r<0)r=next++;
        Charge(order->size()*sizeof(size_t)); // stable_sort temporary buffer
        std::stable_sort(order->begin(),order->end(),[&](size_t a,size_t b) {
            return ranks[size_t(base[a])]<ranks[size_t(base[b])];
        });
    }
    void Motion(const std::string& chara,const std::string& name,const std::string& key,double frame,size_t depth) {
        if(depth>64 || !active.emplace(chara,name).second)throw std::runtime_error("recursive E-mote child motion");
        const auto& motion=model.Document().root.At("object").At(chara).At("motion").At(name);
        if(motion.type!=PsbValue::Object)throw std::runtime_error("missing E-mote child motion: "+chara+"/"+name);
        const double end=Num(motion.At("lastTime")),loop=Num(motion.At("loopTime"),-1);
        if(end>0 && frame>=end)frame=loop>=0 && loop<end?loop+std::fmod(frame-loop,end-loop):std::nextafter(end,0.0);
        // Motion-level parameterize selects the whole nested motion's tick from
        // a variable (the shipped models bind mouth/eye variants this way).
        frame=ParameterTime(motion.At("parameterize"),motion.At("parameter"),frame);
        // A nested player inherits OUTER shape scopes, but its meshCombine scan
        // starts at its own local structural roots, never at the attachment.
        auto outer_structure=std::move(structure);
        Charge(65*sizeof(const Structural*));structure={};structure.reserve(65);
        Nodes(motion.At("layer"),motion,key,frame,depth+1,chara,name);
        structure=std::move(outer_structure);
        active.erase({chara,name});
    }
    void Nodes(const PsbValue& nodes,const PsbValue& motion,const std::string& parent,double frame,size_t depth,
                const std::string& chara,const std::string& motion_name) {
        if(depth>64)throw std::runtime_error("E-mote scene nesting too deep");
        if(nodes.array.size()>kEmoteSceneOutputBytes/sizeof(size_t))
            throw std::runtime_error("E-mote scene ordering byte budget exceeded");
        Charge(nodes.array.size()*sizeof(size_t));
        std::vector<size_t> order(nodes.array.size());
        for(size_t i=0;i<order.size();++i)order[i]=i;
        ApplyPriority(motion,frame,nodes,&order);
        for(size_t oi=0;oi<order.size();++oi) {
            const size_t i=order[oi];
            const auto& node=nodes.array[i];double time=frame;
            const auto parent_acc=acc;
            Charge(scopes.size()*sizeof(const DeformScope*));
            auto parent_scopes=scopes;
            const size_t parent_structure_count=structure.size();
            Charge(sizeof(Structural)+sizeof(DeformScope));
            Structural own_structure;
            own_structure.combine=Num(node.At("meshCombine"))!=0;
            own_structure.low_sync=Num(node.At("meshTransform"))==1 &&
                (int64_t(Num(node.At("meshSyncChildMask")))&0x7)!=0;
            DeformScope own_scope;own_scope.identity=&own_structure;
            const auto parent_ctx=ctx;
            if(removed.count(RemovalKey(chara,motion_name,node.At("label").string)))continue;
            const bool stencil_node=Num(node.At("type"))==12;
            size_t saved_mask_bytes=masks.size()*sizeof(std::string);
            for(const auto& mask:masks)saved_mask_bytes+=mask.size();
            Charge(saved_mask_bytes);
            const auto parent_masks=masks;
            if((int64_t(Num(node.At("stencilType")))&0x4)!=0) {
                const auto& list=node.At("stencilCompositeMaskLayerList");
                if(list.type==PsbValue::Array && !list.array.empty()) {
                    size_t cost=list.array.size()*sizeof(std::string);
                    for(const auto& m:list.array)cost+=m.string.size();
                    Charge(cost);
                    masks.clear();
                    masks.reserve(list.array.size());
                    for(const auto& m:list.array)
                        if(m.type==PsbValue::String)masks.push_back(m.string);
                }
            }
            const auto& parameter=node.At("parameterize");
            if(parameter.type!=PsbValue::Null)
                time=ParameterTime(parameter,motion.At("parameter"),time);
            const auto sampled=Sample(node.At("frameList"),time);
            if(!sampled.left && combinators.count(&node))
                throw std::runtime_error("unsupported E-mote combinator without sampled extent");
            // Shape inheritance belongs to the node, including empty frame
            // lists and times before its first frame; children still evaluate.
            const auto& inherit_field=node.At("inheritMask");
            const bool inherit_shape=inherit_field.type!=PsbValue::Number ||
                (int64_t(inherit_field.number)&0x02000000)!=0;
            own_structure.inherit_shape=inherit_shape;
            if(!inherit_shape)scopes.clear();
            Charge(node.At("label").string.size()+Child(parent,i).size());
            if(layers.size()>=4096)throw std::runtime_error("too many E-mote scene layers");
            // Fixed bound keeps vector growth capacity explicit and bounded.
            if(layers.empty()) {Charge(4096*sizeof(EmoteSceneLayer));layers.reserve(4096);}
            EmoteSceneLayer out;out.key=Child(parent,i);
            out.label=node.At("label").string;
            size_t mask_bytes=masks.size()*sizeof(std::string);
            for(const auto& mask:masks)mask_bytes+=mask.size();
            Charge(mask_bytes);
            out.masks=masks;
            out.effective_z=ctx.source.lz;
            // Containers always keep their transform so children stay placed;
            // only a live frame contributes this node's own drawable content
            // (HOLD frames retain the previous live state, including its Z).
            out.visible=true;
            if(sampled.left) {
                out.x=Coordinate(sampled,0);out.y=Coordinate(sampled,1);
                // Static coordinates: X/Y map through the source's 2D linear,
                // whereas Z adds directly. No perspective or depth scaling is
                // inferred from the affine or partial linear-inheritance bits.
                out.effective_z=ctx.source.lz+Coordinate(sampled,2);
                if(!std::isfinite(out.effective_z))throw std::runtime_error("non-finite E-mote effective Z");
                out.angle=Value(sampled,"angle",0);out.scale_x=Value(sampled,"zx",1);out.scale_y=Value(sampled,"zy",1);
                out.opacity=std::clamp(Value(sampled,"opa",255)/255.0,0.0,1.0);
                out.origin_x=Value(sampled,"ox",0);out.origin_y=Value(sampled,"oy",0);
                // Native placement: the parent layer's affine is the child's
                // inheritance source (prepare_child_inherit_source), the linear
                // composes per inheritMask, and partial masks rebuild relative
                // to the enclosing motion root.
                LinearState own;
                own.fx=Value(sampled,"fx",0)!=0;own.fy=Value(sampled,"fy",0)!=0;
                own.rot=Value(sampled,"angle",0);
                own.sx=Value(sampled,"zx",1);own.sy=Value(sampled,"zy",1);
                own.shx=Value(sampled,"sx",0);own.shy=Value(sampled,"sy",0);
                const int64_t inherit_mask=node.At("inheritMask").type==PsbValue::Number
                    ?int64_t(node.At("inheritMask").number):int64_t(0x1fc);
                out.transform_order.clear();
                Charge(node.At("transformOrder").array.size()*sizeof(double));
                out.transform_order.reserve(node.At("transformOrder").array.size());
                for(const auto& v:node.At("transformOrder").array)out.transform_order.push_back(v.number);
                const LinearState inherited=InheritLinear(own,ctx.source.state,inherit_mask);
                const Affine2 own_linear=BuildLinear(out.transform_order,own);
                Affine2 linear;
                if((inherit_mask&0x1fc)==0x1fc)
                    linear=ctx.source.linear.Then(own_linear);
                else if(!ctx.independent)
                    linear=ctx.root_linear.Then(BuildLinear(out.transform_order,
                        RemoveRootLinear(inherited,ctx.root_state,inherit_mask)));
                else
                    linear=BuildLinear(out.transform_order,inherited);
                const auto anchor=ctx.source.linear.Apply(Coordinate(sampled,0),Coordinate(sampled,1));
                out.abs_m11=linear.m11;out.abs_m12=linear.m12;
                out.abs_m21=linear.m21;out.abs_m22=linear.m22;
                out.abs_tx=ctx.source.lx+anchor.first;out.abs_ty=ctx.source.ly+anchor.second;
                out.st_fx=inherited.fx;out.st_fy=inherited.fy;
                out.st_rot=inherited.rot;out.st_sx=inherited.sx;out.st_sy=inherited.sy;
                out.st_shx=inherited.shx;out.st_shy=inherited.shy;
                out.abs_opa=((inherit_mask&0x400)!=0?ctx.source.opacity
                             :(!ctx.independent?ctx.root_opacity:1.0))*
                            std::clamp(Value(sampled,"opa",255)/255.0,0.0,1.0);
                acc=Affine2{linear.m11,linear.m12,linear.m21,linear.m22,out.abs_tx,out.abs_ty};
                // prepare_child_inherit_source: descendants inherit this layer's
                // affine unless it is marked transparent (0x400000).
                if((inherit_mask&0x400000)==0) {
                    ctx.source.linear=Affine2{linear.m11,linear.m12,linear.m21,linear.m22,0,0};
                    ctx.source.lx=out.abs_tx;ctx.source.ly=out.abs_ty;
                    ctx.source.lz=out.effective_z;
                    ctx.source.opacity=out.abs_opa;
                    ctx.source.state=inherited;
                }
                const auto& content=sampled.left->At("content");const auto& src=content.At("src").string;
                Charge(src.size()+content.At("icon").string.size()+32);
                const auto& color_field=content.At("color");
                if(color_field.type==PsbValue::Number) out.color=uint32_t(int64_t(color_field.number));
                else if(color_field.type==PsbValue::Array && !color_field.array.empty() &&
                        color_field.array[0].type==PsbValue::Number)
                    out.color=uint32_t(int64_t(color_field.array[0].number));
                // `bm` low nibble selects the blend mode; the remaining bits
                // are native flags the compositor does not need (reference:
                // emote_blend masks 0x0f only).
                const auto& bm_field=content.At("bm");
                if(bm_field.type==PsbValue::Number) {
                    switch(int64_t(bm_field.number)&0xF) {
                        case 1: out.blend="add";break;
                        case 2: case 5: out.blend="subtract";break;
                        case 3: out.blend="multiply";break;
                        case 4: out.blend="screen";break;
                        default: break;
                    }
                }
                // Single resolved payload feeds BOTH own art and descendant
                // scope. Null/empty bp never hides a cached combinator.
                enum class PatchKind { None, FrameBp, CombinatorGrid };
                PatchKind patch_kind=PatchKind::None;
                std::vector<float> patch;
                std::vector<double> scope_points;
                int patch_side=0;
                const bool publishes_scope=Num(node.At("meshTransform"))==1 &&
                    (int64_t(Num(node.At("meshSyncChildMask")))&0x8)!=0;
                const auto& mesh=content.At("mesh");
                if(!stencil_node) {
                    const auto cached=combinators.find(&node);
                    if(cached!=combinators.end()) {
                        EmoteCombinatorGrid grid;std::string error;bool neutral=false;
                        if(!EvaluateEmoteCombinator(cached->second,variables,grid,error,neutral))
                            throw std::runtime_error(error);
                        // Ownership survives neutral revocation. NEVER fall
                        // through to frame bp, including during interpolation.
                        if(!neutral) {
                            Charge(grid.size()*sizeof(float));
                            patch.assign(grid.begin(),grid.end());patch_side=4;
                            if(publishes_scope) {
                                Charge(grid.size()*sizeof(double));
                                scope_points.assign(grid.begin(),grid.end());
                            }
                            patch_kind=PatchKind::CombinatorGrid;
                        }
                    } else if(!mesh.At("bp").array.empty()) {
                        Charge(mesh.At("bp").array.size()*sizeof(float));
                        patch.reserve(mesh.At("bp").array.size());
                        if(publishes_scope) {
                            Charge(mesh.At("bp").array.size()*sizeof(double));
                            scope_points.reserve(mesh.At("bp").array.size());
                        }
                        for(const auto& x:mesh.At("bp").array) {
                            patch.push_back(float(x.number));
                            if(publishes_scope)scope_points.push_back(x.number);
                        }
                        if(!ParseMeshPatch(patch,&patch_side))throw std::runtime_error("invalid E-mote mesh patch");
                        patch_kind=PatchKind::FrameBp;
                    }
                }
                // Shape-sync (meshSyncChild): a node with meshTransform and the
                // shape bit propagates its control-grid deformation to
                // descendants. The deformers use the node's absolute affine,
                // so every layer keeps its own authored domain.
                const double node_type=Num(node.At("type"));
                bool nested_motion=false;
                std::string nested_chara,nested_motion_name;
                if(src.rfind("src/",0)==0) {const auto path=Path(src,"src/");out.source=path.first;out.icon=path.second;}
                else if(src.rfind("motion/",0)==0) { /* legacy child motion; recursion below */ }
                else if(src=="blank" || src.rfind("blank:",0)==0) {
                    // blank frames carry no texture; a blank descriptor (on src
                    // or icon, "w:h:ox:oy") gives the layout extent/origin.
                    const std::string descriptor=src.rfind("blank:",0)==0?src:content.At("icon").string;
                    if(!descriptor.empty()) {
                        double ox=0,oy=0;
                        if(!ParseBlankDescriptor(descriptor,&ox,&oy))
                            throw std::runtime_error("invalid E-mote blank source");
                        out.origin_x=ox;out.origin_y=oy;
                    }
                } else if(src=="clip" || src=="layout") {
                } else if(src.rfind("shape/",0)==0 || node_type==1) {
                    out.shape=ParseShape(src);
                    if(out.shape==EmoteSceneLayer::Shape::None)out.shape=EmoteSceneLayer::Shape::Rect;
                    out.origin_x=8;out.origin_y=8;  // 16x16 unit figure (SDK)
                } else if(node_type==3) {
                    // Split content model (win exports): src names the nested
                    // character and icon its motion. Frames missing either are
                    // layout-only (the reference host skips them).
                    const auto motion_name=content.At("icon").string;
                    if(!src.empty() && !motion_name.empty()) {
                        nested_motion=true;nested_chara=src;nested_motion_name=motion_name;
                    }
                } else {
                    // Split content model: src names the texture source and icon
                    // the entry inside it; a missing pair is layout-only.
                    const auto icon_name=content.At("icon").string;
                    if(!src.empty() && !icon_name.empty()) {out.source=src;out.icon=icon_name;}
                }
                if(stencil_node || !sampled.visible) {
                    // Mask groups and HOLD frames keep the transform for the
                    // children but never draw their own content.
                    out.source.clear();out.icon.clear();out.mesh.clear();
                    out.shape=EmoteSceneLayer::Shape::None;out.blend.clear();
                    nested_motion=false;
                }
                // Establish eligibility from the effective OWN patch and real
                // source extent, before any ancestor is combined into it.
                bool extent=false;
                if(src=="blank" || src.rfind("blank:",0)==0) {
                    const auto descriptor=src=="blank"?content.At("icon").string:src;
                    extent=ParseBlankExtent(descriptor,&own_scope.width,&own_scope.height,
                                            &own_scope.origin_x,&own_scope.origin_y);
                } else if(!out.source.empty()) {
                    const auto image=images.find({out.source,out.icon});
                    if(image!=images.end()) {
                        own_scope.width=image->second.width;own_scope.height=image->second.height;
                        own_scope.origin_x=image->second.origin_x;own_scope.origin_y=image->second.origin_y;
                        extent=true;
                    }
                    Charge(out.source.size()+out.icon.size()+2);
                    own_scope.source=out.source;own_scope.icon=out.icon;
                }
                own_scope.acc=acc;own_scope.side=patch_side;
                own_scope.off_x=Value(sampled,"ox",0);own_scope.off_y=Value(sampled,"oy",0);
                own_structure.normal=!stencil_node && (node_type==0 || node_type==2);
                own_structure.shape=publishes_scope && patch_kind!=PatchKind::None && extent &&
                    own_scope.width>0 && own_scope.height>0;
                own_structure.side=patch_side;
                own_structure.has_cc=mesh.At("cc").type==PsbValue::Array && !mesh.At("cc").array.empty();
                if(own_structure.shape) {
                    Affine2 normalized;
                    normalized.m11=own_scope.width;normalized.m22=own_scope.height;
                    normalized.tx=-own_scope.origin_x-own_scope.off_x;
                    normalized.ty=-own_scope.origin_y-own_scope.off_y;
                    own_structure.domain=acc.Then(normalized);
                    if(patch_side==4)
                        std::copy(scope_points.begin(),scope_points.end(),own_structure.own.begin());
                }
                if(own_structure.combine && own_structure.shape && !combine_disabled.count(&node)) {
                    const auto valid_domain=[](const Affine2& a) {
                        const double det=a.m11*a.m22-a.m12*a.m21;
                        return std::isfinite(a.m11)&&std::isfinite(a.m12)&&std::isfinite(a.m21)&&
                            std::isfinite(a.m22)&&std::isfinite(a.tx)&&std::isfinite(a.ty)&&
                            std::isfinite(det)&&std::abs(det)>1e-12;
                    };
                    // Exact full D equality is intentionally conservative. No
                    // epsilon-based coordinate conversion or guessed domain map.
                    const auto same_domain=[](const Affine2& a,const Affine2& b) {
                        return a.m11==b.m11&&a.m12==b.m12&&a.m21==b.m21&&a.m22==b.m22&&
                            a.tx==b.tx&&a.ty==b.ty;
                    };
                    // The self domain's validity is not required here: an
                    // ancestor contribution below demands a valid ancestor
                    // domain EQUAL to it, and no ancestor means a no-op.
                    // Any condition that cannot be proven degrades THIS node to
                    // "no combine" (own patch only, ancestor scopes untouched);
                    // the scan is a dry run so a fallback never half-consumes scopes.
                    bool fallback=!own_structure.normal || !inherit_shape || patch_side!=4 ||
                        own_structure.has_cc;
                    std::array<double,32> combined=own_structure.own;
                    size_t scope_top=scopes.size();
                    for(auto it=structure.rbegin();!fallback && it!=structure.rend();++it) {
                        const auto* ancestor=*it;
                        if(ancestor->low_sync || !ancestor->inherit_shape){fallback=true;break;}
                        if(ancestor->shape) {
                            if(ancestor->has_cc || !ancestor->normal || ancestor->side!=4 ||
                               !valid_domain(ancestor->domain) || !same_domain(own_structure.domain,ancestor->domain)) {
                                fallback=true;break;
                            }
                            for(size_t p=0;p<32;++p) {
                                const double identity=double(p%2?(p/2)/4:(p/2)%4)/3;
                                combined[p]+=ancestor->own[p]-identity;
                            }
                            // Consume ONLY the remaining shape-chain head with
                            // this structural identity. Never erase a middle
                            // barrier/anchor or compare grids to guess identity.
                            if(scope_top>0 && scopes[scope_top-1]->identity==ancestor)--scope_top;
                            else for(size_t s=0;s<scope_top;++s)
                                if(scopes[s]->identity==ancestor){fallback=true;break;}
                            if(fallback)break;
                        }
                        // A terminating ancestor contributes BEFORE stopping;
                        // non-shape intermediates still control continuation.
                        if(!ancestor->combine)break;
                    }
                    if(!fallback)
                        for(size_t p=0;p<32;++p)
                            if(!std::isfinite(float(combined[p]))){fallback=true;break;}
                    if(fallback)runtime_fallbacks.insert(&node);
                    else {
                        scopes.resize(scope_top);
                        for(size_t p=0;p<32;++p) {
                            patch[p]=float(combined[p]);scope_points[p]=combined[p];
                        }
                    }
                }
                if(patch_kind!=PatchKind::None) {
                    // Includes BuildWarpedMesh's bounded xy/index scratch.
                    Charge(size_t(mesh_side-1)*(mesh_side-1)*24*sizeof(float)+
                           size_t(mesh_side)*mesh_side*2*sizeof(float)+
                           size_t(mesh_side-1)*(mesh_side-1)*6*sizeof(uint32_t));
                    if(!BuildWarpedMesh(patch,patch_side,mesh_side,&out.mesh))
                        throw std::runtime_error("invalid E-mote mesh patch");
                    for(float value:out.mesh)if(!std::isfinite(value))
                        throw std::runtime_error("nonfinite E-mote warped mesh");
                }
                if(!out.source.empty() && !scopes.empty()) {
                    size_t cost=scopes.size()*sizeof(EmoteSceneLayer::Deformer);
                    for(const auto* scope:scopes)
                        cost+=scope->points.size()*sizeof(double)+scope->source.size()+scope->icon.size();
                    Charge(cost);
                    out.deformers.reserve(scopes.size());
                    // Nearest scope first: the reference applies the innermost
                    // patch before walking outward through the ancestor chain.
                    for(auto it=scopes.rbegin();it!=scopes.rend();++it) {
                        const auto* scope=*it;
                        // Render maps the part into this domain with the inverse,
                        // samples the patch, then maps domain -> part with chain.
                        const Affine2 chain=acc.Inverse().Then(scope->acc);
                        EmoteSceneLayer::Deformer d;
                        d.m11=chain.m11;d.m12=chain.m12;d.m21=chain.m21;
                        d.m22=chain.m22;d.tx=chain.tx;d.ty=chain.ty;
                        d.points=scope->points;d.side=scope->side;
                        d.width=scope->width;d.height=scope->height;
                        d.origin_x=scope->origin_x;d.origin_y=scope->origin_y;
                        d.off_x=scope->off_x;d.off_y=scope->off_y;
                        d.source=scope->source;d.icon=scope->icon;
                        out.deformers.push_back(std::move(d));
                    }
                    if(getenv("ARTC_EMOTE_DUMP") && !out.deformers.empty()) {
                        static std::set<std::string> rep;
                        if(rep.insert(out.label).second)
                            Log(kLogInfo,"emote-def attached to '"+out.label+"' count="+std::to_string(out.deformers.size()));
                    }
                }
                // Resolve sources first, and attach only ancestor scopes above:
                // this node's own bp already warps its art once. Its new scope
                // applies to descendants, not to itself.
                if(!stencil_node && own_structure.shape) {
                    own_scope.points=std::move(scope_points);
                    if(scopes.capacity()<65) {
                        Charge((65-scopes.capacity())*sizeof(const DeformScope*));scopes.reserve(65);
                    }
                    scopes.push_back(&own_scope);
                }
                structure.push_back(&own_structure);
                if(layers.size()>=4096)throw std::runtime_error("too many E-mote scene layers");
                layers.push_back(std::move(out));
                // layers reserves its maximum capacity before the first node;
                // this reference survives all nested Motion/Nodes emissions.
                const auto& placed=layers.back();
                if((sampled.visible && src.rfind("motion/",0)==0) || nested_motion) {
                    // A nested motion is a new local player: its inheritance
                    // source and motion root become the entering node's context.
                    const auto saved_ctx=ctx;
                    TransformCtx child;
                    child.source.linear=Affine2{placed.abs_m11,placed.abs_m12,placed.abs_m21,placed.abs_m22,0,0};
                    child.source.lx=placed.abs_tx;child.source.ly=placed.abs_ty;
                    // A nested player is mounted at this node, even when its
                    // ordinary children bypass it as a transparent source.
                    child.source.lz=placed.effective_z;
                    child.source.opacity=placed.abs_opa;
                    child.source.state=LinearState{placed.st_fx,placed.st_fy,placed.st_rot,
                                                   placed.st_sx,placed.st_sy,placed.st_shx,placed.st_shy};
                    child.root_linear=child.source.linear;
                    child.root_lx=child.source.lx;child.root_ly=child.source.ly;
                    child.root_opacity=child.source.opacity;
                    child.root_state=child.source.state;
                    ctx=child;
                    if(sampled.visible && src.rfind("motion/",0)==0) {
                        const auto path=Path(src,"motion/");
                        Motion(path.first,path.second,placed.key+".000000",time-sampled.time+Num(content.At("motion").At("timeOffset")),depth+1);
                    } else {
                        Motion(nested_chara,nested_motion_name,placed.key+".000000",
                               time-sampled.time+Num(content.At("motion").At("timeOffset")),depth+1);
                    }
                    ctx=saved_ctx;
                }
                Nodes(node.At("children"),motion,placed.key,frame,depth+1,chara,motion_name);
            } else {
                if(layers.size()>=4096)throw std::runtime_error("too many E-mote scene layers");
                layers.push_back(out);
                structure.push_back(&own_structure);
                Nodes(node.At("children"),motion,out.key,frame,depth+1,chara,motion_name);
            }
            masks=parent_masks;
            acc=parent_acc;
            scopes=std::move(parent_scopes);
            structure.resize(parent_structure_count);
            ctx=parent_ctx;
        }
    }
};
// Inspect every reachable frame, not merely frame zero: a later mesh keyframe
// must fail loading rather than suddenly corrupting a character during playback.
void Validate(const EmoteModel& model,const std::string& chara,const std::string& motion,
              std::set<std::pair<std::string,std::string>>& active,
              const std::set<std::string>& removed,
              std::map<std::pair<std::string,std::string>,EmoteImage>& images,size_t& count,size_t depth,
              std::map<const PsbValue*,EmoteMeshCombinator>& combinators,size_t& cache_bytes,
              EmoteAtlasCache& atlases) {
    if(depth>64 || !active.emplace(chara,motion).second)throw std::runtime_error("recursive E-mote motion graph");
    const auto& m=model.Document().root.At("object").At(chara).At("motion").At(motion);
    if(m.type!=PsbValue::Object)throw std::runtime_error("missing E-mote motion");
    Num(m.At("lastTime"));Num(m.At("loopTime"),-1);
    // Motion-level `variable` is an authoring declaration the reference host
    // ignores; motion-level parameterize selects the whole motion's tick.
    const auto& motion_parameterize=m.At("parameterize");
    if(motion_parameterize.type==PsbValue::Number) {
        const double index=Num(motion_parameterize);
        if(index<0 || index!=std::floor(index) || index>=m.At("parameter").array.size())
            throw std::runtime_error("invalid E-mote motion parameter index");
    } else if(motion_parameterize.type==PsbValue::Object) {
        Num(motion_parameterize.At("rangeBegin"));Num(motion_parameterize.At("rangeEnd"));
        Num(motion_parameterize.At("division"));Num(motion_parameterize.At("discretization"));
    } else if(motion_parameterize.type!=PsbValue::Null) {
        throw std::runtime_error("invalid E-mote motion parameterize");
    }
    const auto& motion_priority=m.At("priority");
    if(motion_priority.type==PsbValue::Array) {
        for(const auto& p:motion_priority.array) {
            Num(p.At("time"));
            const auto& content=p.At("content");
            if(content.type!=PsbValue::Null && content.type!=PsbValue::Array)
                throw std::runtime_error("invalid E-mote priority");
            for(const auto& c:content.array)Num(c);
        }
    } else if(motion_priority.type!=PsbValue::Null) {
        throw std::runtime_error("invalid E-mote priority");
    }
    std::function<void(const PsbValue&,size_t)> nodes=[&](const PsbValue& list,size_t level) {
        if(level>64)throw std::runtime_error("E-mote scene nesting too deep");
        for(const auto& n:list.array) {
            if(removed.count(chara+'\n'+motion+'\n'+n.At("label").string))continue;
            if(++count>4096)throw std::runtime_error("too many E-mote nodes");
            const auto& combinator=n.At("meshCombinator");
            const bool has_combinator=combinator.type!=PsbValue::Null;
            if(has_combinator) {
                const auto& combine=n.At("meshCombine");
                if(combine.type!=PsbValue::Null &&
                   (combine.type!=PsbValue::Number || (combine.number!=0 && combine.number!=1)))
                    throw std::runtime_error("unsupported E-mote meshCombine flag at "+n.At("label").string);
                // Low sync channels (&1/&2/&4) and a coexisting nonempty cc are not
                // implemented: they are not applied and CombinePreflight records
                // them (meshSync.lowChannels / mesh.cc) instead of failing Load.
                const auto& entry=n.At("entryType");
                if(entry.type!=PsbValue::Null &&
                   (entry.type!=PsbValue::Number || entry.number!=0))
                    throw std::runtime_error("unsupported E-mote combinator entryType");
                if(n.At("frameList").array.empty() || Num(n.At("frameList").array.front().At("time"))!=0 ||
                   Num(n.At("frameList").array.front().At("type"))==0)
                    throw std::runtime_error("unsupported E-mote combinator without initial live extent");
                if(!combinators.count(&n)) {
                    size_t decoded=0,metadata=0;std::string error;
                    if(!MeasureEmoteCombinator(model.Document(),combinator,decoded,metadata,error))
                        throw std::runtime_error(error);
                    const size_t cost=decoded+metadata+sizeof(EmoteMeshCombinator)+sizeof(PsbValue*)+64;
                    if(cost>kEmoteSceneCombinatorCacheBytes-cache_bytes)
                        throw std::runtime_error("E-mote scene combinator cache byte budget exceeded");
                    cache_bytes+=cost;
                    // Placeholder only: decode AFTER the complete reachable
                    // graph has passed aggregate preflight, in scene.Load.
                    combinators.emplace(&n,EmoteMeshCombinator{});
                }
                for(const auto& f:n.At("frameList").array) {
                    if(Num(f.At("type"))==0)continue;
                    const auto& cc=f.At("content").At("mesh").At("cc");
                    if(cc.type!=PsbValue::Null && cc.type!=PsbValue::Array)
                        throw std::runtime_error("invalid E-mote combinator cc payload");
                    if(Num(n.At("meshTransform"))==1 &&
                       (int64_t(Num(n.At("meshSyncChildMask")))&0x8)!=0) {
                        const auto& c=f.At("content");const auto& src=c.At("src").string;
                        bool extent=src.rfind("src/",0)==0;
                        if(src=="blank" || src.rfind("blank:",0)==0) {
                            double w=0,h=0,ox=0,oy=0;
                            const auto descriptor=src=="blank"?c.At("icon").string:src;
                            extent=ParseBlankExtent(descriptor,&w,&h,&ox,&oy) &&
                                w>0 && h>0 && std::isfinite(ox) && std::isfinite(oy);
                        } else if(src.rfind("motion/",0)!=0 && src!="layout" && src!="clip" &&
                                  src.rfind("shape/",0)!=0 && Num(n.At("type"))!=3 &&
                                  !src.empty() && !c.At("icon").string.empty())extent=true;
                        if(!extent)throw std::runtime_error("unsupported E-mote combinator shape scope without known extent");
                    }
                }
            }
            const auto type=Num(n.At("type"));
            if(type!=0 && type!=1 && type!=2 && type!=3 && type!=10 && type!=12)
                throw std::runtime_error("unsupported E-mote node type at "+n.At("label").string);
            // Only static/additive coord[2] is established. Reject other
            // coordinate spaces, including on stencil containers; do not guess
            // 3D projection, stereovision or ground-correction behavior.
            if(Num(n.At("coordinate")) || Num(n.At("groundCorrection")))
                throw std::runtime_error("unsupported E-mote coordinate/ground correction");
            const bool stencil_node=type==12 || Num(n.At("stencilType"))!=0;
            if(stencil_node) {
                // These containers still supply sampled position/Z to their
                // descendants. Validate future live coordinates too, even
                // though their own drawable content is suppressed below.
                for(const auto& f:n.At("frameList").array)
                    if(Num(f.At("type"))!=0)
                        for(const auto& coordinate:f.At("content").At("coord").array)Num(coordinate);
                // Mask groups do not fail the model: their children still
                // validate and render; the GL compositor clips them with the
                // listed mask parts' alpha (stencilType bit 0x4 only).
                static std::set<std::string> reported;
                if(reported.insert("stencil").second)
                    Log(kLogInfo,"emote: stencil containers present; stencilCompositeMaskLayerList "
                        "masks render as stage-space alpha coverage on GL hosts (only stencilType bit "
                        "0x4 is interpreted; other stencil modes are not applied)");
                nodes(n.At("children"),level+1);
                continue;
            }
            const auto& inherit=n.At("inheritMask");
            if(inherit.type!=PsbValue::Null && inherit.type!=PsbValue::Number)
                throw std::runtime_error("invalid E-mote transform inheritance");
            if(inherit.type==PsbValue::Number && (int64_t(inherit.number)&0x1FC)!=0x1FC) {
                // Partial-inherit masks rebuild selected transform channels
                // relative to the motion root in the evaluator.
                static std::set<std::string> reported;
                if(reported.insert("partial-inherit").second)
                    Log(kLogInfo,"emote: partial transform inheritance (inheritMask) resolved relative to motion root");
            }
            const auto& order=n.At("transformOrder").array;
            if(!order.empty() && (order.size()!=4 || Num(order[0])!=0 || Num(order[1])!=3 || Num(order[2])!=2 || Num(order[3])!=1))
                throw std::runtime_error("unsupported E-mote transform order");
            double previous=-1;
            for(const auto& f:n.At("frameList").array) {
                const double time=Num(f.At("time")),ft=Num(f.At("type"));
                if(time<previous || time<0 || (ft!=0 && ft!=2 && ft!=3))throw std::runtime_error("unsupported E-mote scene keyframe");
                previous=time;if(ft==0)continue;
                const auto& c=f.At("content");
                const std::set<std::string> supported={"mask","src","coord","angle","zx","zy","opa","ox","oy","bm","motion","mesh",
                    "icon","zcc","ccc","cc","color"};
                for(const auto& field:c.object)if(!supported.count(field.first))throw std::runtime_error("unsupported E-mote content: "+field.first);
                for(const char* field:{"mask","angle","zx","zy","opa","ox","oy","bm"})Num(c.At(field));
                // Split content model (win exports): src names a texture source
                // (or blank/a nested character) and icon selects within it; the
                // motion object only contributes mask/timeOffset (the rest is
                // authored nested-motion structure the reference host ignores).
                const auto& motion_field=c.At("motion");
                if(motion_field.type==PsbValue::Object) {
                    if(Num(motion_field.At("mask"))!=0)
                        throw std::runtime_error("unsupported E-mote child motion flags");
                    Num(motion_field.At("timeOffset"));
                } else if(motion_field.type!=PsbValue::Null) {
                    throw std::runtime_error("invalid E-mote child motion");
                }
                const auto& color=c.At("color");
                if(color.type!=PsbValue::Null) {
                    // Packed 0xRRGGBBAA (a scalar or per-corner array); it
                    // legitimately exceeds the generic 1e9 scene-number limit.
                    const auto valid=[&](const PsbValue& x) {
                        return x.type==PsbValue::Number && std::isfinite(x.number) &&
                               x.number>=0 && x.number<=4294967295.0;
                    };
                    if(color.type==PsbValue::Array) {
                        for(const auto& x:color.array) if(!valid(x))
                            throw std::runtime_error("invalid E-mote content color");
                    } else if(!valid(color)) {
                        throw std::runtime_error("invalid E-mote content color");
                    }
                }
                for(const char* field:{"zcc","ccc","cc"}) {
                    const auto& curve=c.At(field);
                    if(curve.type==PsbValue::Null)continue;
                    if(curve.type!=PsbValue::Object)throw std::runtime_error("invalid E-mote content curve");
                    for(const auto& part:curve.object) {
                        for(const auto& x:part.second.array)Num(x);
                        if(part.second.type!=PsbValue::Array)Num(part.second);
                    }
                    static std::set<std::string> reported;
                    if(reported.insert(field).second)
                        Log(kLogInfo,std::string("emote: content curve evaluated as none yet: ")+field);
                }
                for(const auto& coordinate:c.At("coord").array)Num(coordinate);
                Num(c.At("bm"));
                const auto& src=c.At("src").string;
                // A content mesh patch must be a well-formed square grid.
                const auto& mesh=c.At("mesh");
                if(mesh.type==PsbValue::Object) {
                    const auto& bp=mesh.At("bp");
                    if(!has_combinator && bp.type==PsbValue::Array && !bp.array.empty()) {
                        if(bp.array.size()>kEmoteSceneOutputBytes/(2*sizeof(double)))
                            throw std::runtime_error("E-mote mesh point byte budget exceeded");
                        for(const auto& x:bp.array) {
                            if(x.type!=PsbValue::Number || !std::isfinite(x.number) || !std::isfinite(float(x.number)))
                                throw std::runtime_error("invalid E-mote mesh point");
                        }
                        const size_t side=size_t(std::sqrt(double(bp.array.size()/2)));
                        if(bp.array.size()%2!=0 || side<2 || side*side!=bp.array.size()/2)
                            throw std::runtime_error("invalid E-mote mesh patch shape");
                    }
                    // A nonempty cc mesh is drawn affine (deformation unsupported);
                    // CombinePreflight records it as "mesh.cc".
                } else if(mesh.type!=PsbValue::Null) throw std::runtime_error("invalid E-mote mesh");
                if(src.rfind("src/",0)==0) {
                    auto key=Path(src,"src/");
                    if(!images.count(key)) {EmoteImage image;std::string error;
                        if(!model.Image(key.first,key.second,image,error,&atlases))throw std::runtime_error(error);
                        size_t bytes=image.rgba.size();for(const auto& old:images)bytes+=old.second.rgba.size();
                        if(bytes>128*1024*1024)throw std::runtime_error("E-mote decoded images exceed memory limit");
                        images.emplace(std::move(key),std::move(image));}
                } else if(src.rfind("motion/",0)==0) {
                    const auto path=Path(src,"motion/");Validate(model,path.first,path.second,active,removed,images,count,level+1,combinators,cache_bytes,atlases);
                } else if(src=="blank" || src.rfind("blank:",0)==0) {
                    const std::string descriptor=src.rfind("blank:",0)==0?src:c.At("icon").string;
                    if(!descriptor.empty() && !ParseBlankDescriptor(descriptor,nullptr,nullptr))
                        throw std::runtime_error("invalid E-mote blank source");
                } else if(src.rfind("shape/",0)==0 || type==1) {
                    ParseShape(src);  // validates rect/circle/point/quad
                } else if(src=="clip" || src=="layout") {
                } else if(type==3) {
                    const auto motion_name=c.At("icon").string;
                    if(!src.empty() && !motion_name.empty())
                        Validate(model,src,motion_name,active,removed,images,count,level+1,combinators,cache_bytes,atlases);
                } else {
                    // Split content model: src names the texture source, icon the
                    // entry inside it (win exports share one atlas per source).
                    // A missing pair is a layout-only frame (not drawn).
                    const auto icon_name=c.At("icon").string;
                    if(!src.empty() && !icon_name.empty()) {
                        auto key=std::make_pair(src,icon_name);
                        if(!images.count(key)) {EmoteImage image;std::string error;
                            if(!model.Image(key.first,key.second,image,error,&atlases))throw std::runtime_error(error);
                            size_t bytes=image.rgba.size();for(const auto& old:images)bytes+=old.second.rgba.size();
                            if(bytes>128*1024*1024)throw std::runtime_error("E-mote decoded images exceed memory limit");
                            images.emplace(std::move(key),std::move(image));}
                    }
                }
            }
            nodes(n.At("children"),level+1);
        }
    };
    nodes(m.At("layer"),depth+1);active.erase({chara,motion});
}

// Conservative ALL-future-frame/attachment output preflight. Alternative child
// motions are counted in full rather than assuming they cannot coexist. This
// intentionally bounds instances separately from the unique parsed-node cache.
struct OutputPreflight {
    const EmoteModel& model;
    const std::set<std::string>& removed;
    const std::map<const PsbValue*,EmoteMeshCombinator>& combinators;
    size_t bytes=4096*sizeof(EmoteSceneLayer);
    void Charge(size_t cost) {
        if(cost>kEmoteSceneOutputBytes-bytes)
            throw std::runtime_error("E-mote scene aggregate output byte budget exceeded");
        bytes+=cost;
    }
    void Motion(const std::string& chara,const std::string& name,size_t scope_bytes,size_t scope_count) {
        const auto& m=model.Document().root.At("object").At(chara).At("motion").At(name);
        Nodes(m.At("layer"),chara,name,scope_bytes,scope_count);
    }
    void Nodes(const PsbValue& list,const std::string& chara,const std::string& motion,
               size_t inherited_bytes,size_t inherited_count) {
        for(const auto& n:list.array) {
            if(removed.count(chara+'\n'+motion+'\n'+n.At("label").string))continue;
            if(Num(n.At("type"))==12 || Num(n.At("stencilType"))!=0) {
                Charge(sizeof(EmoteSceneLayer)+512+n.At("label").string.size());
                Nodes(n.At("children"),chara,motion,inherited_bytes,inherited_count);
                continue;
            }
            size_t scope_bytes=inherited_bytes,scope_count=inherited_count;
            const auto& inherit=n.At("inheritMask");
            if(inherit.type==PsbValue::Number && (int64_t(inherit.number)&0x02000000)==0)
                scope_bytes=scope_count=0;
            size_t points=combinators.count(&n)?32:0,max_strings=0;
            bool drawable=false;
            for(const auto& f:n.At("frameList").array) {
                if(Num(f.At("type"))==0)continue;
                const auto& c=f.At("content");const auto& src=c.At("src").string;
                if(!combinators.count(&n))points=std::max(points,c.At("mesh").At("bp").array.size());
                max_strings=std::max(max_strings,src.size()+c.At("icon").string.size());
                if(src.rfind("src/",0)==0 || (!src.empty() && src!="blank" &&
                   src.rfind("blank:",0)!=0 && src!="layout" && src!="clip" &&
                   src.rfind("motion/",0)!=0 && src.rfind("shape/",0)!=0 &&
                   Num(n.At("type"))!=3 && !c.At("icon").string.empty()))drawable=true;
            }
            Charge(sizeof(EmoteSceneLayer)+n.At("label").string.size()+512+max_strings*2+
                   65*(sizeof(const Evaluator::DeformScope*)+sizeof(const Evaluator::Structural*)));
            if(points)Charge(points*(sizeof(double)+sizeof(float))+
                size_t(kEmoteMeshSide-1)*(kEmoteMeshSide-1)*30*sizeof(float)+
                size_t(kEmoteMeshSide)*kEmoteMeshSide*2*sizeof(float));
            if(drawable)Charge(scope_bytes+scope_count*sizeof(EmoteSceneLayer::Deformer));
            if(points && Num(n.At("meshTransform"))==1 &&
               (int64_t(Num(n.At("meshSyncChildMask")))&0x8)!=0) {
                const size_t own=points*sizeof(double)+max_strings;
                Charge(own+65*sizeof(Evaluator::DeformScope));
                scope_bytes+=own;++scope_count;
            }
            for(const auto& f:n.At("frameList").array) {
                if(Num(f.At("type"))==0)continue;
                const auto& c=f.At("content");const auto& src=c.At("src").string;
                if(src.rfind("motion/",0)==0) {
                    const auto p=Path(src,"motion/");Motion(p.first,p.second,scope_bytes,scope_count);
                } else if(Num(n.At("type"))==3 && !src.empty() && !c.At("icon").string.empty() &&
                          src.rfind("src/",0)!=0 && src!="blank" && src.rfind("blank:",0)!=0 &&
                          src!="layout" && src!="clip" && src.rfind("shape/",0)!=0)
                    Motion(src,c.At("icon").string,scope_bytes,scope_count);
            }
            Nodes(n.At("children"),chara,motion,scope_bytes,scope_count);
        }
    }
};

// Load-time proof for the finite normal path. This deliberately proves only
// constant relative transforms/extents across ALL authored live frames. It does
// not equate equal blank strings with equal domains, nor sample just frame zero.
// Dynamic common-ancestor singularity is not proven in this release; local
// structural placement paths participating in a candidate proof must be static.
// A nested attachment remains outside that motion's local combine proof.
struct CombinePreflight {
    const EmoteModel& model;
    const std::map<std::pair<std::string,std::string>,EmoteImage>& images;
    const std::set<std::string>& removed;
    const std::map<const PsbValue*,EmoteMeshCombinator>& combinators;
    // Output: nodes whose combine is unproven, plus the feature counts. A node
    // reached through several attachments counts once and is disabled if ANY
    // reachable context fails its proof.
    std::set<const PsbValue*>& disabled;
    EmoteUnsupportedFeatures& features;
    std::set<std::pair<const PsbValue*,std::string>> noted;
    struct Entry {
        const PsbValue* node=nullptr;
        bool shape=false,combine=false,inherit=true,normal=true;
        bool local_known=true,extent_known=true,transparent=false;
        bool low_sync=false,has_cc=false;
        int side=4;
        Affine2 local,extent;
    };
    std::vector<Entry> chain;
    static bool Equal(const Affine2& a,const Affine2& b) {
        return a.m11==b.m11&&a.m12==b.m12&&a.m21==b.m21&&a.m22==b.m22&&a.tx==b.tx&&a.ty==b.ty;
    }
    Entry Describe(const PsbValue& n) const {
        Entry e;e.node=&n;e.combine=Num(n.At("meshCombine"))!=0;
        const auto& inherit=n.At("inheritMask");
        const int64_t mask=inherit.type==PsbValue::Number?int64_t(inherit.number):0x020001fc;
        e.inherit=(mask&0x02000000)!=0;e.transparent=(mask&0x400000)!=0;
        e.local_known=(mask&0x1fc)==0x1fc;
        e.low_sync=Num(n.At("meshTransform"))==1 && (int64_t(Num(n.At("meshSyncChildMask")))&7)!=0;
        const auto type=Num(n.At("type"));
        e.normal=(type==0||type==2)&&Num(n.At("stencilType"))==0;
        bool first=true;
        for(const auto& f:n.At("frameList").array) {
            if(Num(f.At("type"))==0)continue;
            const auto& c=f.At("content");
            LinearState st;st.rot=Num(c.At("angle"));st.sx=Num(c.At("zx"),1);st.sy=Num(c.At("zy"),1);
            st.fx=Num(c.At("fx"))!=0;st.fy=Num(c.At("fy"))!=0;
            st.shx=Num(c.At("sx"));st.shy=Num(c.At("sy"));
            const auto& coord=c.At("coord").array;
            Affine2 local=BuildLinear({},st);
            local.tx=coord.empty()?0:Num(coord[0]);local.ty=coord.size()<2?0:Num(coord[1]);
            double w=0,h=0,ox=0,oy=0;bool extent=false;
            const auto& src=c.At("src").string;
            if(src=="blank"||src.rfind("blank:",0)==0)
                extent=ParseBlankExtent(src=="blank"?c.At("icon").string:src,&w,&h,&ox,&oy);
            else {
                std::pair<std::string,std::string> key;
                if(src.rfind("src/",0)==0)key=Path(src,"src/");
                else if(type!=3 && type!=1 && src!="layout" && src!="clip" && src.rfind("shape/",0)!=0)
                    key={src,c.At("icon").string};
                const auto image=images.find(key);
                if(image!=images.end()) {
                    w=image->second.width;h=image->second.height;ox=image->second.origin_x;oy=image->second.origin_y;extent=true;
                }
            }
            Affine2 domain;domain.m11=w;domain.m22=h;
            domain.tx=-ox-Num(c.At("ox"));domain.ty=-oy-Num(c.At("oy"));
            extent=extent&&w>0&&h>0&&std::isfinite(domain.tx)&&std::isfinite(domain.ty);
            const bool patch=combinators.count(&n)||!c.At("mesh").At("bp").array.empty();
            if(c.At("mesh").At("cc").type==PsbValue::Array && !c.At("mesh").At("cc").array.empty())e.has_cc=true;
            const bool shape=Num(n.At("meshTransform"))==1 &&
                (int64_t(Num(n.At("meshSyncChildMask")))&8)!=0 && patch && extent;
            if(first) {e.local=local;e.extent=domain;first=false;}
            else {
                e.local_known=e.local_known&&Equal(e.local,local);
                e.extent_known=e.extent_known&&Equal(e.extent,domain);
            }
            e.extent_known=e.extent_known&&extent;
            e.shape=e.shape||shape;
            if(!combinators.count(&n) && patch) {
                const size_t points=c.At("mesh").At("bp").array.size()/2;
                if(points!=16)e.side=0;
            }
        }
        if(first)e.local_known=e.extent_known=false;
        return e;
    }
    void Motion(const std::string& chara,const std::string& motion) {
        auto saved=std::move(chain);chain={};chain.reserve(65);
        Nodes(model.Document().root.At("object").At(chara).At("motion").At(motion).At("layer"),chara,motion);
        chain=std::move(saved);
    }
    // nullptr when the combine of `current` is proven for every authored frame;
    // otherwise a short reason tag (lowSync/cc/barrier/domain/dynamic).
    const char* Prove(const Entry& current) const {
        if(!current.normal||!current.inherit||current.side!=4||!current.extent_known)return "domain";
        if(current.has_cc)return "cc";
        for(size_t i=chain.size();i>0;--i) {
            const auto& ancestor=chain[i-1];
            // Every ancestor the scan visits must be provable, whether
            // or not it contributes a grid.
            if(ancestor.low_sync)return "lowSync";
            if(!ancestor.inherit)return "barrier";
            if(ancestor.shape) {
                if(ancestor.has_cc)return "cc";
                if(!ancestor.normal||ancestor.side!=4||!ancestor.extent_known)return "domain";
                // This release does not prove animated common-ancestor
                // singularity/cancellation across interpolation either.
                // Require the entire structural placement proof static.
                for(size_t j=0;j<i;++j) {
                    const auto& upstream=chain[j];
                    const double upstream_det=upstream.local.m11*upstream.local.m22-
                        upstream.local.m12*upstream.local.m21;
                    if(!upstream.local_known||!std::isfinite(upstream_det)||std::abs(upstream_det)<=1e-12)
                        return "dynamic";
                }
                if((int64_t(Num(ancestor.node->At("meshSyncChildMask")))&7)!=0)return "lowSync";
                Affine2 relative;
                // Complete D_current in ancestor-node space. No
                // frame/variable-dependent local transform is guessed.
                for(size_t j=i;j<=chain.size();++j) {
                    const auto& step=j==chain.size()?current:chain[j];
                    const auto& parent=j==i?ancestor:chain[j-1];
                    if(!step.local_known||!step.inherit||parent.transparent)return "dynamic";
                    relative=relative.Then(step.local);
                }
                const double det=relative.m11*relative.m22-relative.m12*relative.m21;
                if(!std::isfinite(det)||std::abs(det)<=1e-12||
                   !Equal(ancestor.extent,relative.Then(current.extent)))
                    return "domain";
            }
            if(!ancestor.combine)break;
        }
        return nullptr;
    }
    void Note(const PsbValue& n,const char* tag) {
        if(noted.emplace(&n,tag).second)features.Add(tag);
    }
    void Nodes(const PsbValue& list,const std::string& chara,const std::string& motion) {
        for(const auto& n:list.array) {
            if(removed.count(chara+'\n'+motion+'\n'+n.At("label").string))continue;
            const Entry current=Describe(n);
            if(current.low_sync)Note(n,"meshSync.lowChannels");
            if(current.has_cc)Note(n,"mesh.cc");
            // Low sync channels are unproven wherever combine is requested,
            // even on a node that is not itself a shape. Any unprovable combine
            // degrades THIS node to "no combine" (never a Load failure).
            const char* reason=nullptr;
            if(current.combine&&current.low_sync)reason="lowSync";
            else if(current.shape&&current.combine)reason=Prove(current);
            if(reason&&disabled.insert(&n).second) {
                features.Add("meshCombine.fallback");
                features.Add(std::string("meshCombine.fallback.")+reason);
            }
            chain.push_back(current);
            if(Num(n.At("type"))!=12 && Num(n.At("stencilType"))==0)
                for(const auto& f:n.At("frameList").array) {
                    if(Num(f.At("type"))==0)continue;
                    const auto& c=f.At("content");const auto& src=c.At("src").string;
                    if(src.rfind("motion/",0)==0) {
                        const auto p=Path(src,"motion/");Motion(p.first,p.second);
                    } else if(Num(n.At("type"))==3&&!src.empty()&&!c.At("icon").string.empty()&&
                              src.rfind("src/",0)!=0&&src!="layout"&&src!="clip"&&src!="blank"&&
                              src.rfind("blank:",0)!=0&&src.rfind("shape/",0)!=0)
                        Motion(src,c.At("icon").string);
                }
            Nodes(n.At("children"),chara,motion);chain.pop_back();
        }
    }
};
}
bool EmoteScene::Load(std::shared_ptr<const EmoteModel> model,std::string& error) {
    try {
        if(!model)throw std::runtime_error("missing E-mote model");
        EmoteScene next;next.model_=std::move(model);const auto& base=next.model_->Document().root.At("metadata").At("base");
        for(const auto& removal:next.model_->Removals())
            next.removed_.insert(RemovalKey(removal.chara,removal.motion,removal.layer));
        std::set<std::pair<std::string,std::string>> active;size_t count=0,cache_bytes=0;
        {
            // Scratch only: decoded shared atlases live for this validation pass
            // and are released before the retained images are used further.
            EmoteAtlasCache atlases;
            Validate(*next.model_,base.At("chara").string,base.At("motion").string,active,next.removed_,
                     next.images_,count,0,next.combinators_,cache_bytes,atlases);
            next.atlas_decodes_=atlases.Decodes();
        }
        OutputPreflight preflight{*next.model_,next.removed_,next.combinators_};
        preflight.Motion(base.At("chara").string,base.At("motion").string,0,0);
        CombinePreflight combine{*next.model_,next.images_,next.removed_,next.combinators_,
                                 next.combine_disabled_,next.features_};
        combine.Motion(base.At("chara").string,base.At("motion").string);
        for(auto& entry:next.combinators_)
            if(!ParseEmoteCombinator(next.model_->Document(),entry.first->At("meshCombinator"),entry.second,error))return false;
        std::vector<EmoteSceneLayer> layers;
        if(!next.Evaluate(0,{},layers,error))return false;
        next.installed_layers_=std::move(installed_layers_);
        // Existing textures belong to the previous model even when icon names
        // match; discard them at the next Render before any new nodes are drawn.
        next.installed_=std::move(installed_);
        for(auto& image:next.installed_)image.second={};
        *this=std::move(next);error.clear();
        LogEmoteUnsupportedFeatures("scene",features_);
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
EmoteUnsupportedFeatures EmoteScene::UnsupportedFeatures() const {
    EmoteUnsupportedFeatures result=features_;
    result.Add("meshCombine.fallback.runtime",runtime_combine_fallbacks_.size());
    return result;
}
bool EmoteScene::Evaluate(double frame,const std::map<std::string,double>& variables,
                          std::vector<EmoteSceneLayer>& output,std::string& error,
                          int mesh_side) const {
    try {
        if(!model_ || !std::isfinite(frame) || frame<0 || frame>1e9)throw std::runtime_error("invalid E-mote scene time");
        mesh_side=std::clamp(mesh_side,2,32);
        const auto& base=model_->Document().root.At("metadata").At("base");
        Evaluator evaluate{*model_,variables,removed_,combinators_,images_,combine_disabled_,mesh_side};
        evaluate.Motion(base.At("chara").string,base.At("motion").string,"",frame,0);
        output=std::move(evaluate.layers);error.clear();
        if(!evaluate.runtime_fallbacks.empty()) {
            // Frame-dependent exact checks abandoned a combine Load had allowed.
            // Counted per node (bounded) and logged once per scene, not per frame.
            const bool first=runtime_combine_fallbacks_.empty();
            runtime_combine_fallbacks_.insert(evaluate.runtime_fallbacks.begin(),evaluate.runtime_fallbacks.end());
            if(first)Log(kLogInfo,"emote: unsupported feature meshCombine.fallback.runtime (scene; not applied, frame keeps own mesh)");
        }
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool EmoteScene::Render(Compositor& c,const std::string& id,double frame,
                        const std::map<std::string,double>& variables,std::string& error,
                        double grayscale,uint32_t multiply,double mesh_division) {
    if(id.empty()){error="missing E-mote layer id";return false;}
    if(!std::isfinite(grayscale) || grayscale<0)grayscale=0;
    const bool gray=grayscale>0;
    const std::string gray_value=gray?std::to_string(std::clamp(grayscale,0.0,1.0)):std::string();
    // setMeshDivisionRatio: coarser grids keep the deformed silhouette but
    // submit fewer vertices (scripts use it as a per-device performance knob).
    const int mesh_side=std::clamp(
        int(std::lround(kEmoteMeshSide*std::clamp(mesh_division,0.05,1.0))),2,kEmoteMeshSide);
    std::vector<EmoteSceneLayer> layers;if(!Evaluate(frame,variables,layers,error,mesh_side))return false;
    // Evaluate emits a priority-ordered preorder with stable source-position
    // keys. Give each node a postorder rank without renaming those keys: child
    // content precedes its node's own art only as a SAME-Z compatibility tie,
    // not a fully established official rule. Effective Z takes precedence;
    // sibling priority survives as the same-Z tie without changing stable IDs.
    std::map<std::string,int> draw_order;
    std::vector<std::string> pending;
    int rank=0;
    for(const auto& l:layers) {
        while(!pending.empty() && !(l.key.size()>pending.back().size() &&
              l.key.compare(0,pending.back().size(),pending.back())==0 &&
              l.key[pending.back().size()]=='.')) {
            draw_order[pending.back()]=rank++;pending.pop_back();
        }
        pending.push_back(l.key);
    }
    while(!pending.empty()){draw_order[pending.back()]=rank++;pending.pop_back();}
    const char* hidden_icons=getenv("ARTC_EMOTE_HIDE");
    std::set<std::string> current;
    std::set<std::string> pictures;
    for(const auto& l:layers) {
        const auto key=id+l.key;current.insert(key);
        if(!l.source.empty() || l.shape!=EmoteSceneLayer::Shape::None)current.insert(key+".000000");
        if(!l.source.empty())pictures.insert(key+".000000");
    }
    // Remove obsolete ancestors before installing new descendants. A frame may
    // switch from a picture to a child motion using the same source position.
    for(const auto& old:installed_layers_)if(!current.count(old)){c.DeleteLayer(old);installed_.erase(old);}
    for(auto old=installed_.begin();old!=installed_.end();) {
        if(!pictures.count(old->first)){c.DeleteLayer(old->first);old=installed_.erase(old);}else ++old;
    }
    for(const auto& l:layers) {
        const auto key=id+l.key;current.insert(key);
        // Layout nodes only carry alpha/visibility: visual layers below carry
        // absolute transforms (flat native placement, no chain accumulation).
        c.SetProps(key,{{"left","0"},{"top","0"},{"rotate","0"},
            {"xscale","100"},{"yscale","100"},
            {"reversex","0"},{"reversey","0"},{"anchorx","0"},{"anchory","0"},
            {"alpha","255"},
            {"visible",l.visible?"1":"0"}});
        if(!c.SetLayerEmoteOrder(key,id,draw_order.at(l.key),l.effective_z)) {
            error="invalid or overlapping E-mote owner: "+id;return false;
        }
        if(l.shape!=EmoteSceneLayer::Shape::None) {
            // Geometry belongs to a leaf, never to the identity structure holder:
            // descendants already carry absolute model transforms of their own.
            const auto part=key+".000000";
            c.SetProps(part,{{"alpha",std::to_string(l.abs_opa*255)},
                {"visible","1"},{"anchorx","0"},{"anchory","0"}});
            if(!c.SetLayerAffine(part,float(l.abs_m11),float(l.abs_m21),float(l.abs_m12),float(l.abs_m22),
                                 float(l.abs_tx),float(l.abs_ty)) ||
               !c.SetLayerEmoteOrder(part,id,draw_order.at(l.key),l.effective_z)) {
                error="invalid E-mote shape geometry: "+part;return false;
            }
        }
        if(!l.source.empty()) {
            const auto image_key=std::make_pair(l.source,l.icon);const auto& image=images_.at(image_key);
            const auto part=key+".000000";current.insert(part);
            if(installed_[part]!=image_key || !c.GetLayerInfo(part).found) {
                if(!c.SetPixels(part,image.rgba.data(),image.width,image.height)){error="E-mote texture upload failed";return false;}
                installed_[part]=image_key;
            }
            std::map<std::string,std::string> part_props;
            // Hiding authored art must not hide its transform container or
            // descendants. Keep the same part and texture for the next render.
            part_props["visible"]=HideIcon(hidden_icons,l.source,l.icon)?"0":"1";
            {
                // The icon lives at offset (-icon.origin, -content.origin) in
                // the node's local space; the node's absolute affine places it.
                const double off_x=-image.origin_x-l.origin_x;
                const double off_y=-image.origin_y-l.origin_y;
                const double a=l.abs_m11,b=l.abs_m12,c2=l.abs_m21,d=l.abs_m22;
                if(!c.SetLayerAffine(part,float(a),float(c2),float(b),float(d),
                    float(a*off_x+b*off_y+l.abs_tx),float(c2*off_x+d*off_y+l.abs_ty))) {
                    error="invalid E-mote image affine: "+part;return false;
                }
                part_props["anchorx"]="0";part_props["anchory"]="0";
                part_props["alpha"]=std::to_string(l.abs_opa*255);
            }
            // SetProps is incremental: explicitly clear effects from prior frames.
            part_props["grayscale"]=gray?gray_value:"0";
            part_props["layermode"]=l.blend.empty()?"normal":l.blend;
            // Player tint times the frame color (both MODULATE2X space).
            const uint32_t part_multiply=l.color!=0?MultiplyColor(multiply,FrameTint(l.color)):multiply;
            char tint_buf[16]={};
            std::snprintf(tint_buf,sizeof(tint_buf),"0x%06X",part_multiply&0xFFFFFFu);
            part_props["colormultiply"]=tint_buf;
            c.SetProps(part,part_props);
            if(!c.SetLayerEmoteOrder(part,id,draw_order.at(l.key),l.effective_z)) {
                error="invalid or overlapping E-mote owner: "+id;return false;
            }
            if(getenv("ARTC_EMOTE_DUMP")) {
                float ex=0,ey=0,ew=0,eh=0,ea=0;bool ev=false;
                for(const auto& pl:c.Layers()) if(pl.id==part){c.EffectiveRect(pl,&ex,&ey,&ew,&eh,&ea,&ev);break;}
                Log(kLogInfo,"emote-dump part="+part+" label='"+l.label+"' src='"+l.source+"' icon='"+l.icon+
                    "' rect="+std::to_string(int(ex))+","+std::to_string(int(ey))+" "+
                    std::to_string(int(ew))+"x"+std::to_string(int(eh))+
                     " def="+std::to_string(l.deformers.size())+
                     " z="+std::to_string(l.effective_z)+
                    " masks="+std::to_string(l.masks.size())+
                    (l.masks.empty()?"":" ["+[&]{std::string j;for(const auto& m:l.masks)j+=m+"|";return j;}()+"]"));
            }
            if(!l.deformers.empty()) {
                // Shape-sync deformation: sample an 8x8 grid over the icon rect,
                // warp it through the ancestor control patches (nearest first),
                // then emit the triangle list in icon-local pixels.
                const int SIDE=8;
                const double off_x=double(image.origin_x)+l.origin_x;
                const double off_y=double(image.origin_y)+l.origin_y;
                auto warped=[&](double u,double v,double* rx,double* ry) {
                    double px=u*double(image.width)-off_x,py=v*double(image.height)-off_y;
                    for(const auto& d:l.deformers) {
                        double dw=d.width,dh=d.height,dox=d.origin_x,doy=d.origin_y;
                        if(!(dw>0)||!(dh>0)) {
                            const auto image=images_.find({d.source,d.icon});
                            if(image==images_.end())continue;
                            dw=image->second.width;dh=image->second.height;
                            dox=image->second.origin_x;doy=image->second.origin_y;
                        }
                        if(d.side<2)continue;
                        const double dx=px-d.tx,dy=py-d.ty;
                        const double det=d.m11*d.m22-d.m12*d.m21;
                        if(!std::isfinite(det)||std::abs(det)<=1e-12)continue;
                        const double inv=1.0/det;
                        const double lx=(d.m22*dx-d.m12*dy)*inv,ly=(-d.m21*dx+d.m11*dy)*inv;
                        const double nx=(lx+dox+d.off_x)/dw,ny=(ly+doy+d.off_y)/dh;
                        if(!(nx>=0&&nx<=1&&ny>=0&&ny<=1))continue;
                        const auto gp=SampleGrid(d.points,d.side,nx,ny);
                        const double wx=gp.first*dw-dox-d.off_x;
                        const double wy=gp.second*dh-doy-d.off_y;
                        px=d.m11*wx+d.m12*wy+d.tx;py=d.m21*wx+d.m22*wy+d.ty;
                    }
                    *rx=px+off_x;*ry=py+off_y;
                };
                std::vector<float> verts;
                verts.reserve(size_t(SIDE-1)*(SIDE-1)*24);
                auto push=[&](double px,double py,double u,double v){
                    double x=0,y=0;warped(px,py,&x,&y);
                    verts.push_back(float(x));verts.push_back(float(y));
                    verts.push_back(float(u));verts.push_back(float(v));
                };
                if(!l.mesh.empty()) {
                    // Compose the node's own bp first; its UVs remain unchanged.
                    for(size_t i=0;i+3<l.mesh.size();i+=4)
                        push(l.mesh[i],l.mesh[i+1],l.mesh[i+2],l.mesh[i+3]);
                } else {
                    auto grid_vertex=[&](int gx,int gy) {
                        const double u=double(gx)/double(SIDE-1),v=double(gy)/double(SIDE-1);
                        push(u,v,u,v);
                    };
                    for(int gy=0;gy<SIDE-1;++gy)
                        for(int gx=0;gx<SIDE-1;++gx) {
                            grid_vertex(gx,gy);grid_vertex(gx+1,gy);grid_vertex(gx,gy+1);
                            grid_vertex(gx+1,gy);grid_vertex(gx+1,gy+1);grid_vertex(gx,gy+1);
                        }
                }
                c.SetLayerMesh(part,verts);
            } else if(!l.mesh.empty()) {
                // Scale the normalized warped grid into icon-local pixels; uv
                // stays the identity grid the icon texture is sampled through.
                std::vector<float> verts;verts.reserve(l.mesh.size());
                for(size_t i=0;i+3<l.mesh.size();i+=4) {
                    verts.push_back(l.mesh[i]*image.width);
                    verts.push_back(l.mesh[i+1]*image.height);
                    verts.push_back(l.mesh[i+2]);
                    verts.push_back(l.mesh[i+3]);
                }
                c.SetLayerMesh(part,verts);
            } else c.SetLayerMesh(part,{});
            // Parts without a mask drop any mask attached in a previous frame.
            if(l.masks.empty())c.SetLayerStageMask(part,0);
        }
    }
    // E-mote stencil composite: resolve each masked part's labels to the
    // closest mask source part and attach the shared stage mask. All layer
    // centers are computed once per frame — EffectiveRect walks the whole
    // ancestor chain, so a per-candidate call made this quadratic (device:
    // two 立绘 with masks dropped the frame loop to ~3 fps).
    {
        std::map<std::string,std::pair<float,float>> centers;
        std::map<std::string,std::vector<std::string>> sources_by_label;
        std::map<std::string,std::string> container_keys;
        {
            // Read-only pass over every layer: memoize ancestor transforms.
            const Compositor::TransformCache transforms(c);
            for(const auto& pl:c.Layers()) {
                if(pl.id.size()<7 || pl.id.compare(pl.id.size()-7,7,".000000")!=0)continue;
                float ex=0,ey=0,ew=0,eh=0,ea=0;bool ev=false;
                c.EffectiveRect(pl,&ex,&ey,&ew,&eh,&ea,&ev);
                if(!ev)continue;
                centers[pl.id]={ex+ew*0.5f,ey+eh*0.5f};
            }
        }
        for(const auto& l:layers) {
            if(l.label.empty() || !current.count(id+l.key))continue;
            container_keys.emplace(l.label,l.key);
            if(l.source.empty())continue;
            sources_by_label[l.label].push_back(id+l.key+".000000");
        }
        // One mask per resolved SOURCE set, not per label list: parts sharing
        // labels (left/right eye) may resolve to different nearest sources, and
        // several owners can share a label list. Keys are owner-scoped.
        std::map<std::vector<std::string>,uint32_t> mask_cache;
        std::set<std::string> used_mask_keys;
        for(const auto& l:layers) {
            if(l.source.empty()||l.masks.empty())continue;
            const auto part=id+l.key+".000000";
            float px=0,py=0;
            if(const auto center=centers.find(part);center!=centers.end()){px=center->second.first;py=center->second.second;}
            std::vector<std::string> sources;
            for(const auto& label:l.masks) {
                const auto candidates=sources_by_label.find(label);
                if(candidates!=sources_by_label.end()) {
                    std::string best;float best_distance=0;
                    for(const auto& candidate_id:candidates->second) {
                        const auto center=centers.find(candidate_id);
                        if(center==centers.end())continue;
                        const float distance=(center->second.first-px)*(center->second.first-px)+
                                             (center->second.second-py)*(center->second.second-py);
                        if(best.empty()||distance<best_distance){best=candidate_id;best_distance=distance;}
                    }
                    if(!best.empty())sources.push_back(best);
                    continue;
                }
                // Mask labels may name a container (no drawable content of its
                // own, e.g. 下半身変形基礎/頭部変形基礎): the mask is then the
                // union of that subtree's drawable parts.
                const auto container=container_keys.find(label);
                if(container==container_keys.end())continue;
                const std::string prefix=container->second+".";
                for(const auto& l2:layers) {
                    if(l2.source.empty()||l2.key.rfind(prefix,0)!=0)continue;
                    if(!current.count(id+l2.key))continue;
                    sources.push_back(id+l2.key+".000000");
                }
            }
            if(sources.empty()){
                static std::set<std::string> reported;
                if(getenv("ARTC_EMOTE_DUMP")&&reported.insert(l.label).second)
                    Log(kLogInfo,"emote-mask unresolved for '"+l.label+"' labels="+[&]{std::string j;for(const auto& m:l.masks)j+=m+"|";return j;}());
                c.SetLayerStageMask(part,0);continue;}
            auto cached=mask_cache.find(sources);
            if(cached==mask_cache.end()) {
                std::string key=id+'\n';
                for(const auto& source:sources)key+=source+'\n';
                used_mask_keys.insert(key);
                cached=mask_cache.emplace(sources,c.RenderStageMask(key,sources)).first;
            }
            // A mask whose sources did not draw leaves the part unmasked
            // (reference: a stencil without drawn mask layers is disabled).
            if(cached->second==0){c.SetLayerStageMask(part,0);continue;}
            c.SetLayerStageMask(part,cached->second);
        }
        // Masks no longer requested (source set changed) free their textures;
        // every part was re-attached or cleared above.
        for(const auto& old_key:mask_keys_)
            if(!used_mask_keys.count(old_key))c.ReleaseStageMasks(old_key);
        mask_keys_=std::move(used_mask_keys);
    }
    // Opt-in source-image candidate sampling, not framebuffer provenance.
    // Production rectangle queries supply candidate order; deformation,
    // clipping, masks, effects and blending require the actual render pipeline.
    if(const char* probe=getenv("ARTC_EMOTE_PIXEL")) {
        std::vector<std::pair<float,float>> probe_points;
        {
            const char* cursor=probe;
            float px=0,py=0;int consumed=0;
            while(std::sscanf(cursor,"%f,%f%n",&px,&py,&consumed)==2) {
                if(std::isfinite(px) && std::isfinite(py))probe_points.push_back({px,py});
                cursor+=consumed;
                if(*cursor==';')++cursor;
                else break;
            }
        }
        std::map<std::string,const EmoteSceneLayer*> parts;
        std::map<std::string,const Layer*> compositor_layers;
        for(const auto& l:layers)if(!l.source.empty())parts.emplace(id+l.key+".000000",&l);
        for(const auto& l:c.Layers())compositor_layers.emplace(l.id,&l);
        for(const auto& probe_point:probe_points) {
            const float qx=probe_point.first,qy=probe_point.second;
            std::string report="emote-pixel ("+std::to_string(qx)+","+std::to_string(qy)+
                ") mode=source-candidates order=production-rect-front-to-back final-pixel=unresolved";
            size_t count=0;
            auto candidate=[&](const std::string& part,bool ordered) {
                const auto scene_part=parts.find(part);
                const auto installed_part=compositor_layers.find(part);
                if(scene_part==parts.end() || installed_part==compositor_layers.end())return;
                const auto& l=*scene_part->second;
                const auto& cl=*installed_part->second;
                const auto m=c.EffectiveTransform(cl);
                if(!m.visible || !std::isfinite(m.alpha) || m.alpha<=0)return;
                const auto image=images_.find({l.source,l.icon});
                if(image==images_.end())return;
                const bool mesh=!cl.mesh.empty();
                // The public point query is affine-only: a mesh outside that
                // rectangle may still cover the point, but has no query rank.
                if(!ordered && !mesh)return;
                std::string unresolved;
                auto reason=[&](const char* text){if(!unresolved.empty())unresolved+=',';unresolved+=text;};
                if(mesh)reason("mesh");
                if(!l.masks.empty() || cl.effect.stage_mask || !cl.effect.mask.empty())reason("mask");
                if(cl.effect.blend!="normal")reason("blend");
                if(cl.effect.Active())reason("effect");
                for(size_t dot=part.rfind('.');dot!=std::string::npos;dot=part.rfind('.',dot-1)) {
                    const auto parent=compositor_layers.find(part.substr(0,dot));
                    if(parent!=compositor_layers.end() && parent->second->effect.Active()) {
                        reason("ancestor-effect");break;
                    }
                    if(dot==0)break;
                }
                const double det=double(m.a)*m.d-double(m.b)*m.c;
                if(!std::isfinite(det) || std::abs(det)<1e-12)return;
                const double dx=double(qx)-m.tx,dy=double(qy)-m.ty;
                const double lx=(m.d*dx-m.c*dy)/det;
                const double ly=(-m.b*dx+m.a*dy)/det;
                const auto& original=image->second;
                // Check fractional coordinates before truncation: -0.5 is
                // outside the source image, not texel zero.
                const bool inside=lx>=0 && ly>=0 && lx<original.width && ly<original.height;
                const uint8_t* texel=inside?&original.rgba[(size_t(ly)*original.width+size_t(lx))*4]:nullptr;
                if(!mesh && (!texel || texel[3]==0))return;
                ++count;
                report+=" | candidate="+part+" label='"+l.label+"' source="+l.source+"/"+l.icon;
                if(!ordered)report+=" order=unresolved(outside-affine-query)";
                if(texel)report+=" affine-source-rgba="+std::to_string(texel[0])+","+
                    std::to_string(texel[1])+","+std::to_string(texel[2])+","+std::to_string(texel[3]);
                else report+=" affine-source-rgba=outside";
                report+=" effective-alpha="+std::to_string(m.alpha);
                if(!unresolved.empty())report+=" unresolved="+unresolved;
            };
            const auto ordered=c.HitLayers(qx,qy);
            std::set<std::string> queried;
            for(const auto& part:ordered){queried.insert(part);candidate(part,true);}
            for(const auto& part:parts)if(!queried.count(part.first))candidate(part.first,false);
            report+=" candidates="+std::to_string(count);
            Log(kLogInfo,report);
        }
    }
    installed_hit_owner_=id;installed_hits_.clear();
    for(const auto& l:layers) {
        if(l.shape!=EmoteSceneLayer::Shape::None)
            installed_hits_.push_back({id+l.key+".000000",l.label,l.shape});
        if(!l.source.empty())
            installed_hits_.push_back({id+l.key+".000000",l.label,EmoteSceneLayer::Shape::None});
    }
    // One-shot placement diagnostic: the first few frames report how many
    // textured parts are effectively visible and where they land on the stage.
    static std::map<std::string,int> render_counts;
    const int rendered=render_counts[id]++;
    if(rendered==1) {
        size_t parts=0;float minx=1e9f,miny=1e9f,maxx=-1e9f,maxy=-1e9f;
        std::string sample;
        for(const auto& l:layers) {
            if(l.source.empty())continue;
            const std::string part=id+l.key+".000000";
            const Layer* cl=nullptr;
            for(const auto& pl:c.Layers()) if(pl.id==part){cl=&pl;break;}
            if(!cl)continue;
            float ex=0,ey=0,ew=0,eh=0,ea=0;bool ev=false;
            c.EffectiveRect(*cl,&ex,&ey,&ew,&eh,&ea,&ev);
            if(!ev||ea<=0)continue;
            ++parts;
            if(sample.empty()) sample=l.label;
            minx=std::min(minx,ex);miny=std::min(miny,ey);
            maxx=std::max(maxx,ex+ew);maxy=std::max(maxy,ey+eh);
        }
        Log(kLogInfo,"emote: render "+id+" frame="+std::to_string(frame)+
            " parts="+std::to_string(parts)+(parts?" bbox=("+std::to_string(int(minx))+","+
            std::to_string(int(miny))+")-("+std::to_string(int(maxx))+","+std::to_string(int(maxy))+") sample="+sample:""));
    }
    installed_layers_=std::move(current);return true;
}

bool EmoteScene::HitTest(Compositor& c,const std::string& id,const std::string& label,
                         double x,double y) const {
    if(id.empty() || id!=installed_hit_owner_ || !std::isfinite(x) || !std::isfinite(y) ||
       x<0 || y<0 || x>=c.StageWidth() || y>=c.StageHeight())return false;
    for(const auto& hit:installed_hits_) {
        if(!label.empty() && hit.label!=label)continue;
        const auto* layer=static_cast<const Layer*>(nullptr);
        for(const auto& l:c.Layers()) if(l.id==hit.id){layer=&l;break;}
        if(!layer)continue;
        const auto m=c.EffectiveTransform(*layer);
        const double det=double(m.a)*m.d-double(m.b)*m.c;
        if(!m.visible || !std::isfinite(m.alpha) || m.alpha<=0 ||
           !std::isfinite(det) || std::abs(det)<1e-12)continue;
        const double dx=x-m.tx, dy=y-m.ty;
        const double px=( m.d*dx-m.c*dy)/det;
        const double py=(-m.b*dx+m.a*dy)/det;
        if(!std::isfinite(px) || !std::isfinite(py))continue;
        // The SDK's shape figure is a 16x16 unit square; the layer transform
        // already carries the node scale/rotation.
        switch(hit.shape) {
        case EmoteSceneLayer::Shape::Rect:
            if(std::abs(px)<=8 && std::abs(py)<=8)return true;break;
        case EmoteSceneLayer::Shape::Circle:
            if(px*px+py*py<=64)return true;break;
        case EmoteSceneLayer::Shape::Point:
            if(std::abs(px)<=1 && std::abs(py)<=1)return true;break;
        case EmoteSceneLayer::Shape::Quad:
            if(std::abs(px)+std::abs(py)<=8)return true;break;
        case EmoteSceneLayer::Shape::None:
            if(!layer->texture)break;
            if(!layer->mesh.empty() && layer->mesh.size()%4==0) {
                // Mesh UVs are submitted as authored, without an extra crop;
                // follow the live triangle geometry rather than its quad AABB.
                for(size_t i=0;i+11<layer->mesh.size();i+=12)
                    if(InTriangle(px,py,layer->mesh.data()+i))return true;
            } else if(layer->w>0 && layer->h>0 && px>=0 && py>=0 &&
                      px<=layer->w && py<=layer->h)return true;
            break;
        }
    }
    return false;
}
void EmoteScene::Remove(Compositor& c,const std::string& id) {
    // DeleteLayer on a dotted child cascades to its own descendants; keys here
    // are already full dotted paths, but the loop stays O(installed).
    for(const auto& key:installed_layers_)c.DeleteLayer(key);
    for(const auto& key:mask_keys_)c.ReleaseStageMasks(key);
    mask_keys_.clear();
    installed_layers_.clear();installed_.clear();installed_hits_.clear();installed_hit_owner_.clear();
}
}
