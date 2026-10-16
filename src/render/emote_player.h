#pragma once
#include "render/emote_scene.h"
#include <cstdint>
#include <deque>
#include <set>
#include <utility>
namespace artc {
// Playback controller bridging the original Lua E-mote API to the
// EmoteModel/EmoteScene foundation. Frame semantics follow the Artemis engine
// contract recovered from the shipped scripts and the art3m1s-core binding:
// timeline positions are FRAMES at 60 fps; the Lua layer passes frame counts
// for transitions/fades/progress (lua_engine.cpp converts to the millisecond
// clock used here); setters accept (value, transition ms, ease) with the ease
// weight ease>=0 ? ease+1 : 1/(1-ease). Official Android normal-path flags and
// command control behavior are described in docs/emote-driver-contract.md (E3).
// Outer-force/outer-rotation physics is not simulated here. The Lua boundary
// registers dedicated validators for setOuterForce/setOuterRot and reports
// unsupported for valid calls instead of pretending to apply physics.
class EmotePlayer {
public:
    enum : int { kTimelineParallel = 1, kTimelinePrivateDiff = 2 };
    static constexpr double kFramesPerMillisecond = 60.0 / 1000.0;
    static constexpr double kMaxProgressMs = 60000.0;

    // Atomic. Also registers the model's E10 plan: each enabled transition /
    // orbit / immediate label owns a base Animated (declared or not in
    // variableList). An enabled clampControl (type 0 unestablished, type 1 pair
    // projection unimplemented) is ignored, never approximated by scalar
    // clamping, and reported through UnsupportedFeatures(); Load still succeeds.
    bool Load(std::shared_ptr<const EmoteModel> model, std::string& error);
    bool Active() const { return model_ != nullptr; }
    // Model + scene features that are present but not applied (tag -> count).
    // Empty for an inactive player. Fixed at Load; never grows per frame.
    EmoteUnsupportedFeatures UnsupportedFeatures() const;

    // ---- timeline control ----
    // bit0 clear stops all timelines first; bit1 creates private non-instant
    // difference controls. Neither bit queues timelines.
    // Playback rejects zero-width loops and undeclared track variables before
    // mutation (offline Sample is separate). Open-ended nonloops (no lastTime,
    // no loop; the shipped expression timelines) play until stopped and are
    // reported by EmoteModel as "timeline.openEnded".
    bool PlayTimeline(const std::string& label, int flags, std::string& error);
    void StopTimeline(const std::string& label);
    void StopAllTimelines();
    bool IsTimelinePlaying(const std::string& label) const;
    bool IsLoopTimeline(const std::string& label) const;
    double TimelineTotalFrames(const std::string& label) const;  // <0 unknown
    // FadeIn starts with flags=3 (parallel/private diff); fadeOut
    // removes the timeline once its blend reaches zero. The driver contract is
    // (label, time_ms, ease) — there is no separate fade flag.
    bool FadeInTimeline(const std::string& label, double duration_ms, double ease,
                        std::string& error);
    bool FadeOutTimeline(const std::string& label, double duration_ms, double ease,
                         std::string& error);
    // Direct blend set cancels a running fade (and its auto-stop). The driver
    // contract is (label, ratio, time_ms, ease, flags); a non-zero flags arms
    // the fade-out stop (the timeline is removed when the blend reaches zero).
    bool SetTimelineBlendRatio(const std::string& label, double ratio, std::string& error);
    bool SetTimelineBlendRatio(const std::string& label, double ratio, double transition_ms,
                               double ease, bool fade_out_stop, std::string& error);
    double TimelineBlendRatio(const std::string& label, bool* found) const;
    // Local setTimeline(label, loop) extension: loop=false runs through to the
    // authored end/removes instead of wrapping; true enables looping while live.
    bool SetTimelineHoldEnd(const std::string& label, bool loop, std::string& error);

