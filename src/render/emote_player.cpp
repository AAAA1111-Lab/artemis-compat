#include "render/emote_player.h"
#include "render/compositor.h"
#include "log/logger.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace artc {
namespace {
double EaseWeight(double ease) {
    // Same exponent EmoteModel::Sample applies to keyframe easing.
    return ease >= 0 ? ease + 1 : 1 / (1 - ease);
}
// Finite endpoints, convex p. An opposite-sign delta can overflow although
// every point on the segment is representable; weighted endpoints cannot.
// Same-sign subtraction is bounded. Endpoint clamping only absorbs roundoff,
// never turns an unrepresentable composition into a saturated render value.
double Convex(double a, double b, double p) {
    if (p <= 0) return a;
    if (p >= 1) return b;
    const double value = std::signbit(a) != std::signbit(b)
                             ? a * (1 - p) + b * p : a + (b - a) * p;
    return std::clamp(value, std::min(a, b), std::max(a, b));
}
bool FitsDouble(long double value) {
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<double>::max();
}
bool FitsFloat(long double value) {
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}
std::string Num(double v) {
    return std::to_string(v);
}
// ARTC_EMOTE_DUMP diagnostic: record driver commands so a game state can be
// replayed through tools/emote_preview.
void DumpCommand(const void* player, const std::string& text) {
    if (std::getenv("ARTC_EMOTE_DUMP"))
        Log(kLogInfo, "emote-cmd player=" + std::to_string(reinterpret_cast<uintptr_t>(player)) + " " + text);
}
}  // namespace

void EmotePlayer::Animated::Set(double v, double transition_ms, double ease, bool queuing) {
    if (!std::isfinite(v) || !std::isfinite(transition_ms) || !std::isfinite(ease)) return;
    if (transition_ms > 0 && queuing && animating()) {
        queue.push_back({v, transition_ms, ease});
        return;
    }
    queue.clear();
    if (!(transition_ms > 0)) {
        // Immediate sets update the target too: Skip()/Step() call Finish()
        // (value = target) and must not undo an instant placement.
        value = v;
        start = v;
        target = v;
        duration_ms = 0;
        elapsed_ms = 0;
        return;
    }
    start = value;
    target = v;
    duration_ms = transition_ms;
    elapsed_ms = 0;
    weight = EaseWeight(ease);
}

void EmotePlayer::Animated::Advance(double dt_ms) {
    // Consume elapsed time across command boundaries, never epSkip the queue
    // merely because its first command completed.
    while (duration_ms > 0) {
        const double remaining = duration_ms - elapsed_ms;
        // Frame<->ms conversions and partitioned additions can leave a few
        // duration-relative ULPs at an authored deadline. This is NOT a fixed
        // time epsilon (which would immediately finish very short commands).
        // Zero elapsed never completes a positive remaining duration.
        const bool before_deadline = dt_ms < remaining &&
            (dt_ms <= 0 || (remaining - dt_ms) / duration_ms >
                              8 * std::numeric_limits<double>::epsilon());
        if (before_deadline) {
            elapsed_ms += dt_ms;
            const double ratio = std::clamp(elapsed_ms / duration_ms, 0.0, 1.0);
            value = Convex(start, target, std::pow(ratio, weight));
            return;
        }
        dt_ms = std::max(0.0, dt_ms - remaining);
        value = target;
        duration_ms = elapsed_ms = 0;
        if (queue.empty()) return;
        const auto command = queue.front();
        queue.pop_front();
        start = value;
        target = command.value;
        duration_ms = command.duration_ms;
        weight = EaseWeight(command.ease);
    }
}

void EmotePlayer::Animated::Finish() {
    if (!queue.empty()) target = queue.back().value;
    queue.clear();
    value = target;
    duration_ms = 0;
    elapsed_ms = 0;
}

namespace {
// Frame that Step/Skip forward a nonloop to: lastTime, else the loop end, else
// (open-ended timeline) its last authored key.
double FinalFrame(const EmoteTimeline& timeline) {
    if (timeline.last_time >= 0) return timeline.last_time;
    if (timeline.loop_end >= 0) return timeline.loop_end;
    double last = 0;
    for (const auto& track : timeline.tracks)
        if (!track.frames.empty()) last = std::max(last, track.frames.back().time);
    return last;
}
}  // namespace

bool EmotePlayer::Load(std::shared_ptr<const EmoteModel> model, std::string& error) {
    if (!model) {
        error = "missing E-mote model";
        return false;
    }
    // clampControl (type 0 unestablished, type 1 pair projection unimplemented)
    // is ignored, never faked by a scalar clamp; EmoteModel records it in
    // UnsupportedFeatures() and Load still succeeds.
    EmotePlayer next;  // a failed Load must not leave a half-swapped player
    if (!next.scene_.Load(model, error)) return false;
    next.model_ = std::move(model);
    for (const auto& label : next.model_->Variables()) next.variables_[label] = Animated();
    const auto& plan = next.model_->ControlPlan();
    for (const auto& item : plan.additions) {
        next.variables_.emplace(item.label, Animated());
        if (item.kind == EmoteControlKind::Immediate) next.immediate_labels_.insert(item.label);
        else if (item.kind == EmoteControlKind::Orbit) next.orbits_.push_back({item.source_index, {}});
    }
    // Compatibility policy: a deterministic per-controller LCG, reset on Load.
    // This distribution/seed is not a claim about the official engine's RNG.
    const auto& blinks = next.model_->Blinks();
    next.blinks_.resize(blinks.size());
    for (size_t i = 0; i < blinks.size(); ++i) {
        next.blinks_[i].rng = 0x9E3779B9u * static_cast<uint32_t>(i + 1) | 1u;
        next.ScheduleBlink(blinks[i], next.blinks_[i]);
    }
    // metadata.mirror is the metadata state; the user request starts false.
    next.metadata_mirror_ = next.model_->Mirrored();
    next.user_mirror_ = false;
    *this = std::move(next);
    error.clear();
    return true;
}

