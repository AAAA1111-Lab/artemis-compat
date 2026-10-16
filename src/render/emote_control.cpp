#include "render/emote_control.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace artc {
namespace {
const PsbValue* Field(const PsbValue& value, const std::string& key) {
    const auto it = value.object.find(key);
    return it == value.object.end() ? nullptr : &it->second;
}
void Require(bool ok, const char* error) {
    if (!ok) throw std::runtime_error(error);
}
struct Budget {
    size_t nodes = 0, bytes = 0, entries = 0;
    void Bytes(size_t n) {
        Require(n <= kEmoteControlMaxRawBytes - bytes, "E-mote control raw byte budget");
        bytes += n;
    }
    void Raw(const PsbValue& v, size_t depth = 0) {
        Require(depth <= kEmoteControlMaxDepth, "E-mote control raw depth budget");
        Require(++nodes <= kEmoteControlMaxRawNodes, "E-mote control raw node budget");
        Bytes(sizeof(PsbValue));
        Bytes(v.string.size());
        if (v.type == PsbValue::Number || v.type == PsbValue::Boolean)
            Require(std::isfinite(v.number), "non-finite E-mote control raw number");
        for (const auto& item : v.array) Raw(item, depth + 1);
        for (const auto& item : v.object) { Bytes(item.first.size()); Raw(item.second, depth + 1); }
    }
    void Entries(size_t n) {
        Require(n <= kEmoteControlMaxEntries - entries, "E-mote control entry budget");
        entries += n;
    }
};
double Numeric(const PsbValue& value) {
    Require(value.type == PsbValue::Number && std::isfinite(value.number),
            "invalid E-mote control number");
    return value.number;
}
bool Flag(const PsbValue& value) {
    Require((value.type == PsbValue::Number || value.type == PsbValue::Boolean) &&
            std::isfinite(value.number) && (value.number == 0 || value.number == 1),
            "invalid E-mote control flag");
    return value.number != 0;
}
float ProfileFloat(const PsbValue& value) {
    const double n = Numeric(value);
    Require(std::abs(n) <= std::numeric_limits<float>::max(), "E-mote profile float overflow");
    return static_cast<float>(n);
}
const PsbValue& Required(const PsbValue& value, const char* key) {
    const auto* field = Field(value, key);
    Require(field != nullptr, "missing required E-mote control field");
    return *field;
}
std::string Label(const PsbValue& value) {
    Require(value.type == PsbValue::String && !value.string.empty(), "invalid E-mote control label");
    return value.string;
}
const std::vector<PsbValue>& List(const PsbValue& value, Budget& budget) {
    Require(value.type == PsbValue::Array, "invalid E-mote control array");
    budget.Entries(value.array.size());
    return value.array;
}
EmoteNamedControl Named(const PsbValue& value, std::set<std::string>& labels) {
    Require(value.type == PsbValue::Object, "invalid E-mote control entry");
    EmoteNamedControl result;
    result.label = Label(Required(value, "label"));
    if (const auto* enabled = Field(value, "enabled")) result.enabled = Flag(*enabled);
    Require(labels.insert(result.label).second, "duplicate E-mote control label (local policy)");
    return result;
}
void ValidateOrbit(const EmoteOrbitControl& orbit) {
    Require(!orbit.label.empty(), "invalid E-mote orbit label");
    Require(std::isfinite(orbit.period_frames) && orbit.period_frames > 0,
            "invalid E-mote orbit period");
    Require(!orbit.values.empty() && orbit.values.size() <= kEmoteControlMaxEntries,
            "invalid E-mote orbit list");
    const double segment = orbit.period_frames / static_cast<double>(orbit.values.size());
    Require(std::isfinite(segment) && segment > 0, "unrepresentable E-mote orbit segment");
    for (double value : orbit.values)
        Require(std::isfinite(value), "non-finite E-mote orbit value");
}
template <class F> bool Checked(std::string& error, F function) {
    try { function(); error.clear(); return true; }
    catch (const std::exception& e) { error = e.what(); return false; }
}
} // namespace

