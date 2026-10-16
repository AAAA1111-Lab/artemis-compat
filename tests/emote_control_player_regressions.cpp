// emote_control_player_regressions.cpp — E10 control core wired into model/player.
// Synthetic documents only; no game data.

#include "emote_scene_fixture.h"
#include "render/compositor.h"
#include "render/emote_model.h"
#include "render/emote_player.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace emote_fixture;
using artc::EmoteModel;
using artc::EmotePlayer;
using artc::PsbValue;

int g_failures = 0;
void Check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cerr << "FAIL: " << what << "\n"; }
}
bool Near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }

PsbValue Arr(std::vector<PsbValue> values) {
    PsbValue v;
    v.type = PsbValue::Array;
    v.array = std::move(values);
    return v;
}
PsbValue Named(const char* label, bool enabled = true) {
    return O({{"label", S(label)}, {"enabled", N(enabled)}});
}
PsbValue Eye(const char* label, bool enabled = true) {
    return O({{"label", S(label)}, {"beginFrame", N(0)}, {"endFrame", N(10)},
              {"blinkFrameCount", N(10)}, {"blinkIntervalMin", N(1000)},
              {"blinkIntervalMax", N(1000)}, {"enabled", N(enabled)}});
}
PsbValue Orbit(const char* label, double period, bool tween, std::vector<double> values,
               bool enabled = true) {
    std::vector<PsbValue> list;
    for (double v : values) list.push_back(N(v));
    return O({{"label", S(label)}, {"enabled", N(enabled)}, {"interval", N(period)},
              {"tween", N(tween)}, {"orbitFrameList", Arr(std::move(list))}});
}
PsbValue KeyValue(double time, double value) {
    return O({{"time", N(time)}, {"type", N(2)},
              {"content", O({{"value", N(value)}, {"easing", N(0)}})}});
}
PsbValue Track(const char* variable, std::vector<std::pair<double, double>> keys) {
    std::vector<PsbValue> frames;
    for (const auto& k : keys) frames.push_back(KeyValue(k.first, k.second));
    return O({{"label", S(variable)}, {"frameList", Arr(std::move(frames))}});
}
artc::PsbDocument Doc(std::vector<const char*> variables) {
    auto doc = Scene();
    std::vector<PsbValue> list;
    for (const char* v : variables) list.push_back(O({{"label", S(v)}}));
    doc.root.object["metadata"].object["variableList"] = Arr(std::move(list));
    return doc;
}
PsbValue& Meta(artc::PsbDocument& doc) { return doc.root.object["metadata"]; }
void SetMeta(artc::PsbDocument& doc, const char* key, PsbValue value) {
    Meta(doc).object[key] = std::move(value);
}
// Nonloop timeline (last frame `last`) or loop (begin>=0).
void AddTimeline(artc::PsbDocument& doc, const char* label, double last, double begin,
                 double end, bool diff, std::vector<PsbValue> tracks) {
    auto& list = Meta(doc).object["timelineControl"];
    if (list.type != PsbValue::Array) list = Arr({});
    list.array.push_back(O({{"label", S(label)}, {"lastTime", N(last)}, {"loopBegin", N(begin)},
                            {"loopEnd", N(end)}, {"diff", N(diff)},
                            {"variableList", Arr(std::move(tracks))}}));
}

std::shared_ptr<EmoteModel> LoadModel(artc::PsbDocument doc, const std::string& what) {
    auto model = std::make_shared<EmoteModel>();
    std::string error;
    if (!model->Load(std::move(doc), error)) {
        Check(false, "load model " + what + ": " + error);
        return nullptr;
    }
    return model;
}
std::unique_ptr<EmotePlayer> LoadPlayer(std::shared_ptr<EmoteModel> model, const std::string& what) {
    auto player = std::make_unique<EmotePlayer>();
    std::string error;
    if (!model || !player->Load(model, error)) {
        Check(false, "load player " + what + ": " + error);
        return nullptr;
    }
    return player;
}
std::unique_ptr<EmotePlayer> Make(artc::PsbDocument doc, const std::string& what) {
    return LoadPlayer(LoadModel(std::move(doc), what), what);
}
bool Set(EmotePlayer& p, const char* label, double v, double ms, double ease = 0) {
    std::string error;
    return p.SetVariable(label, v, ms, ease, error);
}
double Get(const EmotePlayer& p, const char* label, bool* found = nullptr) {
    return p.GetVariable(label, found);
}
double Ms(double frames) { return frames / EmotePlayer::kFramesPerMillisecond; }