EmoteUnsupportedFeatures EmotePlayer::UnsupportedFeatures() const {
    EmoteUnsupportedFeatures result;
    if (!model_) return result;
    result.Merge(model_->UnsupportedFeatures());
    result.Merge(scene_.UnsupportedFeatures());
    return result;
}

void EmotePlayer::ScheduleBlink(const EmoteBlink& blink, BlinkRuntime& state) {
    state.rng = state.rng * 1664525u + 1013904223u;
    const double unit = static_cast<double>(state.rng) / 4294967295.0;
    const double min = std::max(0.0, blink.interval_min);
    const double max = std::max(min, blink.interval_max);
    state.wait = min + (max - min) * unit;
}

void EmotePlayer::AdvanceBlinks(double dt_ms) {
    if (!model_) return;
    const double delta_frames = dt_ms * kFramesPerMillisecond;
    if (!(delta_frames > 0) || !std::isfinite(delta_frames)) return;
    const auto& blinks = model_->Blinks();
    for (size_t i = 0; i < blinks.size() && i < blinks_.size(); ++i) {
        const EmoteBlink& blink = blinks[i];
        BlinkRuntime& state = blinks_[i];
        if (!blink.enabled || !blink.blink_enabled) continue;
        double frames = delta_frames;  // controllers are parallel, not a shared budget
        const double arc = blink.frames * 0.4;
        // EmoteModel accepts only finite metadata and normalizes duration >=1
        // and intervals >=0. Progress caps each tick at 3600 frames: even with
        // a zero interval, each full cycle consumes >=1 frame, so this loop is
        // bounded by roughly 4*3600 transitions/controller. No fixed phase cap
        // may discard elapsed time (or skip RNG draws). Positive phase durations
        // also guarantee termination when the idle interval is zero. Measuring
        // phases in time avoids division by an empty/reversed variable span.
        while (frames > 0) {
            if (frames < state.wait) {
                state.wait -= frames;
                if (state.phase == BlinkRuntime::kClosing)
                    state.amount = std::clamp(1 - state.wait / arc, 0.0, 1.0);
                else if (state.phase == BlinkRuntime::kOpening)
                    state.amount = std::clamp(state.wait / arc, 0.0, 1.0);
                break;
            }
            frames -= state.wait;
            switch (state.phase) {
            case BlinkRuntime::kIdle:
                state.wait = arc;
                state.amount = 0;
                state.phase = BlinkRuntime::kClosing;
                break;
            case BlinkRuntime::kClosing:
                state.amount = 1;
                state.wait = blink.frames * 0.2;
                state.phase = BlinkRuntime::kClosedHold;
                break;
            case BlinkRuntime::kClosedHold:
                state.wait = arc;
                state.phase = BlinkRuntime::kOpening;
                break;
            case BlinkRuntime::kOpening:
                state.amount = 0;
                state.phase = BlinkRuntime::kIdle;
                ScheduleBlink(blink, state);
                break;
            }
        }
    }
}

void EmotePlayer::ApplyBlinks(std::map<std::string, double>& out) const {
    if (!model_) return;
    const auto& blinks = model_->Blinks();
    for (size_t i = 0; i < blinks.size() && i < blinks_.size(); ++i) {
        const EmoteBlink& blink = blinks[i];
        const BlinkRuntime& state = blinks_[i];
        if (state.phase == BlinkRuntime::kIdle) continue;
        const auto it = out.find(blink.variable);
        const double base = it == out.end() ? 0 : it->second;
        // Compare the small-span gate without an opposite-sign subtraction.
        const bool small_span = blink.end >= blink.begin
            ? blink.end <= blink.begin + 1e-9 : blink.begin <= blink.end + 1e-9;
        // Current local compatibility boundary, not verified official semantics:
        // a degenerate span or a base outside the inclusive endpoint range is
        // left untouched. Otherwise interpolate from that base toward end,
        // including reversed endpoints, without replacing the script variable.
        if (base < std::min(blink.begin, blink.end) ||
            base > std::max(blink.begin, blink.end) || small_span)
            continue;
        out[blink.variable] = Convex(base, blink.end, state.amount);
    }
}

double EmotePlayer::SelectorOptionValue(const EmoteSelector& selector, double value, size_t index) {
    if (selector.items.empty() || index >= selector.items.size()) return 0;
    const double clamped = std::clamp(value, 0.0, static_cast<double>(selector.items.size() - 1));
    const double distance = std::min(std::abs(clamped - static_cast<double>(index)), 1.0);
    const auto& item = selector.items[index];
    return Convex(item.on, item.off, distance);
}

void EmotePlayer::ApplySelectors(std::map<std::string, double>& out) const {
    if (!model_) return;
    for (const auto& selector : model_->Selectors()) {
        if (!selector.enabled || selector.items.empty()) continue;
        const auto it = out.find(selector.label);
        const double value = it == out.end() ? 0.0 : it->second;
        for (size_t i = 0; i < selector.items.size(); ++i)
            out[selector.items[i].label] = SelectorOptionValue(selector, value, i);
    }
}

const EmoteTimeline* EmotePlayer::Timeline(const std::string& label) const {
    if (!model_) return nullptr;
    const auto& timelines = model_->Timelines();
    const auto it = timelines.find(label);
    return it == timelines.end() ? nullptr : &it->second;
}

bool EmotePlayer::Looping(const EmoteTimeline& timeline, bool hold_end) const {
    return !hold_end && timeline.loop_begin >= 0 && timeline.loop_end > timeline.loop_begin;
}

