#pragma once
#include "pack/psb.h"
#include "render/emote_control.h"
#include "render/emote_features.h"
#include <map>
#include <optional>
#include <set>

namespace artc {
struct EmoteImage {
    int width=0,height=0;
    double origin_x=0,origin_y=0;
    std::vector<uint8_t> rgba;
};
// Transient decoded-atlas cache for repeated EmoteModel::Image calls. win/common
// models crop every icon out of one shared texture per source; decoding that
// texture once per icon is the dominant cost of loading a real model. The cache
// keeps whole decoded sources (RGBA8) for the duration of a bulk operation only:
// it is bounded by `limit` bytes (least recently used sources are evicted; a
// source larger than the limit is simply not retained) and is scratch, never
// counted in or mixed with the images a caller retains. One cache serves one
// model whose document is not reloaded meanwhile. Not thread-safe.
constexpr size_t kEmoteAtlasCacheBytes=128*1024*1024;
class EmoteAtlasCache {
public:
    explicit EmoteAtlasCache(size_t limit=kEmoteAtlasCacheBytes):limit_(limit) {}
    size_t Decodes() const {return decodes_;}   // full-source decodes performed
    size_t Hits() const {return hits_;}         // icons served from a cached source
    size_t Bytes() const {return bytes_;}       // currently retained scratch bytes
    size_t Limit() const {return limit_;}
private:
    friend class EmoteModel;
    struct Entry {
        std::string source;
        std::vector<uint8_t> rgba;
        bool exact=true;   // resource length equalled level 0 (no mip chain)
        uint64_t stamp=0;
    };
    Entry* Find(const void* owner,const std::string& source);
    Entry* Insert(const void* owner,std::string source,std::vector<uint8_t>&& rgba,bool exact);
    size_t limit_,bytes_=0,decodes_=0,hits_=0;
    uint64_t clock_=0;
    const void* owner_=nullptr;
    std::vector<Entry> entries_;
};
struct EmoteFrame {double time=0,value=0,easing=0;bool terminal=false;};
struct EmoteTrack {std::string variable;bool instant=false;std::vector<EmoteFrame> frames;};
struct EmoteTimeline {
    double last_time=0,loop_begin=0,loop_end=0;
    bool difference=false;
    std::vector<EmoteTrack> tracks;
};
// metadata.eyeControl — automatic blinking. The label names a model variable
// (writers target it like any other variable); begin/end are the variable
// values of the blink arc and frames is its duration at 60 fps.
struct EmoteBlink {
    std::string variable;
    double begin=0,end=0;
    double frames=1;              // blinkFrameCount, >=1
    double interval_min=0,interval_max=0;
    bool enabled=true,blink_enabled=true;
};
// metadata.selectorControl — option lists. Selecting option v (a variable
// value, usually an index) crossfades each item: the selected item gets
// onValue, neighbours blend linearly (reference host formula).
struct EmoteSelectorItem {
    std::string label;
    double on=1,off=0;
};
struct EmoteSelector {
    std::string label;
    bool enabled=true;
    std::vector<EmoteSelectorItem> items;
};
// metadata.attrcomp remove rules with value<=0: the named layer is not part of
// the motion (the reference host marks it removed at load time).
struct EmoteRemoval {
    std::string chara,motion,layer;
};
// Resource/animation-data foundation, intentionally separate from the future
// mesh/physics renderer. Loading this object does NOT mean an E-mote player is
// available; no original SDK version or complete playback support is advertised.
class EmoteModel {
public:
    bool Load(PsbDocument document,std::string& error);
    // With a cache, the shared atlas texture of win/common sources is decoded at
    // most once per cache (until evicted); results are byte-identical to the
    // uncached call, including error messages. Per-icon (krkr) textures are never
    // cached.
    bool Image(const std::string& source,const std::string& icon,EmoteImage& out,std::string& error,
               EmoteAtlasCache* cache=nullptr) const;
    const PsbDocument& Document() const {return document_;}
    const std::map<std::string,EmoteTimeline>& Timelines() const {return timelines_;}
    const std::set<std::string>& Variables() const {return variables_;}
    const std::vector<EmoteBlink>& Blinks() const {return blinks_;}
    const std::vector<EmoteSelector>& Selectors() const {return selectors_;}
    const std::vector<EmoteRemoval>& Removals() const {return removals_;}
    bool Mirrored() const {return mirror_;}
    // Metadata features present but not implemented (e.g. "clampControl.type0").
    // They are ignored (never approximated) and reported; Load still succeeds.
    const EmoteUnsupportedFeatures& UnsupportedFeatures() const {return features_;}
    // E10 controls (see render/emote_control.h). The plan is built during Load
    // from ACTUALLY registered eye/selector labels (enabled eyeControl variables
    // and enabled selector labels; variableList/timeline declarations and
    // disabled controls do not occupy labels). Its instant set (authored +
    // successful absent-key defaults) is what EmoteTrack::instant uses.
    const EmoteControlConfig& Controls() const {return control_;}
    const EmoteControlRegistryPlan& ControlPlan() const {return control_plan_;}
    // charaProfile queries: authored floats, nullopt when absent; never insert.
    std::optional<float> Height() const {return control_.ProfileHeight();}
    std::optional<float> FindPixelMarker(const std::string& name) const {return control_.PixelMarker(name);}
    // Mirror variable sign-match metadata, not legal SetVariableDiff pairs.
    // Parsed independently of stereoControl. EmotePlayer negates the composed
    // value of labels containing any item (raw-byte substring) while its
    // effective mirror (Mirrored() XOR user request) is on.
    const std::vector<std::string>& MirrorVariableMatches() const {return mirror_variable_matches_;}
    // Legacy derived extension (KiriKiri-derived policy), NOT native Android
    // MMotionPlayer sync or animation completion semantics: maximum authored
    // content time over non-parameterized nodes, following child motions.
    // Retained for existing C++ consumers; <0 when unknown.
    double SyncFrame(const std::string& chara,const std::string& motion) const;
    // Read-only motion.tag query, separate from legacy SyncFrame. For a normal
    // non-loop motion: first tag in authored order with type==1, time>=current
    // and content.sync true; otherwise lastTime. Load locally rejects unordered
    // tags (it does not assume official behavior for malformed/unsorted input).
    // nullopt: loop no-op, unknown motion, or non-finite current. No player clock,
    // wait/completion state or Lua binding is changed by this query.
    std::optional<double> NextMotionSyncFrame(const std::string& chara,
                                             const std::string& motion,double current) const;
    // Offline keyframe sampling for renderer development. Full SDK controller,
    // transition queue, difference mixing and physics evaluation are separate.
    bool Sample(const std::string& timeline,double frame,std::map<std::string,double>& values) const;
private:
    PsbDocument document_;
    std::map<std::string,EmoteTimeline> timelines_;
    std::set<std::string> variables_;
    std::vector<EmoteBlink> blinks_;
    std::vector<EmoteSelector> selectors_;
    std::vector<EmoteRemoval> removals_;
    std::vector<std::string> mirror_variable_matches_;
    EmoteControlConfig control_;
    EmoteControlRegistryPlan control_plan_;
    EmoteUnsupportedFeatures features_;
    std::map<std::pair<std::string,std::string>,double> sync_frames_;
    struct MotionTagSync {
        double last_time=0;
        bool loop=false;
        std::vector<double> frames;
    };
    std::map<std::pair<std::string,std::string>,MotionTagSync> motion_tag_sync_;
    bool mirror_=false;
};
}