bool IsInstant(const EmoteModel& model, const char* timeline, size_t track) {
    return model.Timelines().at(timeline).tracks.at(track).instant;
}

// ---- transitions ----------------------------------------------------------
void TransitionTests() {
    auto doc = Doc({"expression"});
    SetMeta(doc, "transitionControl", Arr({Named("t_x"), Named("t_off", false)}));
    auto player = Make(std::move(doc), "transition");
    if (!player) return;
    bool found = false;
    Check(Get(*player, "t_x", &found) == 0 && found, "enabled transition label owns a base control");
    Check(!Set(*player, "t_off", 1, 0), "disabled transition label is not registered");

    Check(Set(*player, "t_x", 10, 1000, 0) && player->IsAnimating(), "transition starts animating");
    player->Progress(500);
    Check(Near(Get(*player, "t_x"), 5), "linear midpoint");
    player->Progress(500);
    Check(Get(*player, "t_x") == 10 && !player->IsAnimating(), "endpoint is exact and idle");

    Check(Set(*player, "t_x", 0, 1000, 1), "eased transition");
    player->Progress(500);
    Check(Near(Get(*player, "t_x"), 7.5), "ease=1 squares the ratio (10 -> 0 at 0.25)");
    player->Progress(500);
    Check(Get(*player, "t_x") == 0, "eased endpoint");

    player->SetQueuing(true);
    Check(Set(*player, "t_x", 10, 1000) && Set(*player, "t_x", 20, 1000), "queued commands");
    player->Progress(1000);
    Check(Near(Get(*player, "t_x"), 10), "first queued command completes");
    player->Progress(500);
    Check(Near(Get(*player, "t_x"), 15), "second queued command runs next");
    Check(Set(*player, "t_x", 30, 1000), "third command queued");
    player->Step();
    Check(Get(*player, "t_x") == 30 && !player->IsAnimating(), "Step lands on the last queued target");
    player->SetQueuing(false);
    Check(Set(*player, "t_x", 40, 1000, 0), "restart");
    player->Skip();
    Check(Get(*player, "t_x") == 40, "Skip finishes the transition");
}

// ---- immediate controls ---------------------------------------------------
void ImmediateTests() {
    auto doc = Doc({"expression", "imm", "imm_off"});
    SetMeta(doc, "immediateControl", Arr({Named("imm"), Named("imm_off", false), Named("imm2")}));
    auto player = Make(std::move(doc), "immediate");
    if (!player) return;
    Check(Set(*player, "imm", 7, 1000, 1), "immediate set with positive duration");
    Check(Get(*player, "imm") == 7 && !player->IsAnimating(),
          "value is readable at once and nothing animates");
    player->Progress(500);
    player->Step();
    player->Skip();
    Check(Get(*player, "imm") == 7, "Progress/Step/Skip do not rebound");

    player->SetQueuing(true);
    Check(Set(*player, "imm", 3, 1000) && Set(*player, "imm", 4, 1000), "queuing immediate sets");
    Check(Get(*player, "imm") == 4 && !player->IsAnimating(), "queuing never queues immediate");
    player->Step();
    Check(Get(*player, "imm") == 4, "no pending command after Step");
    player->SetQueuing(false);

    bool found = false;
    Check(Set(*player, "imm2", 9, 500) && Get(*player, "imm2", &found) == 9 && found,
          "explicit immediate label not in variableList is registered");
    Check(Set(*player, "imm_off", 5, 1000) && player->IsAnimating() && Get(*player, "imm_off") == 0,
          "disabled immediate entry stays an ordinary control");
    Check(!Set(*player, "face_cheek", 1, 0), "explicit immediateControl suppresses defaults");
}