bool EmoteControlConfig::Load(const PsbValue& metadata, std::string& error) {
    return Checked(error, [&] {
        Require(metadata.type == PsbValue::Object || metadata.type == PsbValue::Null,
                "invalid E-mote metadata object");
        // The budget covers ONLY the subtrees this configuration reads. Unrelated
        // metadata (real models carry megabytes of it) is neither walked nor copied.
        Budget budget;
        EmoteControlConfig next;
        std::set<std::string> labels;
        for (const auto& item : metadata.object) next.metadata_keys_.insert(item.first);
        for (const char* key : {"transitionControl", "immediateControl", "orbitControl",
                                "instantVariableList", "charaProfile"})
            if (const auto* field = Field(metadata, key)) budget.Raw(*field);
        if (const auto* field = Field(metadata, "transitionControl"))
            for (const auto& entry : List(*field, budget)) next.transitions_.push_back(Named(entry, labels));
        if (const auto* field = Field(metadata, "immediateControl")) {
            next.immediate_key_present_ = true;
            for (const auto& entry : List(*field, budget)) next.immediates_.push_back(Named(entry, labels));
        }
        if (const auto* field = Field(metadata, "orbitControl")) {
            for (const auto& entry : List(*field, budget)) {
                EmoteOrbitControl orbit;
                static_cast<EmoteNamedControl&>(orbit) = Named(entry, labels);
                orbit.period_frames = Numeric(Required(entry, "interval"));
                orbit.tween = Flag(Required(entry, "tween"));
                for (const auto& value : List(Required(entry, "orbitFrameList"), budget))
                    orbit.values.push_back(Numeric(value));
                ValidateOrbit(orbit);
                next.orbits_.push_back(std::move(orbit));
            }
        }
        if (const auto* field = Field(metadata, "instantVariableList"))
            for (const auto& entry : List(*field, budget)) next.authored_instant_.insert(Label(entry));
        if (const auto* profile = Field(metadata, "charaProfile")) {
            Require(profile->type == PsbValue::Object, "invalid E-mote character profile");
            if (const auto* height = Field(*profile, "height")) next.profile_height_ = ProfileFloat(*height);
            if (const auto* markers = Field(*profile, "pixelMarker")) {
                Require(markers->type == PsbValue::Object, "invalid E-mote pixel marker map");
                budget.Entries(markers->object.size());
                for (const auto& marker : markers->object)
                    next.pixel_markers_.emplace(marker.first, ProfileFloat(marker.second));
            }
        }
        *this = std::move(next);
    });
}

std::optional<float> EmoteControlConfig::PixelMarker(const std::string& name) const {
    const auto it = pixel_markers_.find(name);
    return it == pixel_markers_.end() ? std::nullopt : std::optional<float>(it->second);
}

bool BuildEmoteControlRegistryPlan(const EmoteControlConfig& config,
                                   const std::set<std::string>& registered_labels,
                                   EmoteControlRegistryPlan& out, std::string& error) {
    return Checked(error, [&] {
        Require(registered_labels.size() <= kEmoteControlMaxEntries, "E-mote registered label budget");
        EmoteControlRegistryPlan next;
        next.registered_labels = registered_labels;
        next.instant_variables = config.AuthoredInstantVariables();
        const auto add = [&](const auto& entries, EmoteControlKind kind) {
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                if (!entry.enabled) continue;
                Require(next.registered_labels.insert(entry.label).second,
                        "E-mote registered control conflict (local policy)");
                next.additions.push_back({kind, entry.label, i, false});
            }
        };
        add(config.Transitions(), EmoteControlKind::Transition);
        add(config.Immediates(), EmoteControlKind::Immediate);
        add(config.Orbits(), EmoteControlKind::Orbit);
        if (!config.ImmediateKeyPresent()) {
            for (const char* label : {"face_cheek", "face_tears", "face_eye_hi"}) {
                if (!next.registered_labels.insert(label).second) continue;
                next.additions.push_back({EmoteControlKind::Immediate, label, 0, true});
                next.instant_variables.insert(label);
            }
        }
        Require(next.registered_labels.size() <= kEmoteControlMaxEntries,
                "E-mote registered label budget");
        out = std::move(next);
    });
}