bool EmotePlayer::ValidateTimeline(const std::string& label, std::string& error) const {
    const auto* timeline = Timeline(label);
    if (!timeline) {
        error = "unknown E-mote timeline: " + label;
        return false;
    }
    // A zero-width loop has no evidenced Forward/Cue behavior and stays rejected.
    // An open-ended nonloop (no lastTime, no loop; every shipped expression
    // timeline) is NOT rejected: it plays until stopped, is recorded by the
    // model as "timeline.openEnded", and Step/Skip forward it to its last key.
    if (timeline->loop_begin >= 0 && timeline->loop_end <= timeline->loop_begin) {
        error = "unsupported E-mote timeline end/loop range: " + label;
        return false;
    }
    for (const auto& track : timeline->tracks)
        if (!variables_.count(track.variable)) {
            error = "unknown E-mote variable: " + track.variable;
            return false;
        }
    return true;
}

EmotePlayer::PlayingTimeline* EmotePlayer::EnsurePlaying(const std::string& label, int flags,
                                                         double blend, std::string& error) {
    if (!model_) {
        error = "E-mote player has no model";
        return nullptr;
    }
    if (!ValidateTimeline(label, error)) return nullptr;
    PlayingTimeline fresh;
    fresh.flags = flags;
    fresh.blend = Animated(blend);
    fresh.tracks.resize(Timeline(label)->tracks.size());
    for (auto& entry : playing_) {
        if (entry.first != label) continue;
        entry.second = std::move(fresh);
        Forward(*Timeline(label), entry.second, 0, true);
        error.clear();
        return &entry.second;
    }
    playing_.push_back({label, std::move(fresh)});
    auto& entry = playing_.back().second;
    Forward(*Timeline(label), entry, 0, true);
    error.clear();
    return &entry;
}

bool EmotePlayer::PlayTimeline(const std::string& label, int flags, std::string& error) {
    if (!model_) {
        error = "E-mote player has no model";
        return false;
    }
    // Validate before bit0's destructive operation. Android proxy forwards
    // official flags unchanged: bit1 is private diff, NOT a timeline FIFO.
    if (!ValidateTimeline(label, error)) return false;
    DumpCommand(this, "play '" + label + "' flags=" + std::to_string(flags));
    if (!(flags & kTimelineParallel)) StopAllTimelines();
    return EnsurePlaying(label, flags, 1.0, error) != nullptr;
}

void EmotePlayer::StopTimeline(const std::string& label) {
    DumpCommand(this, "stop '" + label + "'");
    if (label.empty()) { StopAllTimelines(); return; }
    for (auto it = playing_.begin(); it != playing_.end();) {
        if (it->first == label)
            it = playing_.erase(it);
        else
            ++it;
    }
}

void EmotePlayer::StopAllTimelines() {
    playing_.clear();
}

bool EmotePlayer::IsTimelinePlaying(const std::string& label) const {
    if (label.empty()) return !playing_.empty();
    for (const auto& entry : playing_)
        if (entry.first == label) return true;
    return false;
}

bool EmotePlayer::IsLoopTimeline(const std::string& label) const {
    const auto* timeline = Timeline(label);
    return timeline && Looping(*timeline, false);
}

double EmotePlayer::TimelineTotalFrames(const std::string& label) const {
    const auto* timeline = Timeline(label);
    if (!timeline) return -1;
    return timeline->last_time >= 0 ? timeline->last_time : timeline->loop_end;
}

bool EmotePlayer::FadeInTimeline(const std::string& label, double duration_ms, double ease,
                                 std::string& error) {
    if (!std::isfinite(duration_ms) || !std::isfinite(ease)) {
        error = "E-mote fade expects finite duration and ease";
        return false;
    }
    DumpCommand(this, "fadeIn '" + label + "' ms=" + Num(duration_ms));
    auto* entry = EnsurePlaying(label, kTimelineParallel | kTimelinePrivateDiff, 0.0, error);
    if (!entry) return false;
    entry->blend.Set(1.0, duration_ms, ease, queuing_);
    error.clear();
    return true;
}

bool EmotePlayer::FadeOutTimeline(const std::string& label, double duration_ms, double ease,
                                  std::string& error) {
    if (!std::isfinite(duration_ms) || !std::isfinite(ease)) {
        error = "E-mote fade expects finite duration and ease";
        return false;
    }
    DumpCommand(this, "fadeOut '" + label + "' ms=" + Num(duration_ms));
    for (auto& entry : playing_) {
        if (entry.first != label) continue;
        entry.second.blend.Set(0.0, duration_ms, ease, queuing_);
        entry.second.fade_out_stop = true;
        error.clear();
        return true;
    }
    error = "E-mote timeline not playing: " + label;
    return false;
}

bool EmotePlayer::SetTimelineBlendRatio(const std::string& label, double ratio,
                                         std::string& error) {
    return SetTimelineBlendRatio(label, ratio, 0, 0, false, error);
}

bool EmotePlayer::SetTimelineBlendRatio(const std::string& label, double ratio,
                                        double transition_ms, double ease, bool fade_out_stop,
                                        std::string& error) {
    if (!std::isfinite(ratio) || !std::isfinite(transition_ms) || !std::isfinite(ease)) {
        error = "E-mote blend expects finite ratio, transition and ease";
        return false;
    }
    DumpCommand(this, "blend '" + label + "' ratio=" + Num(ratio) + " ms=" + Num(transition_ms));
    for (auto& entry : playing_) {
        if (entry.first != label) continue;
        entry.second.blend.Set(std::clamp(ratio, 0.0, 1.0), transition_ms, ease, queuing_);
        entry.second.fade_out_stop = fade_out_stop;
        error.clear();
        return true;
    }
    error = "E-mote timeline not playing: " + label;
    return false;
}