// ---- default matrix -------------------------------------------------------
void DefaultMatrixTests() {
    {   // key missing
        auto doc = Doc({"expression", "face_other", "face_tears"});
        AddTimeline(doc, "t", 60, -1, -1, false,
                    {Track("face_cheek", {{0, 1}, {60, 2}}), Track("face_other", {{0, 1}, {60, 2}}),
                     Track("face_tears", {{0, 1}, {60, 2}}), Track("face_eye_hi", {{0, 1}, {60, 2}})});
        auto model = LoadModel(std::move(doc), "default missing");
        if (!model) return;
        const auto& instant = model->ControlPlan().instant_variables;
        Check(instant.size() == 3 && instant.count("face_cheek") && instant.count("face_tears") &&
              instant.count("face_eye_hi"), "defaults extend the effective instant set");
        Check(IsInstant(*model, "t", 0) && !IsInstant(*model, "t", 1) && IsInstant(*model, "t", 2) &&
              IsInstant(*model, "t", 3), "instant flags are set before timeline parsing");
        auto player = LoadPlayer(model, "default missing");
        if (!player) return;
        bool found = false;
        Check(Get(*player, "face_cheek", &found) == 0 && found, "undeclared default label registered");
        Check(Set(*player, "face_cheek", 5, 1000, 1) && Get(*player, "face_cheek") == 5 &&
              !player->IsAnimating(), "face_cheek default is immediate");
        Check(Set(*player, "face_tears", 6, 1000) && Get(*player, "face_tears") == 6,
              "declared-in-variableList face_tears is still the default immediate control");
        Check(Set(*player, "face_eye_hi", 7, 1000) && Get(*player, "face_eye_hi") == 7,
              "face_eye_hi default is immediate");
        Check(Set(*player, "face_other", 5, 1000) && Get(*player, "face_other") == 0 &&
              player->IsAnimating(), "face_other is an ordinary control (no face_* wildcard)");
        Check(!Set(*player, "face_xyz", 1, 0), "arbitrary undeclared face_* is not registered");
    }
    {   // explicit empty array
        auto doc = Doc({"expression", "face_cheek", "face_tears"});
        SetMeta(doc, "immediateControl", Arr({}));
        AddTimeline(doc, "t", 60, -1, -1, false, {Track("face_cheek", {{0, 1}, {60, 2}})});
        auto model = LoadModel(std::move(doc), "default empty");
        if (!model) return;
        Check(model->ControlPlan().instant_variables.empty() && !IsInstant(*model, "t", 0),
              "explicit empty array adds no defaults or instant tracks");
        auto player = LoadPlayer(model, "default empty");
        if (!player) return;
        Check(Set(*player, "face_cheek", 5, 1000) && Get(*player, "face_cheek") == 0 &&
              player->IsAnimating(), "face_cheek is ordinary under an explicit empty table");
        Check(!Set(*player, "face_eye_hi", 1, 0), "undeclared default name stays unknown");
    }
    {   // conflicts: only ACTUAL registered labels count
        auto doc = Doc({"expression", "face_tears", "face_cheek"});
        SetMeta(doc, "eyeControl", Arr({Eye("face_tears"), Eye("face_eye_hi", false)}));
        SetMeta(doc, "transitionControl", Arr({Named("face_cheek")}));
        auto model = LoadModel(std::move(doc), "default conflict");
        if (!model) return;
        const auto& instant = model->ControlPlan().instant_variables;
        Check(instant.size() == 1 && instant.count("face_eye_hi"),
              "enabled eye/transition labels skip defaults; a DISABLED eye control does not");
        auto player = LoadPlayer(model, "default conflict");
        if (!player) return;
        Check(Set(*player, "face_tears", 5, 1000) && player->IsAnimating() &&
              Get(*player, "face_tears") == 0, "eye-occupied face_tears is not immediate");
        Check(Set(*player, "face_cheek", 5, 1000) && Get(*player, "face_cheek") == 0,
              "transition-occupied face_cheek stays animated");
        Check(Set(*player, "face_eye_hi", 5, 1000) && Get(*player, "face_eye_hi") == 5,
              "free default label is still immediate");
    }
    {   // selector label conflict
        auto doc = Doc({"expression"});
        SetMeta(doc, "selectorControl",
                Arr({O({{"label", S("face_eye_hi")},
                        {"optionList", Arr({O({{"label", S("opt")}})})}})}));
        auto model = LoadModel(std::move(doc), "selector conflict");
        if (!model) return;
        const auto& instant = model->ControlPlan().instant_variables;
        Check(instant.size() == 2 && !instant.count("face_eye_hi"),
              "enabled selector label occupies its default name");
    }
    {   // declarations alone never conflict; explicit collision fails and is transactional
        auto doc = Doc({"expression"});
        SetMeta(doc, "eyeControl", Arr({Eye("shared")}));
        SetMeta(doc, "transitionControl", Arr({Named("shared")}));
        auto good = LoadModel(Doc({"expression", "keep"}), "good");
        if (!good) return;
        std::string error;
        EmoteModel model;
        Check(model.Load(Doc({"expression", "keep"}), error), "baseline model");
        Check(!model.Load(std::move(doc), error) && !error.empty(),
              "explicit active eye/transition collision fails Load");
        Check(model.Variables().count("keep") == 1, "failed Load keeps the previous model");
    }
}

