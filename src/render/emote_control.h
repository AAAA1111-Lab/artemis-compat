// E10 configuration/registration and normal-sample orbit helpers. No player,
// Animated, Lua registration, clip scope, queueing or controller precedence.
#pragma once

#include "pack/psb.h"
#include <cstddef>
#include <map>
#include <optional>
#include <set>

namespace artc {
// Local safety limits, not claims about official format limits. They bound only
// the metadata subtrees this configuration actually reads (transitionControl,
// immediateControl, orbitControl, instantVariableList, charaProfile). Other
// metadata, including unknown controls, is neither walked, counted nor copied:
// real models carry megabytes of unrelated metadata.
constexpr size_t kEmoteControlMaxEntries = 4096;
constexpr size_t kEmoteControlMaxRawNodes = 65536;
constexpr size_t kEmoteControlMaxRawBytes = 4 * 1024 * 1024;
constexpr size_t kEmoteControlMaxDepth = 64;

struct EmoteNamedControl {
    std::string label;
    bool enabled = true;
};
struct EmoteOrbitControl : EmoteNamedControl {
    double period_frames = 0; // authored interval is the WHOLE period
    bool tween = false;
    std::vector<double> values; // numeric orbitFrameList, equally spaced
};

class EmoteControlConfig {
public:
    // Takes metadata itself, NOT the PSB root. Atomic replace; failed reload
    // keeps every previous field. Missing metadata may be Null; present null
    // control fields are invalid, notably immediateControl never defaults then.
    // Strict finite numeric/flag shapes, duplicate labels (even disabled),
    // absent enabled => true are local safety policies. Orbit interval/tween
    // are REQUIRED: their official absent-field defaults are not established.
    bool Load(const PsbValue& metadata, std::string& error);
    // Top-level metadata key names seen by the last successful Load (unknown
    // controls are recorded by name only; their values stay in the document).
    const std::set<std::string>& MetadataKeys() const { return metadata_keys_; }
    const std::vector<EmoteNamedControl>& Transitions() const { return transitions_; }
    const std::vector<EmoteNamedControl>& Immediates() const { return immediates_; }
    const std::vector<EmoteOrbitControl>& Orbits() const { return orbits_; }
    bool ImmediateKeyPresent() const { return immediate_key_present_; }
    const std::set<std::string>& AuthoredInstantVariables() const { return authored_instant_; }
    // Values remain authored floats: no units/coordinate conversion or fallback
    // insertion. Pure C++ queries; no evidence of a Lua API is assumed.
    std::optional<float> ProfileHeight() const { return profile_height_; }
    std::optional<float> PixelMarker(const std::string& name) const;
    const std::map<std::string, float>& PixelMarkers() const { return pixel_markers_; }
private:
    std::set<std::string> metadata_keys_;
    std::vector<EmoteNamedControl> transitions_, immediates_;
    std::vector<EmoteOrbitControl> orbits_;
    std::set<std::string> authored_instant_;
    std::optional<float> profile_height_;
    std::map<std::string, float> pixel_markers_;
    bool immediate_key_present_ = false;
};

enum class EmoteControlKind { Transition, Immediate, Orbit };
struct EmoteControlRegistration {
    EmoteControlKind kind = EmoteControlKind::Transition;
    std::string label;
    size_t source_index = 0; // index in corresponding config list; unused for defaults
    bool default_immediate = false;
};
struct EmoteControlRegistryPlan {
    std::vector<EmoteControlRegistration> additions;
    std::set<std::string> registered_labels; // existing + accepted additions
    std::set<std::string> instant_variables; // authored + successful defaults ONLY
};
// Call AFTER other controls (eye/selector/etc.) have been registered. Supply
// their ACTUAL registered labels, not variableList/timeline declarations, nor
// disabled or unknown metadata controls. Explicit active collisions fail by
// local safety policy; native registry first-wins alone does NOT establish that
// conflicting control lists stop producing output. Absent-key defaults skip
// occupied labels. No model/player
// mutation; caller commits this plan together with its own load transaction.
bool BuildEmoteControlRegistryPlan(const EmoteControlConfig& config,
                                   const std::set<std::string>& registered_labels,
                                   EmoteControlRegistryPlan& out, std::string& error);

struct EmoteOrbitSample {
    size_t segment = 0;
    double value = 0;
    double remaining_frames = 0;
};
struct EmoteOrbitCommand {
    size_t segment = 0;
    double value = 0;
    double duration_frames = 0;
};
// Orbit interval, phase, Progress delta and command duration all use the same
// native-frame units. These helpers perform NO milliseconds/seconds conversion.
// Read-only, finite nonnegative elapsed-frame sample; no clock or index mutation.
// At exact boundaries the new segment owns the sample. Remaining time belongs
// to THAT segment, never a request to tween toward the next list entry.
bool SampleEmoteOrbit(const EmoteOrbitControl& orbit, double elapsed_frames,
                     EmoteOrbitSample& out, std::string& error);
// Low-level immediate seed writes item 0 (including when tween is enabled).
// This is NOT a complete native first-Progress operation: a caller using this
// seed must still initialize phase/index at its FIRST Progress without consuming
// that call's delta. For the native tween-duration rule use ProgressEmoteOrbit
// (or CommandAtSample at phase 0 with nullopt), not this immediate seed alone.
// Disabled orbit returns success + nullopt. Outputs unchanged on failure.
bool InitialEmoteOrbitCommand(const EmoteOrbitControl& orbit,
                             std::optional<EmoteOrbitCommand>& out, std::string& error);
// A normal Progress sample emits the CURRENT item iff its segment differs from
// the last observed segment (nullopt forces a command). Caller advances Animated
// and stores the observed segment itself. Tween duration is current remaining
// frames; non-tween duration is zero. Native normal Progress compares only old
// and new index: no intermediate replay, even after whole cycles. This low-level
// API does NOT implement first-Progress/delta consumption; see ProgressEmoteOrbit.
bool EmoteOrbitCommandAtSample(const EmoteOrbitControl& orbit, double elapsed_frames,
                              std::optional<size_t> previous_segment,
                              std::optional<EmoteOrbitCommand>& out, std::string& error);

struct EmoteOrbitState {
    bool initialized = false;
    double phase_frames = 0;
    size_t segment = 0;
};
// Pure normal-Progress state transition; no Animated/value interpolation. First
// call initializes phase=0/index=0 and emits item 0 WITHOUT consuming delta.
// Later phase=fmod(phase+delta,T); emit only if old/new index differ, never replay
// intervening items. Tween duration=(index+1)*(T/N)-phase, including first call;
// non-tween duration=0. Disabled config is a no-op (state unchanged, no command).
// Finite nonnegative delta, consistent state, finite phase+delta required by local
// safety policy. State and output unchanged on failure. Reset state to {} on
// reload/restart; do not reuse state with another configuration. Animated and
// its advancement/ordering, clip scope and queuing remain the caller's concern.
bool ProgressEmoteOrbit(const EmoteOrbitControl& orbit, double delta_frames,
                       EmoteOrbitState& state, std::optional<EmoteOrbitCommand>& out,
                       std::string& error);
} // namespace artc