double EmotePlayer::TimelineBlendRatio(const std::string& label, bool* found) const {
    for (const auto& entry : playing_) {
        if (entry.first != label) continue;
        if (found) *found = true;
        return entry.second.blend.value;
    }
    if (found) *found = false;
    return 0;
}

bool EmotePlayer::SetTimelineHoldEnd(const std::string& label, bool loop, std::string& error) {
    for (auto& entry : playing_) {
        if (entry.first != label) continue;
        entry.second.hold_end = !loop;
        error.clear();
        return true;
    }
    error = "E-mote timeline not playing: " + label;
    return false;
}

int EmotePlayer::CountMainTimelines() const {
    if (!model_) return 0;
    int count = 0;
    for (const auto& t : model_->Timelines())
        if (!t.second.difference) ++count;
    return count;
}

std::string EmotePlayer::MainTimelineLabelAt(int index) const {
    if (!model_ || index < 0) return {};
    int i = 0;
    for (const auto& t : model_->Timelines())
        if (!t.second.difference && i++ == index) return t.first;
    return {};
}

int EmotePlayer::CountDiffTimelines() const {
    if (!model_) return 0;
    int count = 0;
    for (const auto& t : model_->Timelines())
        if (t.second.difference) ++count;
    return count;
}

std::string EmotePlayer::DiffTimelineLabelAt(int index) const {
    if (!model_ || index < 0) return {};
    int i = 0;
    for (const auto& t : model_->Timelines())
        if (t.second.difference && i++ == index) return t.first;
    return {};
}

int EmotePlayer::CountPlayingTimelines() const { return static_cast<int>(playing_.size()); }

std::string EmotePlayer::PlayingTimelineLabelAt(int index) const {
    if (index < 0 || index >= static_cast<int>(playing_.size())) return {};
    return playing_[index].first;
}

int EmotePlayer::PlayingTimelineFlagsAt(int index) const {
    if (index < 0 || index >= static_cast<int>(playing_.size())) return 0;
    // Preserve the existing stored-flags getter in this repair. Native getter
    // normalization (stored | 1) is a separate documented boundary, not a
    // reason to reinstate bit1's former sequential-queue interpretation.
    return playing_[index].second.flags;
}

int EmotePlayer::CountVariables() const {
    if (!model_) return 0;
    return static_cast<int>(model_->Variables().size());
}

std::string EmotePlayer::VariableLabelAt(int index) const {
    if (!model_ || index < 0) return {};
    int i = 0;
    for (const auto& label : model_->Variables())
        if (i++ == index) return label;
    return {};
}

bool EmotePlayer::SetVariable(const std::string& label, double value, double transition_ms,
                              double ease, std::string& error) {
    const auto it = variables_.find(label);
    if (it == variables_.end()) {
        error = "unknown E-mote variable: " + label;
        return false;
    }
    if (!std::isfinite(value) || !std::isfinite(transition_ms) || !std::isfinite(ease)) {
        error = "E-mote variable expects finite value, transition and ease";
        return false;
    }
    DumpCommand(this, "set '" + label + "' = " + Num(value) + " ms=" + Num(transition_ms));
    WriteBaseVariable(label, value, transition_ms, ease);
    error.clear();
    return true;
}

void EmotePlayer::WriteBaseVariable(const std::string& label, double value,
                                    double transition_ms, double ease) {
    Animated& control = variables_.at(label);
    if (immediate_labels_.count(label)) control.Set(value, 0, 0);  // no duration, no queue
    else control.Set(value, transition_ms, ease, queuing_);
}

double EmotePlayer::GetVariable(const std::string& label, bool* found) const {
    const auto it = variables_.find(label);
    if (it == variables_.end()) {
        if (found) *found = false;
        return 0;
    }
    if (found) *found = true;
    return it->second.value;
}

bool EmotePlayer::SetVariableDiff(const std::string& label, const std::string& pair,
                                  double value, double transition_ms, double ease,
                                  std::string& error) {
    if (!variables_.count(label)) {
        error = "unknown E-mote variable: " + label;
        return false;
    }
    if (!variables_.count(pair)) {
        error = "unknown E-mote variable: " + pair;
        return false;
    }
    // Validate before insertion/replacement so rejected input cannot create a
    // control or cancel an existing transition. Keep the existing label range;
    // mirrorControl.variableMatchList is NOT a pair admission whitelist.
    if (!std::isfinite(value) || !std::isfinite(transition_ms) || !std::isfinite(ease)) {
        error = "E-mote variable diff expects finite value, transition and ease";
        return false;
    }
    DumpCommand(this, "diff '" + label + "' '" + pair + "' = " + Num(value) + " ms=" + Num(transition_ms));
    variable_diffs_[{label, pair}].Set(value, transition_ms, ease, queuing_);
    error.clear();
    return true;
}

double EmotePlayer::GetVariableDiff(const std::string& label, const std::string& pair,
                                    bool* found) const {
    const auto it = variable_diffs_.find({label, pair});
    if (found) *found = it != variable_diffs_.end();
    return it == variable_diffs_.end() ? 0 : it->second.value;
}

void EmotePlayer::SetCoord(double x, double y, double transition_ms, double ease) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(transition_ms) || !std::isfinite(ease)) return;
    coord_x_.Set(x, transition_ms, ease, queuing_);
    coord_y_.Set(y, transition_ms, ease, queuing_);
}

void EmotePlayer::SetCoordAngle(double x, double y, double z, double angle) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(angle)) return;
    // Artemis setCoord(x, y, z, angle): an instant placement; z is depth and
    // the compositor has no 3D stage.
    z_ = z;
    coord_x_.Set(x, 0, 0);
    coord_y_.Set(y, 0, 0);
    rot_.Set(angle, 0, 0);
}