    // ---- enumeration (model order for labels, insertion order for playing) ----
    int CountMainTimelines() const;
    std::string MainTimelineLabelAt(int index) const;
    int CountDiffTimelines() const;
    std::string DiffTimelineLabelAt(int index) const;
    int CountPlayingTimelines() const;
    std::string PlayingTimelineLabelAt(int index) const;
    int PlayingTimelineFlagsAt(int index) const;
    int CountVariables() const;
    std::string VariableLabelAt(int index) const;

    // ---- variables (labels come from the model; defaults are 0) ----
    // Immediate controls (explicit immediateControl or absent-key defaults)
    // write the current value at once, ignoring duration and queuing.
    bool SetVariable(const std::string& label, double value, double transition_ms,
                     double ease, std::string& error);
    double GetVariable(const std::string& label, bool* found) const;
    // Ordinary driver path (docs/emote-driver-contract.md, E2): one animated
    // control per ordered pair, initially zero. The first string is identity;
    // controls accumulate into the SECOND variable during scene composition,
    // without replacing the base values returned by GetVariable.
    bool SetVariableDiff(const std::string& label, const std::string& pair, double value,
                         double transition_ms, double ease, std::string& error);
    // Missing controls read zero (including undeclared labels); found describes
    // the pair control, not the existence of either base variable.
    double GetVariableDiff(const std::string& label, const std::string& pair,
                           bool* found = nullptr) const;
    // Independent Animated command queuing, default false. Positive-duration
    // commands append when true; immediate sets clear that channel's queue.
    void SetQueuing(bool on) { queuing_ = on; }
    bool GetQueuing() const { return queuing_; }

    // ---- player transforms (applied to the container layer) ----
    // Artemis contract: setCoord(x, y, z, angle) is an instant placement
    // (z is depth and is not used by the 2D compositor); setScale(scale,
    // origin_x, origin_y) scales uniformly around a model-space pivot.
    void SetCoordAngle(double x, double y, double z, double angle);
    void SetScaleOrigin(double scale, double origin_x, double origin_y);
    // Engine-specific eased variants kept for the framework setters/tests.
    void SetCoord(double x, double y, double transition_ms, double ease);
    void SetScale(double sx, double sy, double transition_ms, double ease);
    void SetRot(double degrees, double transition_ms, double ease);
    // 0xRRGGBBAA: the game scripts build 0xRRGGBBff and the native driver
    // initializes to 0x808080FF (gray). E-mote tints use MODULATE2X, so 0x80
    // is the neutral channel (the renderer doubles and clamps); the reference
    // host documents the same for corner colors. Alpha is the low byte.
    void SetColor(uint32_t rrggbbaa, double transition_ms, double ease);
    uint32_t GetColor() const;
    // 0..1 luminance blend (native SetGrayscale(value, time, ease)); applied to
    // the textured scene layers at render time.
    void SetGrayscale(double value, double transition_ms, double ease);
    double GetGrayscale() const { return grayscale_.value; }
    // Engine knobs the game scripts set: mesh subdivision ratio and the
    // hair/bust sway amplitudes. Mesh ratio controls rendered warp subdivision;
    // hair/bust amplitudes are recorded (clamped), but physics is not simulated.
    void SetMeshDivisionRatio(double ratio);
    double MeshDivisionRatio() const { return mesh_division_ratio_; }
    // Current base-motion frame (diagnostics/tests).
    double BaseFrame() const { return base_frame_; }
    // Native createEmoteLayer width/height: the model's coordinate origin sits
    // at the centre of that box (reference host model_origin), before setCoord.
    void SetLayerSize(int width, int height) { layer_w_ = width; layer_h_ = height; }
    void SetHairScale(double scale);
    double HairScale() const { return hair_scale_; }
    void SetBustScale(double scale);
    double BustScale() const { return bust_scale_; }
    // metadata.selectorControl crossfade: the selected option gets onValue and
    // neighbours blend by min(|value-index|,1) (reference host formula).
    // Exposed for tests/diagnostics.
    static double SelectorOptionValue(const EmoteSelector& selector, double value, size_t index);
    void GetCoord(double* x, double* y) const;
    void GetScale(double* x, double* y) const;
    double GetRot() const;
    double GetOriginX() const { return origin_x_; }
    double GetOriginY() const { return origin_y_; }
    double GetZ() const { return z_; }
    // Three mirror states: metadata (model, fixed at Load), user (SetMirror) and
    // effective = metadata XOR user. SetMirror ASSIGNS the user request (it is
    // not a toggle). Mirror() reports the user request; EffectiveMirror() is
    // what geometry and the variable sign match consume. The official Android
    // sample's Lua registers no setMirror: it is a local extension here.
    void SetMirror(bool mirror) { user_mirror_ = mirror; }
    bool Mirror() const { return user_mirror_; }
    bool EffectiveMirror() const { return metadata_mirror_ != user_mirror_; }
    bool IsMirrored() const { return EffectiveMirror(); }
    void Show();
    void Hide();
    bool IsHidden() const { return hidden_; }