bool SampleEmoteOrbit(const EmoteOrbitControl& orbit, double elapsed_frames,
                     EmoteOrbitSample& out, std::string& error) {
    return Checked(error, [&] {
        ValidateOrbit(orbit);
        Require(std::isfinite(elapsed_frames) && elapsed_frames >= 0, "invalid E-mote orbit sample time");
        const double phase = std::fmod(elapsed_frames, orbit.period_frames);
        const double segment_frames = orbit.period_frames / static_cast<double>(orbit.values.size());
        // Compare against the SAME computed boundaries used for remaining time;
        // avoid division rounding selecting the old segment at a boundary.
        size_t begin = 0, end = orbit.values.size();
        while (begin + 1 < end) {
            const size_t middle = begin + (end - begin) / 2;
            if (phase >= segment_frames * static_cast<double>(middle)) begin = middle;
            else end = middle;
        }
        const double boundary = segment_frames * static_cast<double>(begin + 1);
        const double remaining = boundary - phase;
        Require(std::isfinite(remaining) && remaining > 0, "unrepresentable E-mote orbit remaining time");
        out = {begin, orbit.values[begin], remaining};
    });
}

bool InitialEmoteOrbitCommand(const EmoteOrbitControl& orbit,
                             std::optional<EmoteOrbitCommand>& out, std::string& error) {
    return Checked(error, [&] {
        ValidateOrbit(orbit);
        std::optional<EmoteOrbitCommand> next;
        if (orbit.enabled) next = EmoteOrbitCommand{0, orbit.values.front(), 0};
        out = next;
    });
}

bool EmoteOrbitCommandAtSample(const EmoteOrbitControl& orbit, double elapsed_frames,
                              std::optional<size_t> previous_segment,
                              std::optional<EmoteOrbitCommand>& out, std::string& error) {
    return Checked(error, [&] {
        EmoteOrbitSample sample;
        std::string sample_error;
        if (!SampleEmoteOrbit(orbit, elapsed_frames, sample, sample_error))
            throw std::runtime_error(sample_error);
        Require(!previous_segment || *previous_segment < orbit.values.size(), "invalid E-mote previous orbit segment");
        std::optional<EmoteOrbitCommand> next;
        if (orbit.enabled && (!previous_segment || *previous_segment != sample.segment))
            next = EmoteOrbitCommand{sample.segment, sample.value, orbit.tween ? sample.remaining_frames : 0};
        out = next;
    });
}

bool ProgressEmoteOrbit(const EmoteOrbitControl& orbit, double delta_frames,
                       EmoteOrbitState& state, std::optional<EmoteOrbitCommand>& out,
                       std::string& error) {
    return Checked(error, [&] {
        ValidateOrbit(orbit);
        Require(std::isfinite(delta_frames) && delta_frames >= 0, "invalid E-mote orbit delta");
        if (!orbit.enabled) { out.reset(); return; }
        EmoteOrbitState next;
        std::optional<size_t> previous;
        std::string inner_error;
        if (state.initialized) {
            Require(std::isfinite(state.phase_frames) && state.phase_frames >= 0 &&
                    state.phase_frames < orbit.period_frames, "invalid E-mote orbit state phase");
            EmoteOrbitSample current;
            if (!SampleEmoteOrbit(orbit, state.phase_frames, current, inner_error))
                throw std::runtime_error(inner_error);
            Require(current.segment == state.segment, "inconsistent E-mote orbit state segment");
            const double advanced = state.phase_frames + delta_frames;
            Require(std::isfinite(advanced), "E-mote orbit phase overflow");
            next.phase_frames = std::fmod(advanced, orbit.period_frames);
            previous = state.segment;
        }
        EmoteOrbitSample sample;
        if (!SampleEmoteOrbit(orbit, next.phase_frames, sample, inner_error))
            throw std::runtime_error(inner_error);
        next.initialized = true;
        next.segment = sample.segment;
        std::optional<EmoteOrbitCommand> command;
        if (!previous || *previous != next.segment)
            command = EmoteOrbitCommand{sample.segment, sample.value,
                                       orbit.tween ? sample.remaining_frames : 0};
        state = next;
        out = command;
    });
}
} // namespace artc