void EmotePlayer::SetScaleOrigin(double scale, double origin_x, double origin_y) {
    if (!std::isfinite(scale) || !std::isfinite(origin_x) || !std::isfinite(origin_y)) return;
    // Artemis setScale(scale, origin_x, origin_y): uniform scale around a
    // model-space pivot (folded into the container position at render time).
    origin_x_ = origin_x;
    origin_y_ = origin_y;
    scale_x_.Set(scale, 0, 0);
    scale_y_.Set(scale, 0, 0);
}

void EmotePlayer::SetScale(double sx, double sy, double transition_ms, double ease) {
    if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(transition_ms) || !std::isfinite(ease)) return;
    origin_x_ = 0;
    origin_y_ = 0;
    scale_x_.Set(sx, transition_ms, ease, queuing_);
    scale_y_.Set(sy, transition_ms, ease, queuing_);
}

void EmotePlayer::SetRot(double degrees, double transition_ms, double ease) {
    rot_.Set(degrees, transition_ms, ease, queuing_);
}

void EmotePlayer::SetColor(uint32_t rrggbbaa, double transition_ms, double ease) {
    if (!std::isfinite(transition_ms) || !std::isfinite(ease)) return;
    // Artemis scripts build 0xRRGGBBff; the native driver's neutral is
    // 0x808080FF (gray, opaque), so alpha is the low byte.
    color_rgb_ = (rrggbbaa >> 8) & 0xFFFFFFu;
    alpha_.Set((rrggbbaa & 0xFFu) / 255.0, transition_ms, ease, queuing_);
}

uint32_t EmotePlayer::GetColor() const {
    const int a = std::clamp(static_cast<int>(std::lround(alpha_.value * 255)), 0, 255);
    return (color_rgb_ << 8) | static_cast<uint32_t>(a);
}

void EmotePlayer::SetGrayscale(double value, double transition_ms, double ease) {
    if (!std::isfinite(value)) return;
    grayscale_.Set(std::clamp(value, 0.0, 1.0), transition_ms, ease, queuing_);
}

void EmotePlayer::SetMeshDivisionRatio(double ratio) {
    if (!(ratio > 0) || !std::isfinite(ratio)) return;
    mesh_division_ratio_ = std::clamp(ratio, 0.05, 1.0);
    static bool logged = false;
    if (!logged) {
        logged = true;
        Log(kLogInfo, "emote: mesh division ratio controls rendered warp subdivision");
    }
}

void EmotePlayer::SetHairScale(double scale) {
    if (!std::isfinite(scale)) return;
    hair_scale_ = std::max(0.0, scale);
    static bool logged = false;
    if (!logged) {
        logged = true;
        Log(kLogInfo, "emote: hair sway scale recorded; physics is not simulated");
    }
}

void EmotePlayer::SetBustScale(double scale) {
    if (!std::isfinite(scale)) return;
    bust_scale_ = std::max(0.0, scale);
    static bool logged = false;
    if (!logged) {
        logged = true;
        Log(kLogInfo, "emote: bust sway scale recorded; physics is not simulated");
    }
}

void EmotePlayer::GetCoord(double* x, double* y) const {
    if (x) *x = coord_x_.value;
    if (y) *y = coord_y_.value;
}

void EmotePlayer::GetScale(double* x, double* y) const {
    if (x) *x = scale_x_.value;
    if (y) *y = scale_y_.value;
}

double EmotePlayer::GetRot() const { return rot_.value; }

bool EmotePlayer::MirrorMatches(const std::string& label) const {
    const auto cached = mirror_match_cache_.find(label);
    if (cached != mirror_match_cache_.end()) return cached->second;
    // Case-sensitive raw-byte substring: the label contains a list item; an
    // empty item matches every label; an empty list matches nothing.
    bool matched = false;
    for (const auto& item : model_->MirrorVariableMatches())
        if (label.find(item) != std::string::npos) { matched = true; break; }
    mirror_match_cache_.emplace(label, matched);
    return matched;
}

void EmotePlayer::Show() { hidden_ = false; }

void EmotePlayer::Hide() { hidden_ = true; }

double EmotePlayer::NextTrackEvent(const EmoteTrack& track, const TrackRuntime& state) const {
    return state.cursor < track.frames.size() ? track.frames[state.cursor].time
                                              : std::numeric_limits<double>::infinity();
}

void EmotePlayer::Cue(const EmoteTimeline& timeline, PlayingTimeline& playing, double frame) {
    for (size_t i = 0; i < timeline.tracks.size(); ++i) {
        const auto& keys = timeline.tracks[i].frames;
        auto& cursor = playing.tracks[i].cursor;
        cursor = 0;
        while (cursor < keys.size() && keys[cursor].time < frame) ++cursor;
    }
    playing.position = frame;
}

void EmotePlayer::Forward(const EmoteTimeline& timeline, PlayingTimeline& playing,
                          double frame, bool inclusive) {
    // Per-track cursors separate authored commands from continuously sampled
    // output. Ordinary non-instant segments command the next endpoint over its
    // time interval; instant tracks only submit keys actually crossed. Private
    // bit1 channels use exactly the same segments but never mutate base getters.
    for (size_t i = 0; i < timeline.tracks.size(); ++i) {
        const auto& track = timeline.tracks[i];
        auto& state = playing.tracks[i];
        // Only the private bit1 difference channel bypasses WriteBaseVariable.
        const bool private_diff = (playing.flags & kTimelinePrivateDiff) && !track.instant;
        const auto write = [&](double value, double duration_ms, double ease) {
            if (private_diff) state.difference.Set(value, duration_ms, ease, queuing_);
            else WriteBaseVariable(track.variable, value, duration_ms, ease);
        };
        while (state.cursor < track.frames.size()) {
            const auto& key = track.frames[state.cursor];
            if (key.time > frame || (!inclusive && key.time == frame)) break;
            const bool first = state.cursor == 0;
            ++state.cursor;
            if (key.terminal) continue;
            // Subsequent non-instant endpoints were already commanded by the
            // preceding segment. Re-setting them with duration0 would erase an
            // unrelated script command queue at every key boundary. Instant
            // tracks, and an initial/cued first key, really are immediate sets.
            if (track.instant || first) write(key.value, 0, 0);
            if (!track.instant && state.cursor < track.frames.size()) {
                const auto& next = track.frames[state.cursor];
                if (!next.terminal && next.time > key.time)
                    write(next.value, (next.time - key.time) / kFramesPerMillisecond, next.easing);
            }
        }
    }
    playing.position = frame;
}