// ---- instant independence + routing --------------------------------------
void InstantIndependenceTests() {
    auto doc = Doc({"expression", "imm"});
    SetMeta(doc, "immediateControl", Arr({Named("imm")}));
    AddTimeline(doc, "diff", -1, 0, 40, false, {Track("imm", {{0, 2}, {40, 2}})});
    auto model = LoadModel(std::move(doc), "immediate not instant");
    if (!model) return;
    Check(!IsInstant(*model, "diff", 0) && model->ControlPlan().instant_variables.empty(),
          "explicit immediate does not become instant");
    auto player = LoadPlayer(model, "immediate not instant");
    if (!player) return;
    Check(Set(*player, "imm", 3, 0), "immediate base");
    std::string error;
    Check(player->PlayTimeline("diff", 3, error), "flags3 timeline on an immediate variable");
    Check(Get(*player, "imm") == 3, "private difference never touches the base variable");
    player->Progress(100);
    Check(Get(*player, "imm") == 3, "base unchanged while private track runs");
}

void RoutingTests() {
    {   // Forward: ordinary non-instant timeline writes an immediate control at once
        auto doc = Doc({"expression", "imm", "slow"});
        SetMeta(doc, "immediateControl", Arr({Named("imm")}));
        AddTimeline(doc, "t", 120, -1, -1, false,
                    {Track("imm", {{0, 0}, {60, 10}, {120, 20}}),
                     Track("slow", {{0, 0}, {60, 10}, {120, 20}})});
        auto player = Make(std::move(doc), "routing");
        if (!player) return;
        std::string error;
        player->SetQueuing(true);
        Check(player->PlayTimeline("t", 1, error), "play");
        Check(Get(*player, "imm") == 10, "Forward writes the segment endpoint immediately");
        Check(Get(*player, "slow") == 0, "ordinary control still tweens");
        player->StopTimeline("t");
        Check(Near(Get(*player, "imm"), 10), "no pending command remains on the immediate control");
        player->Progress(1000);
        Check(Get(*player, "imm") == 10, "no rebound after the stop");

        // Pass
        Check(player->PlayTimeline("t", 1, error), "replay");
        player->SetQueuing(false);
        player->Pass();
        Check(Get(*player, "imm") == 20, "Pass writes remaining keys immediately");
        Check(Get(*player, "slow") != 20, "Pass leaves an ordinary control queued/animating");
        player->Progress(1);
        Check(Get(*player, "imm") == 20, "immediate control stable after Pass");
    }
    {   // instant timeline route
        auto doc = Doc({"expression", "imm"});
        SetMeta(doc, "immediateControl", Arr({Named("imm")}));
        SetMeta(doc, "instantVariableList", Arr({S("imm")}));
        AddTimeline(doc, "t", 90, -1, -1, false, {Track("imm", {{0, 1}, {30, 5}, {60, 9}})});
        auto model = LoadModel(std::move(doc), "instant routing");
        if (!model) return;
        Check(IsInstant(*model, "t", 0), "authored instant track");
        auto player = LoadPlayer(model, "instant routing");
        if (!player) return;
        std::string error;
        Check(player->PlayTimeline("t", 1, error) && Get(*player, "imm") == 1, "first key");
        player->Progress(Ms(30));
        Check(Get(*player, "imm") == 5, "crossed instant key");
        Check(Set(*player, "imm", 8, 1000) && Get(*player, "imm") == 8, "public write same route");
        player->Progress(Ms(30));
        Check(Get(*player, "imm") == 9, "next instant key");
        player->StopTimeline("t");
        Check(!player->IsAnimating(), "nothing pending");
    }
}