    // ---- clock ----
    // Non-finite deltas are ignored; finite positive deltas keep the existing
    // kMaxProgressMs cap. Every blink consumes that same accepted delta.
    void Progress(double delta_ms);
    // Forward nonloops to their end/remove; cross a loop once; finish global
    // Animated command queues. Does NOT advance base/scene/blink clocks.
    void Step();
    bool IsAnimating() const;
    // Forward/remove nonloops, retain loops, finish global command queues.
    void Skip();
    // Advance the base motion clock to the model's sync frame (the reference
    // hosts treat it as the meaningful end of the base motion). No-op when the
    // model has no sync frame.
    void SkipToSync();
    // Nonloop main tracks submit remaining commands and are removed; private
    // diff tracks fade out over 20 frames once. Global transitions are untouched.
    void Pass();

    // ---- rendering ----
    // SetProps on the bare container id (a texture-less holder layer the
    // compositor materializes) then evaluates the scene at the base frame.
    bool Render(Compositor& compositor, const std::string& id, std::string& error);
    // Point test over this player's installed shape layers (SDK `shape/...`
    // nodes). An empty label matches every shape; coordinates are stage pixels.
    bool Contains(Compositor& compositor, const std::string& id, const std::string& label,
                  double x, double y) const;
    // Per-frame tick: advance on the wall clock, then redraw. Returns false
    // when the scene rejected the frame; the player keeps ticking either way.
    // With auto progress disabled (createEmoteLayer progress=false, the
    // Windows/PS4/Switch script path) the clock only moves through explicit
    // Progress() calls; Step() is a clock-independent control operation.
    bool Update(double now_ms, Compositor* compositor, const std::string& id);
    void SetAutoProgress(bool on) { auto_progress_ = on; }
    bool AutoProgress() const { return auto_progress_; }
    void RemoveLayers(Compositor& compositor, const std::string& id);

private:
    struct Animated {
        struct Command { double value, duration_ms, ease; };
        double value = 0, start = 0, target = 0, duration_ms = 0, elapsed_ms = 0, weight = 1;
        std::deque<Command> queue;
        Animated() = default;
        // The initial value is also the Finish() target: an epSkip before
        // any Set must not snap the channel back to zero.
        explicit Animated(double v) : value(v), start(v), target(v) {}
        bool animating() const { return duration_ms > 0; }
        void Set(double v, double transition_ms, double ease, bool queuing = false);
        void Advance(double dt_ms);
        void Finish();
    };
    struct TrackRuntime {
        size_t cursor = 0;  // next authored command not yet submitted
        Animated difference;
    };
    struct PlayingTimeline {
        double position = 0;       // frames
        int flags = kTimelineParallel;
        Animated blend{1};         // fades animate this; direct sets cancel it
        bool fade_out_stop = false, hold_end = false, loop_crossed = false, passed = false;
        std::vector<TrackRuntime> tracks;
    };
    const EmoteTimeline* Timeline(const std::string& label) const;
    PlayingTimeline* EnsurePlaying(const std::string& label, int flags, double blend,
                                   std::string& error);
    void Forward(const EmoteTimeline& timeline, PlayingTimeline& playing, double frame, bool inclusive);
    void Cue(const EmoteTimeline& timeline, PlayingTimeline& playing, double frame);
    void AdvanceControls(double delta_ms);
    void FinishGlobalControls();
    // Single route for every BASE variable write (public setter, ordinary and
    // instant timeline Forward, Pass, orbit). Private flags-bit1 difference
    // tracks never come through here. `label` must be registered.
    void WriteBaseVariable(const std::string& label, double value, double transition_ms,
                           double ease);
    // Orbit: runs after the Progress clock advance, with the accepted delta.
    void ProgressOrbits(double delta_ms);
    double NextTrackEvent(const EmoteTrack& track, const TrackRuntime& state) const;
    bool ValidateTimeline(const std::string& label, std::string& error) const;
    bool Looping(const EmoteTimeline& timeline, bool hold_end) const;
    // Local numeric safety: checked accumulation/narrowing before overlays.
    // Failure leaves out untouched; Render must call before container mutation.
    bool ComposeVariables(std::map<std::string, double>& out, std::string& error) const;
    // metadata.eyeControl runtime: the native phase model is 40% closing, 20%
    // closed hold and 40% opening at 2.5x the nominal speed; waits never make
    // IsAnimating() true (a pending blink must not hold a scenario wait).
    struct BlinkRuntime {
        enum Phase { kIdle, kClosing, kClosedHold, kOpening };
        double wait = 0, amount = 0;  // remaining phase frames, normalized closure
        Phase phase = kIdle;
        uint32_t rng = 1;
    };
    void ScheduleBlink(const EmoteBlink& blink, BlinkRuntime& state);
    void AdvanceBlinks(double dt_ms);
    void ApplyBlinks(std::map<std::string, double>& out) const;
    // metadata.selectorControl: the selector label's current value picks an
    // option; items crossfade by min(|value-index|,1) (reference host formula).
    void ApplySelectors(std::map<std::string, double>& out) const;