void EmotePlayer::AdvanceControls(double delta_ms) {
    coord_x_.Advance(delta_ms);
    coord_y_.Advance(delta_ms);
    rot_.Advance(delta_ms);
    scale_x_.Advance(delta_ms);
    scale_y_.Advance(delta_ms);
    alpha_.Advance(delta_ms);
    grayscale_.Advance(delta_ms);
    for (auto& v : variables_) v.second.Advance(delta_ms);
    for (auto& v : variable_diffs_) v.second.Advance(delta_ms);
    for (auto& entry : playing_) {
        entry.second.blend.Advance(delta_ms);
        for (auto& track : entry.second.tracks) track.difference.Advance(delta_ms);
    }
}

void EmotePlayer::Progress(double delta_ms) {
    if (!model_ || !std::isfinite(delta_ms)) return;
    delta_ms = std::clamp(delta_ms, 0.0, kMaxProgressMs);
    base_frame_ += delta_ms * kFramesPerMillisecond;
    AdvanceBlinks(delta_ms);
    // Advance all controls on a common clock, splitting only at authored key
    // and loop/end boundaries. This preserves large/small tick equivalence and
    // lets ordinary timeline setters be visible through GetVariable(base).
    double remaining_ms = delta_ms;
    double remaining = remaining_ms * kFramesPerMillisecond;
    do {
        double step = remaining;
        for (const auto& item : playing_) {
            const auto& timeline = *Timeline(item.first);
            const auto& entry = item.second;
            if (entry.fade_out_stop && entry.blend.animating()) {
                double fade_ms = entry.blend.duration_ms - entry.blend.elapsed_ms;
                for (const auto& command : entry.blend.queue) fade_ms += command.duration_ms;
                const double final = entry.blend.queue.empty() ? entry.blend.target : entry.blend.queue.back().value;
                if (final <= 0) step = std::min(step, fade_ms * kFramesPerMillisecond);
            }
            const double end = Looping(timeline, entry.hold_end) ? timeline.loop_end
                                : (timeline.last_time >= 0 ? timeline.last_time : timeline.loop_end);
            if (end >= entry.position && (!entry.passed || end > entry.position))
                step = std::min(step, end - entry.position);
            for (size_t i = 0; i < timeline.tracks.size(); ++i) {
                const double event = NextTrackEvent(timeline.tracks[i], entry.tracks[i]);
                if (end < 0 || event <= end)
                    step = std::min(step, std::max(0.0, event - entry.position));
            }
        }
        // Preserve the original millisecond remainder when no boundary split
        // is needed: a smallest-subnormal duration must not become zero in the
        // ms->frames->ms roundtrip. A real split still uses its frame boundary.
        const double step_ms = step == remaining ? remaining_ms : step / kFramesPerMillisecond;
        AdvanceControls(step_ms);
        remaining_ms = std::max(0.0, remaining_ms - step_ms);
        remaining = remaining_ms * kFramesPerMillisecond;
        for (auto it = playing_.begin(); it != playing_.end();) {
            auto& entry = it->second;
            const auto& timeline = *Timeline(it->first);
            if (entry.fade_out_stop && !entry.blend.animating() && entry.blend.value <= 0) {
                it = playing_.erase(it);
                continue;
            }
            {
                const bool looping = Looping(timeline, entry.hold_end);
                const double end = looping ? timeline.loop_end
                                    : (timeline.last_time >= 0 ? timeline.last_time : timeline.loop_end);
                const double next = entry.position + step;
                if (end >= 0 && next >= end) {
                    Forward(timeline, entry, end, !looping);
                    if (!looping) {
                        // Pass's bit2 is a repeat guard, not a frozen timeline
                        // clock: private controls keep receiving keys while the
                        // 20-frame fade runs, then hold at end until it removes.
                        if (!entry.passed) { it = playing_.erase(it); continue; }
                    } else {
                        Cue(timeline, entry, timeline.loop_begin);
                        Forward(timeline, entry, timeline.loop_begin, true);
                        entry.loop_crossed = true;
                    }
                } else Forward(timeline, entry, next, true);
            }
            ++it;
        }
    } while (remaining > 0);
    ProgressOrbits(delta_ms);
}

void EmotePlayer::ProgressOrbits(double delta_ms) {
    const auto& orbits = model_->Controls().Orbits();
    const double delta_frames = delta_ms * kFramesPerMillisecond;
    for (auto& runtime : orbits_) {
        std::optional<EmoteOrbitCommand> command;
        std::string error;
        // Failure leaves the orbit state unchanged and writes nothing.
        if (!ProgressEmoteOrbit(orbits[runtime.config_index], delta_frames, runtime.state,
                                command, error) || !command)
            continue;
        WriteBaseVariable(orbits[runtime.config_index].label, command->value,
                          command->duration_frames / kFramesPerMillisecond, 0);
    }
}

bool EmotePlayer::IsAnimating() const {
    if (coord_x_.animating() || coord_y_.animating() || rot_.animating() ||
        scale_x_.animating() || scale_y_.animating() || alpha_.animating() ||
        grayscale_.animating())
        return true;
    for (const auto& v : variables_)
        if (v.second.animating()) return true;
    for (const auto& v : variable_diffs_)
        if (v.second.animating()) return true;
    return !playing_.empty();
}