// ---- charaProfile --------------------------------------------------------
void ProfileTests() {
    auto doc = Doc({"expression"});
    SetMeta(doc, "charaProfile", O({{"height", N(1.5)},
                                    {"pixelMarker", O({{"eye", N(2.5)}, {"mouth", N(3)}})}}));
    EmoteModel model;
    std::string error;
    Check(model.Load(std::move(doc), error), "profile model: " + error);
    Check(model.Height() && *model.Height() == 1.5f, "Height");
    Check(model.FindPixelMarker("eye") && *model.FindPixelMarker("eye") == 2.5f, "marker eye");
    Check(!model.FindPixelMarker("nose"), "missing marker is nullopt");
    Check(model.Controls().PixelMarkers().size() == 2, "query does not insert markers");

    auto bad = Doc({"expression", "other"});
    SetMeta(bad, "orbitControl", Arr({O({{"label", S("o")}})}));  // missing required fields
    Check(!model.Load(std::move(bad), error) && !error.empty(), "invalid control config fails Load");
    Check(model.Height() && *model.Height() == 1.5f && model.FindPixelMarker("mouth") &&
          *model.FindPixelMarker("mouth") == 3.f && model.Variables().count("other") == 0,
          "failed Load keeps the previous profile and model");

    EmoteModel plain;
    Check(plain.Load(Doc({"expression"}), error), "plain model");
    Check(!plain.Height() && !plain.FindPixelMarker("eye"), "absent profile is nullopt, not zero");
}

// ---- clampControl --------------------------------------------------------
void ClampTests() {
    const auto clamp = [](double type, bool enabled) {
        return Arr({O({{"type", N(type)}, {"enabled", N(enabled)}, {"var_lr", S("a")},
                       {"var_ud", S("b")}, {"min", N(0)}, {"max", N(1)}})});
    };
    auto first = Make(Doc({"expression", "a", "b"}), "plain");
    if (!first) return;
    Check(first->UnsupportedFeatures().Empty(), "no clampControl, no unsupported features");
    {
        auto doc = Doc({"expression", "a", "b"});
        SetMeta(doc, "clampControl", clamp(0, true));
        auto model = LoadModel(std::move(doc), "clamp0");
        std::string error;
        Check(model && model->UnsupportedFeatures().Count("clampControl.type0") == 1,
              "enabled clamp type 0 is recorded by the model");
        Check(model && first->Load(model, error) && error.empty(),
              "enabled clamp type 0 degrades: Load succeeds: " + error);
        Check(first->UnsupportedFeatures().Count("clampControl.type0") == 1 &&
              !first->UnsupportedFeatures().Has("clampControl.type1"),
              "player reports the unsupported clamp type 0 exactly once per control");
        Check(Set(*first, "a", 5, 0) && Get(*first, "a") == 5 && Set(*first, "b", -3, 0) && Get(*first, "b") == -3,
              "type 0 is ignored (not faked by the min/max scalar clamp)");
        Check(!first->Load(nullptr, error) && first->Active() &&
              first->UnsupportedFeatures().Count("clampControl.type0") == 1,
              "failed player Load keeps the previous player and its features");
    }
    {
        auto doc = Doc({"expression", "a", "b"});
        SetMeta(doc, "clampControl", clamp(0, false));
        auto player = Make(std::move(doc), "disabled clamp0");
        Check(player != nullptr && player->UnsupportedFeatures().Empty(),
              "disabled type 0 is inactive and not reported");
    }
    {
        auto doc = Doc({"expression", "a", "b"});
        SetMeta(doc, "clampControl", clamp(1, true));
        auto player = Make(std::move(doc), "clamp1");
        if (!player) return;
        Check(player->UnsupportedFeatures().Count("clampControl.type1") == 1 &&
              !player->UnsupportedFeatures().Has("clampControl.type0"),
              "type 1 is recorded as unsupported");
        Check(Set(*player, "a", 5, 0) && Get(*player, "a") == 5,
              "type 1 is not implemented and is never faked with a scalar clamp");
    }
}

