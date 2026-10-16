// E10 pure helpers: synthetic PSB metadata only, no player/GL/Lua/game assets.
#include "render/emote_control.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>

namespace {
using namespace artc;
int failures = 0;
void Check(bool ok, const std::string& message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void Near(double value, double expected, const std::string& message) {
    Check(std::isfinite(value) && std::abs(value - expected) < 1e-9, message);
}
PsbValue N(double n) { PsbValue v; v.type = PsbValue::Number; v.number = n; return v; }
PsbValue S(std::string s) { PsbValue v; v.type = PsbValue::String; v.string = std::move(s); return v; }
PsbValue A(std::vector<PsbValue> a = {}) {
    PsbValue v; v.type = PsbValue::Array; v.array = std::move(a); return v;
}
PsbValue O(std::map<std::string, PsbValue> o = {}) {
    PsbValue v; v.type = PsbValue::Object; v.object = std::move(o); return v;
}
PsbValue Named(const std::string& label, bool enabled = true) {
    return O({{"label", S(label)}, {"enabled", N(enabled ? 1 : 0)}});
}
PsbValue Orbit(const std::string& label = "orbit", bool tween = true) {
    auto result = Named(label);
    result.object["interval"] = N(12);
    result.object["tween"] = N(tween ? 1 : 0);
    result.object["orbitFrameList"] = A({N(2), N(8), N(-4)});
    return result;
}
bool Load(EmoteControlConfig& config, const PsbValue& metadata) {
    std::string error = "stale";
    const bool ok = config.Load(metadata, error);
    Check(ok && error.empty(), "load synthetic metadata: " + error);
    return ok;
}
EmoteControlRegistryPlan Plan(const EmoteControlConfig& config,
                               const std::set<std::string>& existing = {}) {
    EmoteControlRegistryPlan out;
    std::string error = "stale";
    Check(BuildEmoteControlRegistryPlan(config, existing, out, error) && error.empty(),
          "build registry plan: " + error);
    return out;
}
void MissingEmptyNullTests() {
    EmoteControlConfig config;
    Load(config, O());
    Check(!config.ImmediateKeyPresent(), "absent immediate key retained as absent");
    auto plan = Plan(config);
    Check(plan.additions.size() == 3, "missing immediate key adds three defaults");
    Check(plan.registered_labels == std::set<std::string>{"face_cheek", "face_tears", "face_eye_hi"},
          "only exact three face defaults, not bare tears/eye_hi or a wildcard");
    Check(plan.instant_variables == plan.registered_labels, "successful defaults append instant variables");
    for (const auto& entry : plan.additions)
        Check(entry.kind == EmoteControlKind::Immediate && entry.default_immediate,
              "default registrations are identified as immediate defaults");

    Load(config, O({{"immediateControl", A()}}));
    Check(config.ImmediateKeyPresent(), "explicit empty immediate key remains present");
    Check(Plan(config).additions.empty() && Plan(config).instant_variables.empty(),
          "explicit empty immediate suppresses defaults and default instant entries");
    std::string error;
    Check(!config.Load(O({{"immediateControl", PsbValue{}}}), error) && !error.empty(),
          "present null immediate fails rather than using defaults");
    Check(config.ImmediateKeyPresent() && Plan(config).additions.empty(), "null failure is atomic");
    for (auto field : {"transitionControl", "orbitControl", "instantVariableList", "charaProfile"})
        Check(!config.Load(O({{field, PsbValue{}}}), error), "present null rejected: " + std::string(field));
    Check(config.ImmediateKeyPresent(), "other null failures preserve config");

    Load(config, PsbValue{});
    Check(!config.ImmediateKeyPresent() && Plan(config).additions.size() == 3,
          "absent metadata accepted as no authored controls");
}

void RegistrationAndInstantTests() {
    EmoteControlConfig config;
    Load(config, O({{"instantVariableList", A({S("authored"), S("authored")})},
                    {"immediateControl", A({Named("explicit"), Named("disabled", false)})},
                    {"transitionControl", A({Named("transition")})},
                    {"orbitControl", A({Orbit()})}}));
    auto plan = Plan(config, {"eye", "selector", "other"});
    Check(plan.additions.size() == 3, "only active explicit transition/immediate/orbit registered");
    Check(!plan.registered_labels.count("disabled") && !plan.registered_labels.count("face_cheek"),
          "disabled explicit not registered, present key prevents defaults");
    Check(plan.instant_variables == std::set<std::string>{"authored"},
          "explicit immediate does NOT automatically become instant");
    Check(plan.additions[0].kind == EmoteControlKind::Transition &&
          plan.additions[1].kind == EmoteControlKind::Immediate &&
          plan.additions[2].kind == EmoteControlKind::Orbit && plan.additions[2].source_index == 0,
          "typed registrations point into source config arrays");
    const auto before = plan.registered_labels;
    std::string error;
    Check(!BuildEmoteControlRegistryPlan(config, {"explicit"}, plan, error), "explicit occupied label rejected");
    Check(plan.registered_labels == before && plan.additions.size() == 3,
          "failed registry plan leaves output unchanged");

    Load(config, O({{"transitionControl", A({Named("face_cheek")})},
                    {"instantVariableList", A({S("authored"), S("face_eye_hi")})},
                    // Raw declarations are NOT control registrations.
                    {"variableList", A({Named("face_tears"), Named("face_other")})},
                    {"unknownControl", A({Named("face_tears")})}}));
    plan = Plan(config, {"face_eye_hi", "external_eye", "external_selector"});
    Check(plan.additions.size() == 2 && plan.additions[0].label == "face_cheek" &&
          plan.additions[1].label == "face_tears", "default skips existing and new active controls");
    Check(!plan.instant_variables.count("face_cheek") && plan.instant_variables.count("face_tears") &&
          plan.instant_variables.count("face_eye_hi") && plan.instant_variables.count("authored"),
          "only successful default appends instant; independent authored conflict stays authored");
    Check(!plan.registered_labels.count("face_other"), "variableList not promoted to control registry");
    Check(config.MetadataKeys().count("unknownControl") == 1, "unknown runtime control recorded by name only");
    const auto raw_size = config.MetadataKeys().size();
    Check(!BuildEmoteControlRegistryPlan(config, {"face_cheek", "face_tears", "face_eye_hi"}, plan, error),
          "explicit transition collision is not silently treated as a default conflict");
    Check(config.MetadataKeys().size() == raw_size, "planning never mutates recorded metadata keys");
}

void ConflictAndDisabledTests() {
    EmoteControlConfig config;
    Load(config, O({{"transitionControl", A({Named("face_cheek", false)})}}));
    auto plan = Plan(config);
    Check(plan.additions.size() == 3 && plan.instant_variables.count("face_cheek"),
          "disabled explicit controller does not occupy default label");
    Load(config, O({{"immediateControl", A({Named("face_cheek", false)})}}));
    Check(Plan(config).additions.empty(), "disabled present immediate list still suppresses all defaults");
    Load(config, O());
    plan = Plan(config, {"face_cheek", "face_tears", "face_eye_hi"});
    Check(plan.additions.empty() && plan.instant_variables.empty(), "all default conflicts skipped, no instant appended");
    for (auto field : {"eyeControl", "selectorControl", "otherControl"}) {
        // The caller, not raw declarations, supplies actual enabled registrations.
        Load(config, O({{field, A({Named("face_cheek")})}}));
        plan = Plan(config, {"face_cheek"});
        Check(plan.additions.size() == 2 && !plan.instant_variables.count("face_cheek"),
              "external registered label respected: " + std::string(field));
    }
    std::string error;
    const auto sentinel = config.MetadataKeys().size();
    for (auto metadata : {
        O({{"transitionControl", A({Named("same"), Named("same")})}}),
        O({{"immediateControl", A({Named("same"), Named("same", false)})}}),
        O({{"transitionControl", A({Named("same")})}, {"immediateControl", A({Named("same")})}}),
        O({{"transitionControl", A({Named("same")})}, {"orbitControl", A({Orbit("same")})}})
    }) {
        Check(!config.Load(metadata, error) && !error.empty(), "duplicate/cross-control conflict locally rejected");
        Check(config.MetadataKeys().size() == sentinel, "conflicting reload atomic");
    }
    Load(config, O({{"transitionControl", A({Named("eye")})}, {"immediateControl", A()}}));
    plan.registered_labels = {"sentinel"};
    Check(!BuildEmoteControlRegistryPlan(config, {"eye"}, plan, error) &&
          plan.registered_labels == std::set<std::string>{"sentinel"}, "external explicit collision atomic");
}

void ProfileAndReloadTests() {
    EmoteControlConfig config;
    Load(config, O({{"charaProfile", O({{"height", N(170.125)},
        {"pixelMarker", O({{"negative", N(-8.25)}, {"zero", N(0)}, {"fraction", N(0.125)}})}})}}));
    Check(config.ProfileHeight() && *config.ProfileHeight() == 170.125f, "profile authored float height");
    Check(config.PixelMarker("negative") == -8.25f && config.PixelMarker("zero") == 0.f &&
          config.PixelMarker("fraction") == 0.125f, "marker raw signed/fractional floats preserved");
    const auto count = config.PixelMarkers().size();
    Check(!config.PixelMarker("missing") && !config.PixelMarker("missing") && config.PixelMarkers().size() == count,
          "missing marker optional and read-only; no map insertion");
    std::string error;
    for (double value : {std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::max()}) {
        Check(!config.Load(O({{"charaProfile", O({{"height", N(value)}})}}), error),
              "non-finite/float-overflow profile height rejected");
        Check(config.ProfileHeight() == 170.125f && config.PixelMarkers().size() == count,
              "invalid profile reload preserves previous height and marker map");
        Check(!config.Load(O({{"charaProfile", O({{"pixelMarker", O({{"bad", N(value)}})}})}}), error),
              "non-finite/float-overflow marker rejected");
    }
    Check(!config.Load(O({{"charaProfile", O({{"pixelMarker", A({N(1)})}})}}), error),
          "pixelMarker must be object, not positional vector");
    Load(config, O({{"immediateControl", A()}, {"charaProfile", O()}}));
    Check(!config.ProfileHeight() && config.PixelMarkers().empty() && !config.PixelMarker("negative"),
          "successful reload clears old profile fields");
    Load(config, O({{"instantVariableList", A({S("old")})}, {"orbitControl", A({Orbit()})}}));
    Load(config, O({{"immediateControl", A()}}));
    Check(config.AuthoredInstantVariables().empty() && config.Orbits().empty() && config.Transitions().empty(),
          "successful reload replaces, not appends controls/instant variables");
}

void ValidationAndBudgetTests() {
    EmoteControlConfig config;
    Load(config, O({{"immediateControl", A()}}));
    std::string error;
    for (auto bad : {N(1), S("x"), A(), O({{"label", S("")}}),
                     O({{"label", S("x")}, {"enabled", N(2)}}),
                     O({{"label", S("x")}, {"enabled", PsbValue{}}})}) {
        Check(!config.Load(O({{"transitionControl", A({bad})}}), error), "invalid named control shape rejected");
        Check(config.ImmediateKeyPresent(), "invalid shape reload atomic");
    }
    for (auto bad : {N(0), N(-1), N(std::numeric_limits<double>::infinity()), S("12")}) {
        auto entry = Orbit(); entry.object["interval"] = bad;
        Check(!config.Load(O({{"orbitControl", A({entry})}}), error), "invalid orbit period rejected");
    }
    for (auto key : {"interval", "tween", "orbitFrameList"}) {
        auto entry = Orbit(); entry.object.erase(key);
        Check(!config.Load(O({{"orbitControl", A({entry})}}), error), "unconfirmed missing orbit defaults not invented");
    }
    for (auto values : {A(), A({S("1")}), A({N(std::numeric_limits<double>::quiet_NaN())}), O()}) {
        auto entry = Orbit(); entry.object["orbitFrameList"] = values;
        Check(!config.Load(O({{"orbitControl", A({entry})}}), error), "invalid orbit list rejected");
    }
    auto entry = Orbit(); entry.object["interval"] = N(std::numeric_limits<double>::denorm_min());
    Check(!config.Load(O({{"orbitControl", A({entry})}}), error), "underflowed segment rejected");
    // The raw preflight covers only the control fields this configuration reads.
    Check(!config.Load(O({{"transitionControl", A({O({{"label", S("x")},
                                                       {"extra", N(std::numeric_limits<double>::infinity())}})})}}), error),
          "non-finite number inside a read control field preflighted");
    PsbValue entries = A(std::vector<PsbValue>(kEmoteControlMaxEntries + 1, S("a")));
    Check(!config.Load(O({{"instantVariableList", entries}}), error) && error.find("entry budget") != std::string::npos,
          "aggregate authored entry budget enforced before parsing");
    PsbValue many_orbits = Orbit();
    many_orbits.object["orbitFrameList"] = A(std::vector<PsbValue>(kEmoteControlMaxEntries, N(1)));
    Check(!config.Load(O({{"orbitControl", A({many_orbits})}}), error) && error.find("entry budget") != std::string::npos,
          "orbit items share aggregate budget with control entries");
    Check(!config.Load(O({{"transitionControl", A({O({{"label", S(std::string(kEmoteControlMaxRawBytes + 1, 'x'))}})})}}),
                       error) && error.find("raw byte budget") != std::string::npos,
          "control field raw byte budget enforced");
    PsbValue deep = N(0);
    for (size_t i = 0; i <= kEmoteControlMaxDepth; ++i) deep = A({std::move(deep)});
    Check(!config.Load(O({{"charaProfile", deep}}), error), "control field raw depth bounded");
    Check(!config.Load(O({{"transitionControl", A({O({{"label", S("x")}, {"deep", deep}})})}}), error) &&
          error.find("depth budget") != std::string::npos, "nested control field depth bounded");
    Check(config.ImmediateKeyPresent() && Plan(config).additions.empty(), "all budget failures leave prior config intact");
    {   // Unrelated/unknown metadata is neither walked, counted nor copied: real
        // models carry megabytes (tens of thousands of nodes) of it.
        std::vector<PsbValue> wide;
        for (size_t i = 0; i < kEmoteControlMaxRawNodes + 1000; ++i) wide.push_back(S(std::string(80, 'x')));
        PsbValue huge_object = O();
        for (size_t i = 0; i < 100; ++i) huge_object.object["k" + std::to_string(i)] = S(std::string(100000, 'y'));
        Check(config.Load(O({{"transitionControl", A({Named("kept")})},
                             {"unknownControl", O({{"raw", N(std::numeric_limits<double>::infinity())}})},
                             {"bigUnrelated", A(wide)},
                             {"hugeBytes", huge_object},
                             {"tooDeepUnrelated", deep}}), error) && error.empty(),
              "large/deep/non-finite unrelated metadata subtree does not trigger the control budget: " + error);
        Check(config.Transitions().size() == 1 && config.Transitions()[0].label == "kept",
              "control fields still parsed alongside a large unrelated subtree");
        Check(config.MetadataKeys().count("bigUnrelated") == 1 && config.MetadataKeys().count("hugeBytes") == 1,
              "unrelated subtrees recorded by key only");
    }
    Load(config, O({{"immediateControl", A()},
                    {"instantVariableList", A(std::vector<PsbValue>(kEmoteControlMaxEntries, S("instant")))}}));
    Check(config.AuthoredInstantVariables() == std::set<std::string>{"instant"},
          "exact entry budget accepted; authored instant duplicates form a set");
    Load(config, O({{"immediateControl", A()},
                    {"transitionControl", A({O({{"label", S("enabled_missing")}})})}}));
    Check(Plan(config).registered_labels.count("enabled_missing"), "local missing enabled policy is true");
}

void OrbitTests() {
    EmoteControlConfig config;
    Load(config, O({{"orbitControl", A({Orbit()})}, {"immediateControl", A()}}));
    const auto& orbit = config.Orbits().front();
    std::string error;
    std::optional<EmoteOrbitCommand> command;
    Check(InitialEmoteOrbitCommand(orbit, command, error) && command && command->segment == 0,
          "orbit initialization selects item zero");
    Near(command->value, 2, "initial item value");
    Near(command->duration_frames, 0, "initial write immediate even with tween");
    for (auto test : std::vector<std::pair<double, size_t>>{{0, 0}, {3.5, 0}, {4, 1}, {7.5, 1},
            {8, 2}, {11.5, 2}, {12, 0}, {16, 1}, {24, 0}, {32.5, 2}}) {
        EmoteOrbitSample sample;
        Check(SampleEmoteOrbit(orbit, test.first, sample, error) && sample.segment == test.second,
              "equal subdivision/current modulo segment at " + std::to_string(test.first));
        Near(sample.value, orbit.values[test.second], "sample targets current item, never next");
        Near(sample.remaining_frames, 4 - std::fmod(test.first, 4), "remaining duration of current segment");
    }
    EmoteOrbitSample sample;
    Check(SampleEmoteOrbit(orbit, std::nextafter(4., 0.), sample, error) && sample.segment == 0,
          "sample immediately before boundary still old segment");
    Check(SampleEmoteOrbit(orbit, std::nextafter(4., 5.), sample, error) && sample.segment == 1,
          "sample immediately after boundary new segment");
    Check(EmoteOrbitCommandAtSample(orbit, 3.5, 0, command, error) && !command,
          "same segment emits nothing; no early tween to next");
    Check(EmoteOrbitCommandAtSample(orbit, 5.5, 0, command, error) && command && command->segment == 1,
          "normal sample segment change emits new current item");
    Near(command->value, 8, "new current item target");
    Near(command->duration_frames, 2.5, "tween uses remaining, not full segment nor whole period");
    Check(EmoteOrbitCommandAtSample(orbit, 8, 1, command, error) && command,
          "exact segment boundary command");
    Near(command->duration_frames, 4, "exact boundary has full segment remaining");
    Check(EmoteOrbitCommandAtSample(orbit, 12, 2, command, error) && command && command->segment == 0,
          "cycle boundary transitions to first item");
    Check(EmoteOrbitCommandAtSample(orbit, 32.5, 0, command, error) && command && command->segment == 2,
          "large sample locates final segment only; no assertion of intermediate replay semantics");
    Check(EmoteOrbitCommandAtSample(orbit, 24, 0, command, error) && !command,
          "same final index after cycles does not invent replay in this sampling helper");
    Check(EmoteOrbitCommandAtSample(orbit, 1, std::nullopt, command, error) && command && command->segment == 0,
          "caller can force sample command without previous segment");
    auto direct = orbit; direct.tween = false;
    Check(EmoteOrbitCommandAtSample(direct, 5.5, 0, command, error) && command && command->duration_frames == 0,
          "non-tween segment change is immediate");
    direct.enabled = false;
    Check(InitialEmoteOrbitCommand(direct, command, error) && !command, "disabled initialization no command");
    Check(EmoteOrbitCommandAtSample(direct, 5.5, 0, command, error) && !command, "disabled Progress no command");
    auto single = orbit; single.values = {-9};
    Check(SampleEmoteOrbit(single, 25, sample, error) && sample.segment == 0 && sample.value == -9,
          "single item orbit uses whole period as its segment");
    Near(sample.remaining_frames, 11, "single-item remaining duration");
    auto fractional = orbit; fractional.period_frames = 10;
    const double width = fractional.period_frames / fractional.values.size();
    Check(SampleEmoteOrbit(fractional, width, sample, error) && sample.segment == 1,
          "non-integral exact computed subdivision boundary belongs to new segment");
    Near(sample.remaining_frames, width, "fractional segment remaining duration");
    Check(SampleEmoteOrbit(fractional, std::nextafter(width, 0.), sample, error) && sample.segment == 0,
          "no epsilon moves immediately-before fractional sample into next segment");
    for (double invalid : {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        sample = {9, 99, 999};
        command = EmoteOrbitCommand{9, 99, 999};
        Check(!SampleEmoteOrbit(orbit, invalid, sample, error) && sample.segment == 9 && sample.value == 99,
              "invalid sample is atomic");
        Check(!EmoteOrbitCommandAtSample(orbit, invalid, 0, command, error) && command && command->segment == 9,
              "invalid command time is atomic");
    }
    Check(!EmoteOrbitCommandAtSample(orbit, 5, 3, command, error) && command && command->segment == 9,
          "invalid previous segment rejected without altering output");
    auto invalid = orbit; invalid.values[1] = std::numeric_limits<double>::infinity();
    Check(!InitialEmoteOrbitCommand(invalid, command, error) && command && command->segment == 9,
          "typed invalid config cannot bypass finite validation");
    Check(orbit.values == std::vector<double>{2, 8, -4} && orbit.period_frames == 12 &&
          config.MetadataKeys().count("orbitControl"), "sampling never mutates config/recorded keys");
}
} // namespace

int main() {
    MissingEmptyNullTests();
    RegistrationAndInstantTests();
    ConflictAndDisabledTests();
    ProfileAndReloadTests();
    ValidationAndBudgetTests();
    OrbitTests();
    if (failures) { std::cerr << failures << " E10 control regression failure(s)\n"; return 1; }
    std::cout << "E10 control helper regressions passed\n";
    return 0;
}