void EmotePlayer::SkipToSync() {
    if (!model_) return;
    const auto& base = model_->Document().root.At("metadata").At("base");
    const double sync = model_->SyncFrame(base.At("chara").string, base.At("motion").string);
    if (sync >= 0) base_frame_ = sync;
}

void EmotePlayer::FinishGlobalControls() {
    coord_x_.Finish();
    coord_y_.Finish();
    rot_.Finish();
    scale_x_.Finish();
    scale_y_.Finish();
    alpha_.Finish();
    grayscale_.Finish();
    for (auto& v : variables_) v.second.Finish();
    for (auto& v : variable_diffs_) v.second.Finish();
}

void EmotePlayer::Step() {
    // Android epSkip is a control operation, not a wall-clock frame tick.
    for (auto it = playing_.begin(); it != playing_.end();) {
        auto& entry = it->second;
        const auto& timeline = *Timeline(it->first);
        if (timeline.loop_begin < 0 || entry.hold_end || entry.fade_out_stop) {
            Forward(timeline, entry, FinalFrame(timeline), true);
            it = playing_.erase(it);
            continue;
        }
        if (!entry.loop_crossed) {
            const double elapsed = std::max(0.0, timeline.loop_end - entry.position);
            Forward(timeline, entry, timeline.loop_end, false);
            Cue(timeline, entry, timeline.loop_begin);
            Forward(timeline, entry, timeline.loop_begin, true);
            if (entry.flags & kTimelinePrivateDiff)
                for (auto& track : entry.tracks) track.difference.Advance(elapsed / kFramesPerMillisecond);
            entry.loop_crossed = true;
        }
        entry.blend.Finish();
        ++it;
    }
    FinishGlobalControls();
}

void EmotePlayer::Skip() {
    for (auto it = playing_.begin(); it != playing_.end();) {
        auto& entry = it->second;
        const auto& timeline = *Timeline(it->first);
        if (timeline.loop_begin < 0 || entry.hold_end) {
            Forward(timeline, entry, FinalFrame(timeline), true);
            it = playing_.erase(it);
            continue;
        }
        entry.blend.Finish();
        ++it;
    }
    FinishGlobalControls();
}

void EmotePlayer::Pass() {
    // Official Android normal path: only finite/nonloop playback is passed.
    // bit1 keeps non-instant private controllers alive under a single 20-frame
    // stop fade; ordinary/instant future records become queued setter commands.
    // This is deliberately not FinishGlobalControls(), nor a 20-millisecond
    // approximation. Keep the public blend flags' existing boolean contract
    // separate from internal passed/fade-out bookkeeping.
    for (auto it = playing_.begin(); it != playing_.end();) {
        auto& entry = it->second;
        const auto& timeline = *Timeline(it->first);
        if (timeline.loop_begin >= 0 || entry.passed) { ++it; continue; }
        for (size_t i = 0; i < timeline.tracks.size(); ++i) {
            const auto& track = timeline.tracks[i];
            if ((entry.flags & kTimelinePrivateDiff) && !track.instant) continue;
            for (size_t k = entry.tracks[i].cursor; k < track.frames.size(); ++k) {
                const auto& key = track.frames[k];
                if (!key.terminal)
                    WriteBaseVariable(track.variable, key.value, key.time / kFramesPerMillisecond,
                                      key.easing);
            }
            entry.tracks[i].cursor = track.frames.size();
        }
        if (entry.flags & kTimelinePrivateDiff) {
            entry.passed = true;
            entry.blend.Set(0, 20.0 / kFramesPerMillisecond, 0, queuing_);
            entry.fade_out_stop = true;
            ++it;
        } else it = playing_.erase(it);
    }
}

bool EmotePlayer::ComposeVariables(std::map<std::string, double>& out, std::string& error) const {
    // Local safety policy, not official exceptional-input semantics. Accumulate
    // before narrowing, permitting cancellation where this platform's long
    // double can represent the intermediates. Some platforms give it no wider
    // exponent than double: reject any nonfinite intermediate there, rather
    // than promising arbitrary huge cancellation, saturation, or a silent zero.
    std::map<std::string, long double> accumulated;
    const auto add = [&](const std::string& label, long double value) {
        if (!std::isfinite(value)) {
            error = "non-finite E-mote variable composition: " + label;
            return false;
        }
        auto& sum = accumulated[label];
        sum += value;
        if (!std::isfinite(sum)) {
            error = "unrepresentable E-mote variable intermediate: " + label;
            return false;
        }
        return true;
    };
    for (const auto& v : variables_)
        if (!add(v.first, v.second.value)) return false;
    for (const auto& entry : playing_) {
        const auto* timeline = Timeline(entry.first);
        if (!timeline || !(entry.second.flags & kTimelinePrivateDiff)) continue;
        for (size_t i = 0; i < timeline->tracks.size(); ++i)
            if (!timeline->tracks[i].instant &&
                !add(timeline->tracks[i].variable,
                     static_cast<long double>(entry.second.tracks[i].difference.value) * entry.second.blend.value))
                return false;
    }
    // Ordinary E2 path: V_b = timeline-composed base_b + sum_a C_(a,b).
    // Do not infer a +v/-v relation or clamp negative controls. Special
    // selector/blink collisions are not established by that ordinary path:
    // preserve the existing local override order below rather than claiming
    // native precedence for those cases.
    for (const auto& v : variable_diffs_)
        if (!add(v.first.second, v.second.value)) return false;
    std::map<std::string, double> next;
    for (const auto& v : accumulated) {
        if (!FitsDouble(v.second)) {
            error = "unrepresentable E-mote composed variable: " + v.first;
            return false;
        }
        double narrowed = static_cast<double>(v.second);
        if (v.second != 0 && narrowed == 0) {
            error = "underflow narrowing E-mote composed variable: " + v.first;
            return false;
        }
        // Mirror sign: negate the WHOLE composed value once (base + private
        // diffs + pair), only while effective mirror is on. Getters are unchanged.
        if (EffectiveMirror() && MirrorMatches(v.first)) narrowed = 0.0 - narrowed;
        next[v.first] = narrowed;
    }
    // Blink/selector override order is the existing local policy, unproven
    // against the official engine (本地策略未证).
    ApplyBlinks(next);
    // Selector controls resolve after the variable map is merged, overriding
    // any timeline writes to their option variables.
    ApplySelectors(next);
    for (const auto& v : next) {
        if (!std::isfinite(v.second)) {
            error = "non-finite E-mote variable overlay: " + v.first;
            return false;
        }
    }
    out = std::move(next);
    error.clear();
    return true;
}