// ---- large unrelated metadata (real models carry megabytes of it) ----------
void LargeMetadataTests() {
    auto doc = Doc({"expression"});
    std::vector<PsbValue> wide;
    for (size_t i = 0; i < artc::kEmoteControlMaxRawNodes + 5000; ++i)
        wide.push_back(O({{"x", N(double(i))}, {"s", S(std::string(64, 'q').c_str())}}));
    SetMeta(doc, "transitionControl", Arr({Named("t_x")}));
    SetMeta(doc, "bigUnrelated", Arr(std::move(wide)));
    auto player = Make(std::move(doc), "large unrelated metadata");
    if (!player) return;
    bool found = false;
    Check(Get(*player, "t_x", &found) == 0 && found,
          "control registered although unrelated metadata exceeds the control budget");
    Check(player->UnsupportedFeatures().Empty(), "large unrelated metadata is not an unsupported feature");
}

// ---- orbit ---------------------------------------------------------------
artc::PsbDocument OrbitDoc(double period, bool tween, std::vector<double> values) {
    auto doc = Doc({"expression"});
    SetMeta(doc, "orbitControl", Arr({Orbit("orb", period, tween, std::move(values)),
                                      Orbit("orb_off", period, tween, {1}, false)}));
    return doc;
}

void OrbitTests() {
    {   // non-tween, 60 frames, 4 items of 15 frames
        auto player = Make(OrbitDoc(60, false, {10, 20, 30, 40}), "orbit");
        if (!player) return;
        bool found = false;
        Check(Get(*player, "orb", &found) == 0 && found, "orbit label owns a base control");
        Check(!Set(*player, "orb_off", 1, 0), "disabled orbit is not registered");
        player->Step();
        player->Skip();
        player->Pass();
        Check(Get(*player, "orb") == 0, "nothing is sent before the first Progress");
        player->Progress(std::nan(""));
        Check(Get(*player, "orb") == 0, "a rejected Progress does not initialize");
        player->Progress(Ms(30));
        Check(Get(*player, "orb") == 10, "first Progress sends item 0 without consuming dt");
        player->Progress(Ms(6));
        Check(Get(*player, "orb") == 10, "phase 6 stays in item 0");
        Check(Set(*player, "orb", 99, 0), "script overwrite");
        player->Step();
        player->Skip();
        player->Pass();
        player->Progress(100);  // 6 frames: phase 12, Step/Skip/Pass advanced nothing
        Check(Get(*player, "orb") == 99, "Step/Skip/Pass do not advance the phase");
        player->Progress(50);  // 3 frames: phase 15, boundary belongs to the new segment
        Check(Get(*player, "orb") == 20, "exact boundary selects the new segment");
        Check(Set(*player, "orb", 99, 0), "sentinel");
        player->Progress(Ms(60));  // whole cycle back to item 1
        Check(Get(*player, "orb") == 99, "whole cycle to the same index is not resent");
        player->Progress(Ms(30));  // phase 15+30=45 -> item 3, no replay of item 2
        Check(Get(*player, "orb") == 40, "multi-segment jump sends only the current item");
        player->Progress(Ms(15));  // wraps to phase 0
        Check(Get(*player, "orb") == 10, "wrap-around sends item 0 again");
    }
    {   // accepted time only: Progress caps at kMaxProgressMs (3600 frames)
        auto player = Make(OrbitDoc(1000, false, {1, 2, 3, 4}), "orbit cap");
        if (!player) return;
        player->Progress(0);
        Check(Get(*player, "orb") == 1, "zero first Progress initializes");
        player->Progress(1e9);
        Check(Get(*player, "orb") == 3, "only the accepted (capped) 3600 frames are consumed");
    }
    {   // tween: duration is the current segment's remaining frames
        auto player = Make(OrbitDoc(60, true, {10, 20, 30, 40}), "orbit tween");
        if (!player) return;
        player->Progress(Ms(30));
        Check(Get(*player, "orb") == 0 && player->IsAnimating(),
              "first Progress starts a tween over item 0's whole segment (not yet consumed)");
        player->Progress(Ms(6));
        Check(Near(Get(*player, "orb"), 10.0 * 6 / 15), "tween interpolates over 15 frames");
        player->Progress(Ms(9));  // t=15 frames: completes item 0, phase 15 starts item 1
        Check(Get(*player, "orb") == 10, "item 0 reached exactly");
        player->Progress(Ms(6));  // phase 21, same segment
        Check(Near(Get(*player, "orb"), 10 + 10.0 * 6 / 15), "item 1 tween progresses");
        player->Progress(Ms(12));  // item 1 completes at phase 30; phase 33 -> item 2, remaining 12
        Check(Near(Get(*player, "orb"), 20), "item 1 completed during the jump");
        player->Progress(Ms(6));
        Check(Near(Get(*player, "orb"), 20 + 10 * 6.0 / 12),
              "command duration is the remaining frames of the current segment");
        player->Skip();
        Check(Get(*player, "orb") == 30, "Skip finishes the tween at its target");
    }
    {   // reload resets orbit state
        auto model = LoadModel(OrbitDoc(60, false, {10, 20, 30, 40}), "orbit reload");
        auto player = LoadPlayer(model, "orbit reload");
        if (!player) return;
        player->Progress(Ms(20));
        player->Progress(Ms(20));
        std::string error;
        Check(player->Load(model, error), "reload");
        Check(Get(*player, "orb") == 0, "reload resets the variable");
        player->Progress(Ms(40));
        Check(Get(*player, "orb") == 10, "first Progress after reload sends item 0 again");
    }
}