    std::shared_ptr<const EmoteModel> model_;
    EmoteScene scene_;
    std::vector<std::pair<std::string, PlayingTimeline>> playing_;  // insertion order
    std::map<std::string, Animated> variables_;                     // model labels at 0
    std::map<std::pair<std::string, std::string>, Animated> variable_diffs_;
    std::vector<BlinkRuntime> blinks_;                              // parallel to model Blinks()
    struct OrbitRuntime {
        size_t config_index = 0;  // index in model Controls().Orbits()
        EmoteOrbitState state;    // reset to {} by Load
    };
    std::vector<OrbitRuntime> orbits_;
    std::set<std::string> immediate_labels_;
    Animated coord_x_, coord_y_, rot_;
    Animated scale_x_{1}, scale_y_{1}, alpha_{1}, grayscale_{0};
    uint32_t color_rgb_ = 0xFFFFFF;
    double base_frame_ = 0, last_now_ms_ = -1;
    // Native transform contract: translate(coord) * rotate(angle) *
    // scale * translate(-origin); origin/z are instant (non-animated).
    double z_ = 0, origin_x_ = 0, origin_y_ = 0;
    int layer_w_ = 0, layer_h_ = 0;
    double mesh_division_ratio_ = 1, hair_scale_ = 1, bust_scale_ = 1;
    bool auto_progress_ = true;
    bool queuing_ = false;
    bool metadata_mirror_ = false, user_mirror_ = false, hidden_ = false;
    // Variable label -> matches mirrorControl.variableMatchList (byte substring).
    bool MirrorMatches(const std::string& label) const;
    mutable std::map<std::string, bool> mirror_match_cache_;
};
}