bool EmotePlayer::Render(Compositor& compositor, const std::string& id, std::string& error) {
    if (!model_) {
        error = "E-mote player has no model";
        return false;
    }
    if (id.empty()) {
        error = "missing E-mote layer id";
        return false;
    }
    std::map<std::string, double> variables;
    if (!ComposeVariables(variables, error)) return false;
    if (std::getenv("ARTC_EMOTE_DUMP")) {
        // Diagnostic: log the composed state handed to the scene whenever it changes.
        std::string state = "mirror=" + std::string(EffectiveMirror() ? "1" : "0") + " playing=[";
        for (const auto& p : playing_) state += p.first + (p.second.flags & kTimelinePrivateDiff ? "(diff) " : " ");
        state += "] vars={";
        for (const auto& v : variables)
            if (v.second != 0) state += v.first + "=" + Num(v.second) + " ";
        state += "}";
        static std::map<const EmotePlayer*, std::string> last_state;
        auto& last = last_state[this];
        if (last != state) {
            last = state;
            Log(kLogInfo, "emote-state " + id + " " + state);
        }
    }
    // SetProps parses these values as float (rotation even falls back to zero
    // on parse failure). Validate raw controls AND derived container properties
    // before touching the holder, instead of letting stof throw/misrender.
    for (double value : {coord_x_.value, coord_y_.value, rot_.value, scale_x_.value,
                         scale_y_.value, origin_x_, origin_y_}) {
        if (!FitsFloat(value)) {
            error = "E-mote container control exceeds finite float range";
            return false;
        }
    }
    // The bare id materializes a texture-less holder layer; scene children are
    // keyed id.<node> and inherit this transform through the dotted-id chain.
    // Native order is translate(coord) * rotate * scale * translate(-origin):
    // the pivot shift is folded into the container position.
    const double radians = rot_.value * (3.14159265358979323846 / 180.0);
    const double cosr = std::cos(radians), sinr = std::sin(radians);
    const long double pivot_x = static_cast<long double>(scale_x_.value) * cosr * origin_x_ -
                                static_cast<long double>(scale_y_.value) * sinr * origin_y_;
    const long double pivot_y = static_cast<long double>(scale_x_.value) * sinr * origin_x_ +
                                static_cast<long double>(scale_y_.value) * cosr * origin_y_;
    // createEmoteLayer's width/height box centres the model origin; setCoord
    // then moves the model inside that box (native model_origin transform).
    const double center_x = layer_w_ * 0.5, center_y = layer_h_ * 0.5;
    const long double left = center_x + static_cast<long double>(coord_x_.value) - pivot_x;
    const long double top = center_y + static_cast<long double>(coord_y_.value) - pivot_y;
    const long double xscale = static_cast<long double>(scale_x_.value) * 100;
    const long double yscale = static_cast<long double>(scale_y_.value) * 100;
    if (!FitsFloat(left) || !FitsFloat(top) || !FitsFloat(xscale) || !FitsFloat(yscale)) {
        error = "E-mote container transform exceeds finite float range";
        return false;
    }
    std::map<std::string, std::string> props{{"left", Num(static_cast<double>(left))},
                                             {"top", Num(static_cast<double>(top))},
                                             {"rotate", Num(rot_.value)},
                                             {"xscale", Num(static_cast<double>(xscale))},
                                             {"yscale", Num(static_cast<double>(yscale))},
                                             {"alpha", Num(std::lround(alpha_.value * 255))},
                                             {"visible", hidden_ ? "0" : "1"},
                                             {"reversex", EffectiveMirror() ? "1" : "0"}};
    compositor.SetProps(id, props);
    // E-mote SetColor tints are MODULATE2X: 0x80 is the neutral channel.
    const auto doubled=[&](int shift) {
        return std::min(0xFFu,((color_rgb_>>shift)&0xFFu)*2u);
    };
    const uint32_t multiply=(doubled(16)<<16)|(doubled(8)<<8)|doubled(0);
    return scene_.Render(compositor, id, base_frame_, variables, error,
                         grayscale_.value, multiply, mesh_division_ratio_);
}

bool EmotePlayer::Update(double now_ms, Compositor* compositor, const std::string& id) {
    double dt = 0;
    if (last_now_ms_ >= 0) {
        dt = now_ms - last_now_ms_;
        if (!(dt > 0)) dt = 0;  // paused frame or clock reset
    }
    last_now_ms_ = now_ms;
    // progress=false layers are driven by explicit progress()/step() calls.
    if (auto_progress_) Progress(dt);
    if (!compositor) return true;
    std::string error;
    if (!Render(*compositor, id, error)) return false;
    return true;
}

bool EmotePlayer::Contains(Compositor& compositor, const std::string& id,
                           const std::string& label, double x, double y) const {
    return scene_.HitTest(compositor, id, label, x, y);
}

void EmotePlayer::RemoveLayers(Compositor& compositor, const std::string& id) {
    scene_.Remove(compositor, id);
}
}