// ---- composition ---------------------------------------------------------
artc::PsbDocument PairDoc() {
    auto doc = Doc({"expression", "leftEye", "rightEye"});
    const auto node = [](const char* label, int parameter) {
        auto result = Node(0, label, A({
            Key(0, 3, O({{"src", S("src/images/face")}, {"coord", A({N(-20), N(0), N(0)})}})),
            Key(40, 2, O({{"src", S("src/images/face")}, {"coord", A({N(20), N(0), N(0)})}})),
            Key(41, 0)}));
        result.object["parameterize"] = N(parameter);
        return result;
    };
    auto& motion = doc.root.object["object"].object["actor"].object["motion"].object["idle"];
    motion.object["layer"] = A({node("left", 0), node("right", 1)});
    motion.object["parameter"] = A({
        O({{"id", S("leftEye")}, {"rangeBegin", N(-20)}, {"rangeEnd", N(20)}, {"division", N(40)}}),
        O({{"id", S("rightEye")}, {"rangeBegin", N(-20)}, {"rangeEnd", N(20)}, {"division", N(40)}})});
    auto& image = doc.root.object["source"].object["images"].object["icon"].object["face"];
    image.object["originX"] = N(0);
    image.object["originY"] = N(0);
    return doc;
}
double RightEye(EmotePlayer& player) {
    artc::Compositor compositor;
    std::string error;
    Check(player.Render(compositor, "eyes", error), "render: " + error);
    const auto info = compositor.GetLayerInfo("eyes.000002.000000");
    Check(info.found, "right eye installed");
    return info.left;
}

void CompositionTests() {
    for (const bool immediate : {false, true}) {
        auto doc = PairDoc();
        if (immediate) SetMeta(doc, "immediateControl", Arr({Named("rightEye")}));
        AddTimeline(doc, "priv", -1, 0, 40, false, {Track("rightEye", {{0, 2}, {40, 2}})});
        auto player = Make(std::move(doc), immediate ? "compose immediate" : "compose");
        if (!player) return;
        const std::string tag = immediate ? " (immediate base)" : " (ordinary base)";
        std::string error;
        Check(Set(*player, "rightEye", 3, 0), "base 3");
        Check(player->PlayTimeline("priv", 3, error), "private difference timeline");
        Check(player->SetTimelineBlendRatio("priv", 0.5, error), "blend 0.5");
        Check(player->SetVariableDiff("leftEye", "rightEye", 4, 0, 0, error), "pair 4");
        Check(Near(RightEye(*player), 8, 2e-5), "base 3 + private 2*0.5 + pair 4 = 8" + tag);
        Check(Get(*player, "rightEye") == 3, "base getter stays 3" + tag);
        player->Progress(Ms(10));
        Check(Near(RightEye(*player), 8, 2e-5), "sum stable while the timeline runs" + tag);
    }
}
}  // namespace

int main() {
    TransitionTests();
    ImmediateTests();
    DefaultMatrixTests();
    InstantIndependenceTests();
    RoutingTests();
    ProfileTests();
    ClampTests();
    LargeMetadataTests();
    OrbitTests();
    CompositionTests();
    if (g_failures) {
        std::cerr << g_failures << " emote control player regression failure(s)\n";
        return 1;
    }
    std::cout << "emote control player regressions passed\n";
    return 0;
}
