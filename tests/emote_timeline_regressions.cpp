// emote_timeline_regressions.cpp — timeline sampling and parallel blink clocks.

#include "emote_scene_fixture.h"
#include "render/emote_model.h"
#include "render/emote_player.h"
#include "render/compositor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace {
int g_failures = 0;
void Check(bool ok, const std::string &what) {
    if (!ok) { ++g_failures; std::cerr << "FAIL: " << what << "\n"; }
}
const double *Find(const std::map<std::string, double> &v, const std::string &k) {
    const auto it = v.find(k);
    return it == v.end() ? nullptr : &it->second;
}

struct EyeClock {
    double duration, delay, max_delay;
    bool enabled = true, blink_enabled = true;
    double begin = 0, end = 10;
};

std::shared_ptr<artc::EmoteModel> BlinkModel(EyeClock left, EyeClock right) {
    using namespace emote_fixture;
    auto doc = PlayerDocument();
    auto& metadata = doc.root.object["metadata"].object;
    metadata["variableList"] = A({O({{"label", S("expression")}}),
        O({{"label", S("leftEye")}}), O({{"label", S("rightEye")}})});
    const auto eye = [](const char* label, EyeClock clock) {
        return O({{"label", S(label)}, {"beginFrame", N(clock.begin)}, {"endFrame", N(clock.end)},
            {"blinkFrameCount", N(clock.duration)}, {"blinkIntervalMin", N(clock.delay)},
            {"blinkIntervalMax", N(clock.max_delay)}, {"enabled", N(clock.enabled)},
            {"blinkEnabled", N(clock.blink_enabled)}});
    };
    metadata["eyeControl"] = A({eye("leftEye", left), eye("rightEye", right)});
    // Each eye variable moves a synthetic image from x=0 to x=10. Observe the
    // composed values through Render, not GetVariable (which reads script state).
    auto& motion = doc.root.object["object"].object["actor"].object["motion"].object["idle"];
    const auto node = [](const char* label, int parameter) {
        auto result = Node(0, label, A({
            Key(0, 3, O({{"src", S("src/images/face")}, {"coord", A({N(0), N(0), N(0)})}})),
            Key(10, 2, O({{"src", S("src/images/face")}, {"coord", A({N(10), N(0), N(0)})}})),
            Key(11, 0)}));
        result.object["parameterize"] = N(parameter);
        return result;
    };
    motion.object["layer"] = A({node("left", 0), node("right", 1)});
    motion.object["parameter"] = A({
        O({{"id", S("leftEye")}, {"rangeBegin", N(0)}, {"rangeEnd", N(10)}, {"division", N(10)}}),
        O({{"id", S("rightEye")}, {"rangeBegin", N(0)}, {"rangeEnd", N(10)}, {"division", N(10)}})});
    auto& image = doc.root.object["source"].object["images"].object["icon"].object["face"];
    image.object["originX"] = N(0);
    image.object["originY"] = N(0);
    auto model = std::make_shared<artc::EmoteModel>();
    std::string error;
    Check(model->Load(std::move(doc), error), "load synthetic eye model: " + error);
    return model;
}

std::pair<double, double> ObserveEyes(artc::EmotePlayer& player) {
    artc::Compositor compositor;
    std::string error;
    Check(player.Render(compositor, "eyes", error), "render synthetic eyes: " + error);
    const auto left = compositor.GetLayerInfo("eyes.000001.000000");
    const auto right = compositor.GetLayerInfo("eyes.000002.000000");
    Check(left.found && right.found, "both parameterized eye pictures installed");
    return {left.left, right.left};
}

void CheckEyes(artc::EmotePlayer& player, double left, double right, const std::string& what) {
    const auto values = ObserveEyes(player);
    Check(std::abs(values.first - left) < 2e-5 && std::abs(values.second - right) < 2e-5,
          what + " (actual " + std::to_string(values.first) + ", " +
          std::to_string(values.second) + ")");
}

void AdvanceFrames(artc::EmotePlayer& player, double frames) {
    player.Progress(frames / artc::EmotePlayer::kFramesPerMillisecond);
}

artc::PsbDocument PairDocument() {
    using namespace emote_fixture;
    auto doc = PlayerDocument();
    auto& metadata = doc.root.object["metadata"].object;
    metadata["variableList"] = A({O({{"label", S("expression")}}),
        O({{"label", S("leftEye")}}), O({{"label", S("rightEye")}})});
    auto& motion = doc.root.object["object"].object["actor"].object["motion"].object["idle"];
    const auto node = [](const char* label, int parameter) {
        auto result = Node(0, label, A({
            Key(0, 3, O({{"src", S("src/images/face")}, {"coord", A({N(-20), N(0), N(0)})}})),
            Key(40, 2, O({{"src", S("src/images/face")}, {"coord", A({N(20), N(0), N(0)})}})),
            Key(41, 0)}));
        result.object["parameterize"] = N(parameter);
        return result;
    };
    motion.object["layer"] = A({node("left", 0), node("right", 1)});
    motion.object["parameter"] = A({
        O({{"id", S("leftEye")}, {"rangeBegin", N(-20)}, {"rangeEnd", N(20)}, {"division", N(40)}}),
        O({{"id", S("rightEye")}, {"rangeBegin", N(-20)}, {"rangeEnd", N(20)}, {"division", N(40)}})});
    auto& image = doc.root.object["source"].object["images"].object["icon"].object["face"];
    image.object["originX"] = N(0);
    image.object["originY"] = N(0);
    return doc;
}

void PairCompositionTests() {
    auto model = std::make_shared<artc::EmoteModel>();
    std::string error;
    Check(model->Load(PairDocument(), error), "load ordinary pair fixture: " + error);
    artc::EmotePlayer player;
    Check(player.Load(model, error), "load ordinary pair player");
    Check(player.SetVariable("leftEye", 3, 0, 0, error) &&
          player.SetVariable("rightEye", 2, 0, 0, error), "nonzero pair base values");
    Check(player.SetVariableDiff("leftEye", "rightEye", 4, 0, 0, error), "set ordered pair");
    CheckEyes(player, 3, 6, "pair adds only to its second label, without mutating bases");
    bool found = false;
    Check(player.GetVariable("leftEye", &found) == 3 && found &&
          player.GetVariable("rightEye", &found) == 2 && found, "base getters are independent of pair controls");
    Check(player.SetVariableDiff("rightEye", "leftEye", -5, 0, 0, error), "set reversed negative pair");
    CheckEyes(player, -2, 6, "reverse pair is independent and negative values are not clamped");
    Check(player.SetVariableDiff("expression", "rightEye", -1, 0, 0, error), "another first key sharing second");
    CheckEyes(player, -2, 5, "all distinct pairs with the same second key accumulate");
    Check(player.GetVariableDiff("leftEye", "rightEye", &found) == 4 && found &&
          player.GetVariableDiff("rightEye", "leftEye", &found) == -5 && found &&
          player.GetVariableDiff("expression", "rightEye", &found) == -1 && found,
          "pair getters return each independent current, not the composed variable");
    Check(player.GetVariableDiff("leftEye", "leftEye", &found) == 0 && !found &&
          player.GetVariableDiff("missing", "rightEye", &found) == 0 && !found,
          "missing ordered pairs read zero without inserting controls");
    Check(player.SetVariableDiff("leftEye", "leftEye", 1, 0, 0, error), "same-label ordered pair is valid");
    CheckEyes(player, -1, 5, "same-label pair adds once rather than cancelling itself");

    Check(player.Load(model, error), "reload clears all pair controls");
    Check(player.GetVariableDiff("leftEye", "rightEye", &found) == 0 && !found &&
          !player.IsAnimating(), "reload pair getter is absent, idle and zero");
    Check(player.SetVariable("rightEye", 3, 0, 0, error) &&
          player.SetVariableDiff("leftEye", "rightEye", 8, 1000, 1, error), "start eased pair transition");
    Check(player.GetVariableDiff("leftEye", "rightEye", &found) == 0 && found && player.IsAnimating(),
          "new pair transition begins at zero and participates in IsAnimating");
    player.Progress(500);
    Check(player.GetVariableDiff("leftEye", "rightEye") == 2, "positive ease squares the half-time ratio");
    CheckEyes(player, 0, 5, "pair transition renders over nonzero base");
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        Check(!player.SetVariableDiff("leftEye", "rightEye", invalid, 1000, 0, error) &&
              !player.SetVariableDiff("leftEye", "rightEye", 7, invalid, 0, error) &&
              !player.SetVariableDiff("leftEye", "rightEye", 7, 1000, invalid, error),
              "nonfinite pair arguments reject before replacing transition");
        Check(!player.SetVariableDiff("expression", "rightEye", invalid, 1000, 0, error) &&
              player.GetVariableDiff("expression", "rightEye", &found) == 0 && !found,
              "invalid new pair does not insert a controller");
        player.Progress(invalid);
        Check(player.GetVariableDiff("leftEye", "rightEye") == 2, "nonfinite Progress preserves pair current");
    }
    Check(!player.SetVariableDiff("missing", "rightEye", 1, 0, 0, error) &&
          !player.SetVariableDiff("leftEye", "missing", 1, 0, 0, error), "unknown labels reject without mutation");
    Check(!player.Load(nullptr, error) && player.GetVariableDiff("leftEye", "rightEye") == 2,
          "failed reload preserves pair state");
    player.Progress(250);
    Check(std::abs(player.GetVariableDiff("leftEye", "rightEye") - 4.5) < 1e-12,
          "rejected calls preserve original ease and transition elapsed time");
    Check(player.SetVariableDiff("leftEye", "rightEye", -3.5, 1000, -1, error), "retarget existing pair from its current");
    player.Progress(250);
    Check(std::abs(player.GetVariableDiff("leftEye", "rightEye") - 0.5) < 1e-12,
          "negative ease uses square root and retarget begins at current");
    const double frame = player.BaseFrame();
    player.Step();
    Check(player.GetVariableDiff("leftEye", "rightEye") == -3.5 && !player.IsAnimating() &&
          player.BaseFrame() == frame, "Step completes pair transition without advancing base clock");
    CheckEyes(player, 0, -0.5, "Step preserves signed pair target and base");
    Check(player.SetVariableDiff("leftEye", "rightEye", 6, 1000, 0, error), "restart pair for Skip");
    player.Skip();
    Check(player.GetVariableDiff("leftEye", "rightEye") == 6 && !player.IsAnimating(), "Skip completes pair transition");
    CheckEyes(player, 0, 9, "Skip completed pair composes over base");
    player.Step();
    Check(player.GetVariableDiff("leftEye", "rightEye") == 6, "repeated Finish does not undo instant/completed controls");

    // E3 main tracks drive base setters; bit1 private tracks add weighted
    // differences. E2 pair controls accumulate after that composition.
    using namespace emote_fixture;
    auto timeline_doc = PairDocument();
    auto& timelines = timeline_doc.root.object["metadata"].object["timelineControl"].array;
    for (auto& timeline : timelines) {
        auto& track = timeline.object["variableList"].array[0];
        track.object["label"] = S("leftEye");
        for (auto& key : track.object["frameList"].array)
            key.object["content"].object["value"] = N(timeline.object["diff"].number ? 2 : 6);
    }
    auto timeline_model = std::make_shared<artc::EmoteModel>();
    Check(timeline_model->Load(timeline_doc, error) && player.Load(timeline_model, error), "load timeline/pair composition");
    Check(player.SetVariable("leftEye", 3, 0, 0, error) &&
          player.PlayTimeline("once", 1, error) && player.SetTimelineBlendRatio("once", 0.5, error) &&
          player.PlayTimeline("delta", 3, error) && player.SetTimelineBlendRatio("delta", 0.25, error) &&
          player.SetVariableDiff("rightEye", "leftEye", 1, 0, 0, error), "configure base/main/diff/pair layers");
    CheckEyes(player, 7.5, 0, "main setter base 6 plus private diff .5 plus pair 1");
    Check(player.GetVariable("leftEye", &found) == 6 && found &&
          player.GetVariableDiff("rightEye", "leftEye") == 1,
          "main setter updates base while private diff and pair getter remain independent");
}

void MirrorMatchMetadataTests() {
    using namespace emote_fixture;
    auto document = PairDocument();
    auto& metadata = document.root.object["metadata"].object;
    metadata["mirrorControl"] = O({{"variableMatchList", A({S("leftEye"), S("leftEye"), S("*Eye"), S("")})}});
    metadata["stereoControl"] = O({{"variableMatchList", A({S("rightEye")})}});
    artc::EmoteModel model;
    std::string error;
    Check(model.Load(document, error) && model.MirrorVariableMatches() ==
          std::vector<std::string>({"leftEye", "leftEye", "*Eye", ""}),
          "mirror strings preserved exactly, independently of stereo, without invented matching rules");
    auto player_model = std::make_shared<artc::EmoteModel>(model);
    artc::EmotePlayer player;
    Check(player.Load(player_model, error) && player.SetVariableDiff("leftEye", "rightEye", 2, 0, 0, error),
          "mirror match list is not a pair whitelist");
    for (const auto& malformed : {N(1), S("leftEye"), A({S("leftEye"), N(2)})}) {
        metadata["mirrorControl"].object["variableMatchList"] = malformed;
        Check(!model.Load(document, error) && model.MirrorVariableMatches() == player_model->MirrorVariableMatches(),
              "malformed mirror list is rejected transactionally");
    }
    Check(model.Load(PairDocument(), error) && model.MirrorVariableMatches().empty(), "reload resets mirror match metadata");
}

void NativeTimelineContractTests() {
    auto model = std::make_shared<artc::EmoteModel>();
    std::string error;
    Check(model->Load(PairDocument(), error), "load E3 normal fixture");
    artc::EmotePlayer player;
    Check(player.Load(model, error), "load E3 player");
    Check(player.PlayTimeline("once", 1, error) && player.PlayTimeline("loop", 1, error), "start parallel timelines");
    Check(player.PlayTimeline("delta", 0, error) && player.CountPlayingTimelines() == 1 &&
          !player.IsTimelinePlaying("once") && !player.IsTimelinePlaying("loop"), "flags zero clears all previous timelines");
    Check(player.PlayTimeline("once", 2, error) && player.IsTimelinePlaying("once") &&
          player.CountPlayingTimelines() == 1, "bit1 starts private diff immediately, not sequentially");
    const double frame = player.BaseFrame();
    player.Step();
    Check(player.BaseFrame() == frame && player.CountPlayingTimelines() == 0,
          "Step forwards/removes nonloop without advancing base clock");
    player.SetCoord(10, 20, 1000, 0);
    player.Pass();
    double x, y;
    player.GetCoord(&x, &y);
    Check(x == 0 && y == 0 && player.IsAnimating(), "Pass does not finish global transitions");
    player.StopTimeline("");
    Check(!player.IsTimelinePlaying(""), "empty playing query observes whole set");
}

std::shared_ptr<artc::EmoteModel> ControllerModel(bool instant = false) {
    using namespace emote_fixture;
    auto document = PairDocument();
    auto& metadata = document.root.object["metadata"].object;
    const auto key = [](double time, double value, double ease = 0) {
        return Key(time, 2, O({{"value", N(value)}, {"easing", N(ease)}}));
    };
    const auto track = [&](const char* label) {
        return O({{"label", S(label)}, {"frameList", A({key(0, 0), key(10, 10),
                    key(20, 4, 1), Key(21, 0)})}});
    };
    const auto timeline = [&](const char* label, double begin, double end) {
        return O({{"label", S(label)}, {"lastTime", N(20)}, {"loopBegin", N(begin)},
                  {"loopEnd", N(end)}, {"diff", N(0)},
                  {"variableList", A({track("leftEye"), track("rightEye")})}});
    };
    metadata["timelineControl"] = A({timeline("main", -1, -1), timeline("other", -1, -1),
                                       timeline("loop", 0, 20)});
    if (instant) metadata["instantVariableList"] = A({S("rightEye")});
    auto model = std::make_shared<artc::EmoteModel>();
    std::string error;
    Check(model->Load(document, error), "load controller fixture: " + error);
    return model;
}

void PrivateTimelineTests() {
    std::string error;
    auto model = ControllerModel(true);
    artc::EmotePlayer player;
    Check(player.Load(model, error) && player.SetVariable("leftEye", 3, 0, 0, error) &&
          player.SetVariable("rightEye", 2, 0, 0, error), "initialize private vs instant bases");
    Check(player.PlayTimeline("main", 3, error) && player.IsTimelinePlaying(""), "start private timeline");
    AdvanceFrames(player, 5);
    CheckEyes(player, 8, 0, "bit1 noninstant control adds to base while instant track uses setter");
    bool found = false;
    Check(player.GetVariable("leftEye", &found) == 3 && found &&
          player.GetVariable("rightEye", &found) == 0 && found, "private tracks do not mutate base getter");
    Check(player.SetTimelineBlendRatio("main", 0.5, error), "set private blend");
    CheckEyes(player, 5.5, 0, "private contribution multiplied by blend only once");
    AdvanceFrames(player, 5);
    CheckEyes(player, 8, 10, "instant command at inclusive key boundary bypasses private blend");
    AdvanceFrames(player, 5);
    CheckEyes(player, 7.25, 10, "private second segment uses authored nonlinear ease");
    Check(player.PlayTimeline("other", 3, error) && player.CountPlayingTimelines() == 2,
          "private timelines run concurrently, never wait in FIFO");
    CheckEyes(player, 7.25, 0, "second private controller starts at zero independently");
    AdvanceFrames(player, 2);
    CheckEyes(player, 8.53, 0, "independent private timelines accumulate by track over base");
    Check(player.PlayTimeline("other", 3, error), "restart private timeline");
    CheckEyes(player, 6.53, 0, "restart resets only that timeline's private controls");
    const auto base = player.GetVariable("leftEye", nullptr);
    Check(!player.PlayTimeline("missing", 0, error) && player.CountPlayingTimelines() == 2 &&
          player.GetVariable("leftEye", nullptr) == base, "unknown flags0 play rejects atomically before clearing");
    player.StopTimeline("");
    Check(player.CountPlayingTimelines() == 0 && !player.IsTimelinePlaying(""), "empty stop clears complete playing set");
    CheckEyes(player, 3, 0, "stopped private contributions disappear without changing base");
    Check(player.PlayTimeline("main", 1, error), "start ordinary timeline independent of metadata.diff");
    AdvanceFrames(player, 5);
    CheckEyes(player, 5, 0, "ordinary track controls base directly");
    Check(player.GetVariable("leftEye", nullptr) == 5, "main track observed by base getter");
    Check(player.PlayTimeline("other", 2, error) && player.CountPlayingTimelines() == 1 &&
          !player.IsTimelinePlaying("main"), "bit1 plus clear bit0 clears old entries and starts private immediately");
    CheckEyes(player, 5, 0, "private start preserves ordinary base and initializes differential zero");
    Check(player.FadeInTimeline("main", 1000, 0, error) && player.CountPlayingTimelines() == 2 &&
          player.PlayingTimelineFlagsAt(1) == 3, "FadeIn starts with official flags3");
}

void StepSkipPassTests() {
    std::string error;
    auto model = ControllerModel(true);
    artc::EmotePlayer player;
    Check(player.Load(model, error), "load Step/Skip/Pass fixture");
    Check(player.PlayTimeline("main", 1, error), "start nonloop for Step");
    AdvanceFrames(player, 5);
    const auto clock = player.BaseFrame();
    player.SetCoord(10, 20, 1000, 0);
    Check(player.SetVariableDiff("expression", "leftEye", 7, 1000, 0, error), "start pair before Step");
    player.Step();
    double x, y;
    player.GetCoord(&x, &y);
    Check(player.BaseFrame() == clock && player.CountPlayingTimelines() == 0 && x == 10 && y == 20 &&
          player.GetVariable("leftEye", nullptr) == 4 && player.GetVariable("rightEye", nullptr) == 4 &&
          player.GetVariableDiff("expression", "leftEye") == 7,
          "Step forwards inclusive final keys, removes nonloop and epSkips globals, not baseclock");

    Check(player.Load(model, error) && player.PlayTimeline("loop", 3, error), "start private loop for first Step");
    AdvanceFrames(player, 5);
    const double loop_clock = player.BaseFrame();
    Check(player.SetTimelineBlendRatio("loop", 0.5, 1000, 0, false, error), "start blend before loop Step");
    player.Step();
    Check(player.BaseFrame() == loop_clock && player.CountPlayingTimelines() == 1 &&
          player.TimelineBlendRatio("loop", nullptr) == 0.5, "Step retains loop and finishes blend without ticking clock");
    CheckEyes(player, 5, 0, "first loop Step excludes end key, cues inclusive begin, advances private control by end-oldpos");
    player.Step();
    CheckEyes(player, 5, 0, "second loop Step does not repeat the loop crossing");
    Check(player.FadeOutTimeline("loop", 1000, 0, error), "mark loop fade out");
    player.Step();
    Check(player.CountPlayingTimelines() == 0 && player.BaseFrame() == loop_clock &&
          player.GetVariable("rightEye", nullptr) == 4, "Step fade_out_stop loop forwards final inclusive then removes");

    Check(player.Load(model, error) && player.PlayTimeline("main", 1, error) &&
          player.PlayTimeline("loop", 3, error), "start nonloop plus loop for Skip");
    AdvanceFrames(player, 5);
    const double skip_clock = player.BaseFrame();
    player.Skip();
    Check(!player.IsTimelinePlaying("main") && player.IsTimelinePlaying("loop") &&
          player.CountPlayingTimelines() == 1 && player.BaseFrame() == skip_clock,
          "Skip removes nonloop but keeps loop and its clock");
    CheckEyes(player, 9, 4, "Skip final main base plus unchanged private loop contribution");

    Check(player.Load(model, error) && player.SetVariable("leftEye", 3, 0, 0, error) &&
          player.PlayTimeline("main", 3, error), "start private nonloop for Pass fade");
    AdvanceFrames(player, 5);
    player.SetCoord(10, 20, 1000, 0);
    Check(player.SetVariableDiff("expression", "leftEye", 6, 1000, 0, error), "global pair before Pass");
    player.Pass();
    player.GetCoord(&x, &y);
    Check(x == 0 && y == 0 && player.GetVariableDiff("expression", "leftEye") == 0 &&
          player.TimelineBlendRatio("main", nullptr) == 1, "Pass leaves global transitions and starts rather than finishes fade");
    AdvanceFrames(player, 10);
    Check(std::abs(player.TimelineBlendRatio("main", nullptr) - 0.5) < 1e-12 &&
          player.GetVariableDiff("expression", "leftEye") > 0 && player.IsTimelinePlaying("main"),
          "Pass private fade is 20 frames (not 20ms), global pair continues normally");
    CheckEyes(player, 8.25, 1, "Pass repeat guard does not freeze private track clock during fade");
    player.Pass();
    AdvanceFrames(player, 10);
    Check(!player.IsTimelinePlaying("main") && player.GetVariable("leftEye", nullptr) == 3,
          "repeat Pass does not reset 20-frame fade; private base stays intact");
    player.GetCoord(&x, &y);
    Check(std::abs(x - 10.0 / 3) < 1e-10 && std::abs(y - 20.0 / 3) < 1e-10,
          "Pass does not epSkip or cancel global coord transitions");
    Check(player.GetVariable("rightEye", nullptr) > 0,
          "bit1 Pass still submits instant track future commands through ordinary setter");

    Check(player.Load(model, error) && player.PlayTimeline("loop", 3, error), "start loop before Pass no-op");
    AdvanceFrames(player, 5);
    const auto before = ObserveEyes(player);
    player.Pass();
    CheckEyes(player, before.first, before.second, "Pass skips looping timeline entirely");
    Check(player.TimelineBlendRatio("loop", nullptr) == 1 && player.IsTimelinePlaying("loop"), "Pass does not fade a loop");
}

void AnimatedQueuingTests() {
    std::string error;
    auto model = ControllerModel();
    artc::EmotePlayer player;
    Check(player.Load(model, error) && !player.GetQueuing(), "queuing defaults false");
    player.SetQueuing(true);
    Check(player.SetVariable("leftEye", 10, 1000, 0, error) &&
          player.SetVariable("leftEye", -2, 1000, 0, error), "append two base commands");
    Check(player.SetVariableDiff("expression", "rightEye", 8, 1000, 0, error) &&
          player.SetVariableDiff("expression", "rightEye", -4, 1000, 0, error), "append two pair commands");
    player.SetCoord(10, 20, 1000, 0);
    player.SetCoord(-2, -4, 1000, 0);
    player.Progress(1500);
    double x, y;
    player.GetCoord(&x, &y);
    Check(player.GetVariable("leftEye", nullptr) == 4 && player.GetVariableDiff("expression", "rightEye") == 2 &&
          x == 4 && y == 8 && player.IsAnimating(), "elapsed delta crosses per-channel command boundary without discarding remainder");
    player.Pass();
    Check(player.GetVariable("leftEye", nullptr) == 4 && player.GetVariableDiff("expression", "rightEye") == 2,
          "Pass with no timelines leaves queued global commands untouched");
    player.Skip();
    player.GetCoord(&x, &y);
    Check(player.GetVariable("leftEye", nullptr) == -2 && player.GetVariableDiff("expression", "rightEye") == -4 &&
          x == -2 && y == -4 && !player.IsAnimating(), "Skip selects final queued target for all supported global controls");
    Check(player.SetVariable("leftEye", 5, 1000, 0, error) &&
          player.SetVariable("leftEye", 9, 1000, 0, error), "new queued commands before immediate override");
    Check(player.SetVariable("leftEye", -3, 0, 0, error), "nonpositive duration immediately clears queue");
    player.Step();
    Check(player.GetVariable("leftEye", nullptr) == -3 && !player.IsAnimating(), "Step cannot resurrect cleared queued commands");
    Check(player.SetVariable("leftEye", 10, 1000, 0, error) &&
          player.SetVariable("leftEye", 20, 1000, 0, error), "queue commands before replacing");
    player.SetQueuing(false);
    Check(player.SetVariable("leftEye", 6, 1000, 0, error), "queuing=false replaces active and pending commands");
    player.Progress(500);
    Check(player.GetVariable("leftEye", nullptr) == 1.5, "replacement transition starts at current value");
    player.Step();
    Check(player.GetVariable("leftEye", nullptr) == 6, "Step finishes replacement rather than old queue tail");

    player.SetQueuing(true);
    Check(player.SetVariable("leftEye", 10, 1000, 0, error) &&
          player.SetVariable("leftEye", 12, 1000, 0, error), "start queue for atomic validation");
    for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        Check(!player.SetVariable("leftEye", bad, 1000, 0, error) &&
              !player.SetVariable("leftEye", 7, bad, 0, error) &&
              !player.SetVariable("leftEye", 7, 1000, bad, error), "nonfinite commands reject atomically");
    }
    Check(!player.SetVariable("missing", 99, 1000, 0, error), "unknown variable does not mutate queue");
    Check(!player.Load(nullptr, error) && player.GetQueuing(), "failed Load preserves queued mode and active controls");
    player.Step();
    Check(player.GetVariable("leftEye", nullptr) == 12, "invalid commands did not overwrite queue's final target");
    Check(player.Load(model, error) && !player.GetQueuing() && !player.IsAnimating() &&
          player.GetVariable("leftEye", nullptr) == 0, "Load resets queuing flag and all commands");

    // Pass uses remaining authored record.time as the command duration and
    // obeys Animated queuing, not a whole-timeline queue. At frame5 the next
    // records are 10 and 20; type0 terminal is never submitted as a value.
    for (bool queue : {false, true}) {
        Check(player.Load(model, error) && player.PlayTimeline("main", 1, error), "start main before Pass command test");
        AdvanceFrames(player, 5);
        player.SetQueuing(queue);
        player.Pass();
        Check(player.CountPlayingTimelines() == 0 && player.GetVariable("leftEye", nullptr) == 5,
              "main Pass removes playback without instantly applying future keys");
        AdvanceFrames(player, 5);
        Check(std::abs(player.GetVariable("leftEye", nullptr) - (queue ? 10 : 4.9375)) < 1e-12,
              "Pass commands append to current segment or replace using record.time/easing");
        player.Step();
        Check(player.GetVariable("leftEye", nullptr) == 4, "Step resolves Pass command queue to final nonterminal value");
    }

    Check(player.Load(model, error) && player.PlayTimeline("loop", 3, error), "start blend queue fixture");
    player.SetQueuing(true);
    Check(player.SetTimelineBlendRatio("loop", 0.5, 1000, 0, false, error) &&
          player.SetTimelineBlendRatio("loop", 0.25, 1000, 0, false, error), "blend commands use independent queuing flag");
    player.SetScale(2, 3, 1000, 0);
    player.SetScale(4, 5, 1000, 0);
    player.SetRot(30, 1000, 0);
    player.SetRot(90, 1000, 0);
    player.SetColor(0x80808080, 1000, 0);
    player.SetColor(0x80808040, 1000, 0);
    player.SetGrayscale(0.5, 1000, 0);
    player.SetGrayscale(1, 1000, 0);
    player.Progress(1500);
    double sx, sy;
    player.GetScale(&sx, &sy);
    Check(std::abs(player.TimelineBlendRatio("loop", nullptr) - 0.375) < 1e-12 &&
          std::abs(sx - 3) < 1e-12 && std::abs(sy - 4) < 1e-12 &&
          std::abs(player.GetRot() - 60) < 1e-12 && std::abs(player.GetGrayscale() - 0.75) < 1e-12 &&
          (player.GetColor() & 255) == 96, "blend, scale, rotation, alpha and grayscale consume queued commands");
    player.Step();
    player.GetScale(&sx, &sy);
    Check(player.TimelineBlendRatio("loop", nullptr) == 0.25 && sx == 4 && sy == 5 &&
          player.GetRot() == 90 && player.GetGrayscale() == 1 && (player.GetColor() & 255) == 64,
          "Step epSkip covers every supported Animated channel and final queued target");

    Check(player.Load(model, error) && player.PlayTimeline("loop", 3, error), "load invalid blend comparison");
    Check(player.SetTimelineBlendRatio("loop", 0.5, 1000, 0, true, error), "start valid fade state");
    for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        Check(!player.SetTimelineBlendRatio("loop", bad, 1000, 0, false, error) &&
              !player.SetTimelineBlendRatio("loop", 0.7, bad, 0, false, error) &&
              !player.SetTimelineBlendRatio("loop", 0.7, 1000, bad, false, error) &&
              !player.FadeInTimeline("main", bad, 0, error) &&
              !player.FadeOutTimeline("loop", bad, 0, error), "nonfinite fades/blends reject before mutating controls or playback");
    }
    Check(player.CountPlayingTimelines() == 1 && player.IsTimelinePlaying("loop"), "invalid FadeIn does not insert a timeline");
    player.Step();
    Check(player.CountPlayingTimelines() == 0, "invalid blend did not cancel preexisting fade_out_stop flag");
}

void TimelinePartitionTests() {
    auto model = ControllerModel(true);
    std::string error;
    for (int flags : {1, 3}) for (const char* label : {"main", "loop"}) {
        artc::EmotePlayer large, split;
        for (auto* player : {&large, &split}) {
            Check(player->Load(model, error) && player->SetVariable("leftEye", 3, 0, 0, error) &&
                  player->PlayTimeline(label, flags, error), "load partitioned controller player");
        }
        AdvanceFrames(large, 57.25);
        for (int i = 0; i < 229; ++i) AdvanceFrames(split, 0.25);
        const auto expected = ObserveEyes(split);
        CheckEyes(large, expected.first, expected.second, "large timeline delta matches quarter-frame key/loop crossings");
        Check(large.CountPlayingTimelines() == split.CountPlayingTimelines() &&
              std::abs(large.GetVariable("leftEye", nullptr) - split.GetVariable("leftEye", nullptr)) < 1e-10 &&
              large.GetVariable("rightEye", nullptr) == split.GetVariable("rightEye", nullptr),
              "partitioning preserves timeline membership and base getter semantics");
        large.Step();
        split.Step();
        const auto after_step = ObserveEyes(split);
        CheckEyes(large, after_step.first, after_step.second, "natural loop crossing also marks Step's one-time crossing state");
    }
    artc::EmotePlayer large, split;
    for (auto* player : {&large, &split}) {
        Check(player->Load(model, error) && player->PlayTimeline("loop", 3, error) &&
              player->FadeOutTimeline("loop", 3.0 / artc::EmotePlayer::kFramesPerMillisecond, 0, error),
              "start short fade alongside instant track for partition test");
    }
    AdvanceFrames(large, 15);
    for (int i = 0; i < 60; ++i) AdvanceFrames(split, 0.25);
    Check(large.CountPlayingTimelines() == 0 && split.CountPlayingTimelines() == 0 &&
          large.GetVariable("rightEye", nullptr) == 0 && split.GetVariable("rightEye", nullptr) == 0,
          "fade removal boundary precedes future instant writes for both large and split deltas");
}

void TimelineClockIsolationTests() {
    std::string error;
    auto model = BlinkModel({10, 0, 0}, {20, 0, 0});
    artc::EmotePlayer player;
    Check(player.Load(model, error), "load blink/control clock isolation fixture");
    AdvanceFrames(player, 2);
    const auto eyes = ObserveEyes(player);
    const auto frame = player.BaseFrame();
    for (int i = 0; i < 3; ++i) {
        player.Step();
        player.Skip();
        player.Pass();
        CheckEyes(player, eyes.first, eyes.second, "Step/Skip/Pass do not tick or reset either blink controller");
        Check(player.BaseFrame() == frame, "control-only methods preserve scene clock");
    }
    AdvanceFrames(player, 1);
    CheckEyes(player, 7.5, 3.75, "blink clocks resume from unchanged phases after control operations");
}

void UnsupportedTimelineBoundaryTests() {
    using namespace emote_fixture;
    for (int kind = 0; kind < 3; ++kind) {
        auto document = PairDocument();
        auto& timeline = document.root.object["metadata"].object["timelineControl"].array[0];
        if (kind == 0) { timeline.object["loopBegin"] = N(-1); timeline.object["loopEnd"] = N(-1); }
        if (kind == 1) { timeline.object["loopBegin"] = N(0); timeline.object["loopEnd"] = N(0); }
        if (kind == 2) timeline.object["variableList"].array[0].object["label"] = S("missing");
        auto model = std::make_shared<artc::EmoteModel>();
        std::string error;
        Check(model->Load(document, error), "load offline timeline boundary fixture");
        artc::EmotePlayer player;
        Check(player.Load(model, error) && player.PlayTimeline("once", 1, error), "start valid playback before unsupported call");
        if (kind == 0) {
            // Open-ended nonloop (lastTime -1, no loop; every shipped expression
            // timeline) degrades: it plays until stopped and is reported.
            Check(model->UnsupportedFeatures().Count("timeline.openEnded") == 1 &&
                  player.UnsupportedFeatures().Has("timeline.openEnded"),
                  "open-ended nonloop recorded as unsupported feature");
            Check(player.PlayTimeline("delta", 0, error) && player.IsTimelinePlaying("delta") &&
                  !player.IsTimelinePlaying("once") && !player.IsLoopTimeline("delta"),
                  "open-ended nonloop plays (flag0 clears the previous timeline)");
            for (int i = 0; i < 40; ++i) player.Progress(1000.0 / 60.0);
            Check(player.IsTimelinePlaying("delta"), "open-ended nonloop never ends on its own");
            player.Skip();
            Check(!player.IsTimelinePlaying("delta"), "Skip forwards and removes an open-ended nonloop");
            Check(player.PlayTimeline("delta", 1, error) && player.FadeInTimeline("delta", 100, 0, error),
                  "FadeIn admits the open-ended nonloop");
            continue;
        }
        Check(!player.PlayTimeline("delta", 0, error) && !error.empty() &&
              player.CountPlayingTimelines() == 1 && player.IsTimelinePlaying("once"),
              "zero-width loop or unknown track explicitly fails before flags0 clearing");
        Check(!player.FadeInTimeline("delta", 100, 0, error) && player.CountPlayingTimelines() == 1,
              "FadeIn uses same transactional normal-path admission");
    }
}

double FixedEyeAt(double time, double duration, double delay) {
    if (time < delay) return 0;
    const double phase = std::fmod(time - delay, duration + delay);
    if (phase < duration * 0.4) return 10 * phase / (duration * 0.4);
    if (phase < duration * 0.6) return 10;
    if (phase < duration) return 10 * (duration - phase) / (duration * 0.4);
    return 0;
}

void BlinkClockTests() {
    std::string error;
    artc::EmotePlayer player;
    auto synchronous = BlinkModel({10, 3, 3}, {10, 3, 3});
    Check(player.Load(synchronous, error), "load synchronous controllers");
    CheckEyes(player, 0, 0, "controllers initially idle");
    Check(!player.IsAnimating(), "idle blink waits do not hold animation waits");
    AdvanceFrames(player, 5);
    CheckEyes(player, 5, 5, "different variables receive the same closing time");
    AdvanceFrames(player, 3);
    CheckEyes(player, 10, 10, "40 percent closing followed by 20 percent hold");
    Check(!player.IsAnimating(), "active blinks do not hold animation waits");
    AdvanceFrames(player, 3);
    CheckEyes(player, 5, 5, "40 percent opening advances both controllers");
    AdvanceFrames(player, 2);
    CheckEyes(player, 0, 0, "both controllers finish their cycle");

    const EyeClock left{10, 3, 3}, right{20, 7, 7};
    auto independent = BlinkModel(left, right);
    Check(player.Load(independent, error), "load independent durations and delays");
    double time = 0;
    for (double dt : {5.0, 4.0, 2.0, 4.0, 3.0, 7.0, 2.0}) {
        time += dt;
        AdvanceFrames(player, dt);
        CheckEyes(player, FixedEyeAt(time, left.duration, left.delay),
                  FixedEyeAt(time, right.duration, right.delay), "independent clocks at " + std::to_string(time));
    }

    // Exercise many more than eight phase transitions, including zero waits,
    // model-normalized sub-frame durations and randomized (seeded) intervals.
    const std::pair<EyeClock, EyeClock> clocks[] = {
        {{10, 3, 3}, {20, 7, 7}}, {{1, 0, 0}, {2, 0, 0}},
        {{4, 10, 10}, {3, 0, 0}}, {{0, 0, 0}, {0.25, 0, 0}},
        {{1, 0, 2}, {7, 2, 9}}
    };
    for (const auto& clock : clocks) {
        auto model = BlinkModel(clock.first, clock.second);
        for (double total : {123.25, 3599.75, 3600.0}) {
            artc::EmotePlayer large, split;
            Check(large.Load(model, error) && split.Load(model, error), "load large/split players");
            AdvanceFrames(large, total);
            double remaining = total;
            while (remaining > 0) {
                const double dt = std::min(0.25, remaining);
                AdvanceFrames(split, dt);
                remaining -= dt;
            }
            auto expected = ObserveEyes(split);
            CheckEyes(large, expected.first, expected.second, "large step equals quarter-frame partitions");
            if (clock.first.delay == clock.first.max_delay &&
                clock.second.delay == clock.second.max_delay) {
                CheckEyes(large,
                    FixedEyeAt(total, std::max(1.0, clock.first.duration), clock.first.delay),
                    FixedEyeAt(total, std::max(1.0, clock.second.duration), clock.second.delay),
                    "large step matches analytical fixed-interval phase");
            }
            // Compare the future too: matching open eyes must not hide lost
            // wait time, a wrong phase, or a different RNG position.
            for (int i = 0; i < 17; ++i) {
                AdvanceFrames(large, 0.375);
                AdvanceFrames(split, 0.375);
                expected = ObserveEyes(split);
                CheckEyes(large, expected.first, expected.second, "large/split clocks remain synchronized");
            }
        }
    }

    auto zero_wait = BlinkModel({10, 0, 0}, {10, 0, 0});
    Check(player.Load(zero_wait, error), "load zero interval model");
    Check(player.PlayTimeline("once", 0, error), "start finite animation alongside blinks");
    Check(player.SetVariable("expression", 1, 1000, 0, error), "start variable animation");
    AdvanceFrames(player, 62);
    CheckEyes(player, 5, 5, "zero interval cycles consume full large step");
    Check(!player.IsTimelinePlaying("once") && !player.IsAnimating(),
          "finished timeline and transition are not suspended by active blinks");
    for (double invalid : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        player.Progress(invalid);
        CheckEyes(player, 5, 5, "invalid/nonpositive dt does not move blink clocks");
    }

    auto randomized = BlinkModel({10, 0, 5}, {20, 0, 9});
    artc::EmotePlayer capped, over_cap;
    Check(capped.Load(randomized, error) && over_cap.Load(randomized, error), "load cap comparison");
    capped.Progress(artc::EmotePlayer::kMaxProgressMs);
    over_cap.Progress(std::numeric_limits<double>::max());
    const auto capped_eyes = ObserveEyes(capped);
    CheckEyes(over_cap, capped_eyes.first, capped_eyes.second, "finite progress retains the documented 60s cap");
    Check(player.Load(randomized, error), "load randomized model");
    AdvanceFrames(player, 123.25);
    const auto before = ObserveEyes(player);
    Check(!player.Load(nullptr, error), "failed Load rejects null model");
    CheckEyes(player, before.first, before.second, "failed Load preserves blink state");
    Check(player.Load(randomized, error), "reload resets clocks and RNG");
    artc::EmotePlayer fresh;
    Check(fresh.Load(randomized, error), "fresh player for reload comparison");
    CheckEyes(player, 0, 0, "successful Load resets blink phases");
    for (int i = 0; i < 60; ++i) {
        AdvanceFrames(player, 0.75);
        AdvanceFrames(fresh, 0.75);
        const auto expected = ObserveEyes(fresh);
        CheckEyes(player, expected.first, expected.second, "Load resets both RNG streams and waits");
    }
    Check(player.Load(BlinkModel({10, 0, 0, false}, {10, 0, 0, true, false}), error),
          "load disabled controllers");
    AdvanceFrames(player, 2);
    CheckEyes(player, 0, 0, "either controller enable switch suppresses automatic blink");

    // The bounded-work argument relies on the real model loader: durations
    // are at least one frame and all timing fields must be finite.
    auto normalized = BlinkModel({-10, -1, -2}, {0.25, 0, 0});
    Check(normalized->Blinks().size() == 2 && normalized->Blinks()[0].frames == 1 &&
          normalized->Blinks()[0].interval_min == 0 && normalized->Blinks()[0].interval_max == 0 &&
          normalized->Blinks()[1].frames == 1, "model bounds blink duration and intervals");
    for (const char* field : {"blinkFrameCount", "blinkIntervalMin", "blinkIntervalMax",
                             "beginFrame", "endFrame"}) {
        for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()}) {
            using namespace emote_fixture;
            auto document = Scene();
            auto eye = O({{"label", S("expression")}, {"beginFrame", N(0)}, {"endFrame", N(10)},
                          {"blinkFrameCount", N(4)}, {"blinkIntervalMin", N(0)}, {"blinkIntervalMax", N(0)}});
            eye.object[field] = N(invalid);
            document.root.object["metadata"].object["eyeControl"] = A({eye});
            artc::EmoteModel invalid_model;
            Check(!invalid_model.Load(std::move(document), error),
                  std::string("non-finite blink metadata rejected: ") + field);
        }
    }
}

// These edge cases lock this project's present compatibility policy, not
// unmeasured official RNG, degenerate-span, or queue semantics. Public Render
// observations have float coordinates and decimal serialization, hence the
// existing 2e-5 coordinate tolerance rather than bitwise clock comparisons.
void BlinkNumericalBoundaryTests() {
    std::string error;
    for (const auto& clock : {EyeClock{10, 3, 3}, EyeClock{3.7, 0.1, 0.1}}) {
        auto model = BlinkModel(clock, clock);
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (double offset : {0.0, clock.duration * 0.4, clock.duration * 0.6, clock.duration}) {
                const double boundary = cycle * (clock.duration + clock.delay) + clock.delay + offset;
                const double ms = boundary / artc::EmotePlayer::kFramesPerMillisecond;
                for (double adjacent : {std::nextafter(ms, 0.0), ms,
                                        std::nextafter(ms, std::numeric_limits<double>::infinity())}) {
                    artc::EmotePlayer player;
                    Check(player.Load(model, error), "load nextafter phase player");
                    player.Progress(adjacent);
                    double time = adjacent * artc::EmotePlayer::kFramesPerMillisecond;
                    double expected = FixedEyeAt(time, clock.duration, clock.delay);
                    CheckEyes(player, expected, expected, "nextafter before/at/after phase boundary");
                    // A continuous boundary pose alone cannot detect a wrong phase.
                    // Probe later poses too, without requiring a hidden phase identity
                    // or treating one floating-point ULP as an official timing rule.
                    for (double dt : {0.125, 0.75, 3.1}) {
                        AdvanceFrames(player, dt);
                        time += dt;
                        expected = FixedEyeAt(time, clock.duration, clock.delay);
                        CheckEyes(player, expected, expected, "nextafter boundary continues without stuck/lost time");
                    }
                }
            }
        }
    }

    const std::pair<EyeClock, EyeClock> clocks[] = {
        {{1.1, 0.1, 0.1}, {3.7, 0.3, 0.3}},
        {{1.3, 0.1, 0.7}, {7.9, 0.2, 1.1}}
    };
    for (const auto& clock : clocks) {
        auto decimal_model = BlinkModel(clock.first, clock.second);
        for (double total : {123.1, 3599.9}) {
            for (double part : {0.1, 0.3, 0.07}) {
                artc::EmotePlayer large, split;
                Check(large.Load(decimal_model, error) && split.Load(decimal_model, error),
                      "load non-binary duration/interval players");
                AdvanceFrames(large, total);
                const int count = static_cast<int>(std::floor(total / part));
                for (int i = 0; i < count; ++i) AdvanceFrames(split, part);
                AdvanceFrames(split, total - count * part);
                auto expected = ObserveEyes(split);
                CheckEyes(large, expected.first, expected.second, "decimal large/split within render tolerance");
                if (clock.first.delay == clock.first.max_delay) {
                    CheckEyes(large, FixedEyeAt(total, clock.first.duration, clock.first.delay),
                              FixedEyeAt(total, clock.second.duration, clock.second.delay),
                              "decimal fixed intervals match analytical phases");
                }
                for (double dt : {0.13, 0.7, 2.3, 11.1}) {
                    AdvanceFrames(large, dt);
                    AdvanceFrames(split, dt);
                    expected = ObserveEyes(split);
                    CheckEyes(large, expected.first, expected.second, "decimal large/split future phases agree");
                }
            }
        }
    }
}

void BlinkBaseBoundaryTests() {
    std::string error;
    // With amount=1/2, local application interpolates from a nonzero base
    // toward end; it neither overwrites GetVariable nor extrapolates. Bases
    // outside the inclusive endpoint range and empty spans remain untouched.
    auto normal = BlinkModel({10, 0, 0, true, true, 2, 8}, {10, 0, 0, true, true, 2, 8});
    for (double base : {1.0, std::nextafter(2.0, 0.0), 2.0, 4.0, 8.0,
                        std::nextafter(8.0, 9.0), 9.0}) {
        artc::EmotePlayer player;
        Check(player.Load(normal, error), "load nonzero-base player");
        Check(player.SetVariable("leftEye", base, 0, 0, error) &&
              player.SetVariable("rightEye", base, 0, 0, error), "set eye bases");
        double time = 0;
        for (double dt : {2.0, 3.0, 3.0, 2.0, 3589.75}) {
            AdvanceFrames(player, dt);
            time += dt;
            const double amount = FixedEyeAt(time, 10, 0) / 10;
            const double expected = base >= 2 && base <= 8 ? base + (8 - base) * amount : base;
            CheckEyes(player, expected, expected, "nonzero base interpolation / out-of-range no-op");
            bool found = false;
            Check(player.GetVariable("leftEye", &found) == base && found,
                  "blink overlay does not mutate the script base");
        }
    }

    auto degenerate = BlinkModel({1.1, 0, 0, true, true, 4, 4},
                                {3.7, 0.1, 0.1, true, true, 8, 2});
    for (double base : {1.0, 2.0, 4.0, 6.0, 8.0, 9.0}) {
        artc::EmotePlayer player;
        Check(player.Load(degenerate, error), "load empty/reversed-span player");
        Check(player.SetVariable("leftEye", base, 0, 0, error) &&
              player.SetVariable("rightEye", base, 0, 0, error), "set empty/reversed-span bases");
        double time = 0;
        for (double dt : {0.84, 1.11, 0.37, 0.74, 3590.1}) {
            AdvanceFrames(player, dt);
            time += dt;
            const double amount = FixedEyeAt(time, 3.7, 0.1) / 10;
            const double expected = base >= 2 && base <= 8 ? base + (2 - base) * amount : base;
            CheckEyes(player, base, expected, "empty/reversed spans terminate and respect local base gate");
            const auto actual = ObserveEyes(player);
            Check(std::isfinite(actual.first) && std::isfinite(actual.second) &&
                  actual.second >= std::min(base, 2.0) - 2e-5 &&
                  actual.second <= std::max(base, 2.0) + 2e-5,
                  "reversed-span local application does not extrapolate past base/end");
        }
    }
}

struct PublicPlaybackState {
    std::vector<double> values;
    std::vector<std::string> labels;
    bool operator==(const PublicPlaybackState& other) const {
        return values == other.values && labels == other.labels;
    }
};

PublicPlaybackState ObservePlayback(artc::EmotePlayer& player) {
    double x, y, sx, sy;
    player.GetCoord(&x, &y);
    player.GetScale(&sx, &sy);
    PublicPlaybackState state;
    state.values = {x, y, sx, sy, player.GetRot(), static_cast<double>(player.GetColor()),
                    player.GetGrayscale(), player.BaseFrame(), static_cast<double>(player.IsAnimating()),
                    static_cast<double>(player.CountPlayingTimelines())};
    for (const char* label : {"expression", "leftEye", "rightEye"}) {
        bool found = false;
        state.values.push_back(player.GetVariable(label, &found));
        state.values.push_back(found);
    }
    for (const char* label : {"once", "loop"}) {
        bool found = false;
        state.values.push_back(player.TimelineBlendRatio(label, &found));
        state.values.push_back(found);
        state.values.push_back(player.IsTimelinePlaying(label));
    }
    for (int i = 0; i < player.CountPlayingTimelines(); ++i) {
        state.labels.push_back(player.PlayingTimelineLabelAt(i));
        state.values.push_back(player.PlayingTimelineFlagsAt(i));
    }
    return state;
}

void NonfiniteProgressTests() {
    std::string error;
    auto model = BlinkModel({1.3, 0.1, 0.7}, {7.9, 0.2, 1.1});
    artc::EmotePlayer player, control;
    for (auto* p : {&player, &control}) {
        Check(p->Load(model, error), "load nonfinite-dt comparison player");
        p->SetCoord(20, -10, 2000, 0);
        p->SetScale(2, 3, 2000, 0);
        p->SetRot(90, 2000, 0);
        p->SetColor(0x40404000, 2000, 0);
        p->SetGrayscale(1, 2000, 0);
        Check(p->SetVariable("expression", 8, 2000, 0, error), "start variable transition");
        Check(p->FadeInTimeline("once", 2000, 0, error), "start timeline and blend transition");
        Check(p->PlayTimeline("loop", artc::EmotePlayer::kTimelineParallel, error), "start concurrent loop");
        p->Progress(250);
    }
    const auto before = ObservePlayback(player);
    const auto eyes_before = ObserveEyes(player);
    Check(player.IsAnimating() && player.IsTimelinePlaying("once") && player.IsTimelinePlaying("loop") &&
          player.CountPlayingTimelines() == 2, "nonfinite test has active transitions and concurrent timelines");
    bool found = false;
    double x, y, sx, sy;
    player.GetCoord(&x, &y);
    player.GetScale(&sx, &sy);
    Check(std::abs(x - 2.5) < 1e-12 && std::abs(y + 1.25) < 1e-12 &&
          std::abs(sx - 1.125) < 1e-12 && std::abs(sy - 1.25) < 1e-12 &&
          std::abs(player.GetRot() - 11.25) < 1e-12 && std::abs(player.GetGrayscale() - 0.125) < 1e-12 &&
          player.BaseFrame() == 15 && std::abs(player.GetVariable("expression", &found) - 15) < 1e-12 && found &&
          std::abs(player.TimelineBlendRatio("once", &found) - 0.125) < 1e-12 && found,
          "nonfinite baseline is genuinely partway through transforms, variable and blend");
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        player.Progress(invalid);
        Check(ObservePlayback(player) == before,
              "nonfinite Progress leaves active transform/blend/variables/base/timelines unchanged");
        CheckEyes(player, eyes_before.first, eyes_before.second, "nonfinite Progress leaves blink overlay unchanged");
    }
    // Public getters cannot inspect transition elapsed times.
    // Compare future state and exact timeline release against an untouched control
    // to catch a latent change even when the immediate visible pose is equal.
    for (double dt : {749.0, 1.0, 250.0, 750.0}) {
        player.Progress(dt);
        control.Progress(dt);
        Check(ObservePlayback(player) == ObservePlayback(control), "nonfinite Progress preserves future playback and controls");
        const auto expected = ObserveEyes(control);
        CheckEyes(player, expected.first, expected.second, "nonfinite Progress preserves future blink/RNG state");
        if (player.BaseFrame() < 60) {
            Check(player.IsTimelinePlaying("once") && player.IsTimelinePlaying("loop"), "both timelines remain live before finish");
        } else {
            Check(!player.IsTimelinePlaying("once") && player.IsTimelinePlaying("loop") &&
                   player.CountPlayingTimelines() == 1, "nonloop removes at unchanged finish time");
        }
    }
}
void HugeFiniteNumericTests() {
    auto model = std::make_shared<artc::EmoteModel>();
    std::string error;
    Check(model->Load(PairDocument(), error), "load huge finite fixture");
    artc::EmotePlayer player;
    Check(player.Load(model, error), "load huge finite player");
    Check(player.SetVariable("expression", 1e308, 0, 0, error) &&
          player.SetVariable("expression", -1e308, 1000, 0, error), "opposite finite endpoints accepted");
    player.Progress(0);
    Check(player.GetVariable("expression", nullptr) == 1e308, "zero Progress preserves huge finite start");
    player.Progress(500);
    Check(player.GetVariable("expression", nullptr) == 0, "opposite huge endpoints interpolate to finite midpoint");
    for (double sign : {-1.0, 1.0}) {
        Check(player.Load(model, error) && player.SetVariable("expression", sign * 1e308, 0, 0, error) &&
              player.SetVariable("expression", -sign * 1e308, 1000, 0, error), "start +/-1e308 quarter-step segment");
        const double expected[] = {sign * 1e308, sign * 5e307, 0, -sign * 5e307, -sign * 1e308};
        const double steps[] = {0, 250, 250, 250, 250};
        for (int i = 0; i < 5; ++i) {
            player.Progress(steps[i]);
            const double value = player.GetVariable("expression", nullptr);
            Check(std::isfinite(value) && std::fabs(value - expected[i]) <= 1e293,
                  "opposite huge endpoints are finite at Progress 0, 1/4, 1/2, 3/4 and completion");
        }
        Check(player.GetVariable("expression", nullptr) == -sign * 1e308 && !player.IsAnimating(),
              "opposite huge segment completes exactly on its finite target");
    }
    Check(player.Load(model, error), "reset huge composition player");
    artc::Compositor compositor;
    Check(player.Render(compositor, "huge", error), "install finite baseline container");
    const auto baseline_revision = compositor.Revision();
    player.SetCoordAngle(12, 34, 0, 0);
    Check(player.SetVariableDiff("leftEye", "expression", 1e308, 0, 0, error) &&
          player.SetVariableDiff("rightEye", "expression", 1e308, 0, 0, error), "two finite pair currents can overflow composition");
    Check(!player.Render(compositor, "huge", error) && !error.empty(), "unrepresentable composed variable explicitly fails Render");
    const auto unchanged = compositor.GetLayerInfo("huge");
    Check(unchanged.found && unchanged.left == 0 && unchanged.top == 0,
          "composition failure does not partially mutate existing container");
    Check(compositor.Revision() == baseline_revision, "composition failure does not revise compositor state");
    Check(player.GetVariableDiff("leftEye", "expression") == 1e308 &&
          player.GetVariableDiff("rightEye", "expression") == 1e308,
          "composition rejection leaves finite independent currents intact");
    Check(!player.Render(compositor, "absent", error) && !compositor.GetLayerInfo("absent").found,
          "composition failure never creates a new container");
    Check(player.SetVariableDiff("leftEye", "expression", 2, 0, 0, error) &&
          player.SetVariableDiff("rightEye", "expression", 3, 0, 0, error) &&
          player.Render(compositor, "huge", error) && error.empty(), "correcting pair values recovers Render after overflow");
    Check(compositor.GetLayerInfo("huge").left == 12 && compositor.GetLayerInfo("huge").top == 34,
          "recovered Render applies the previously pending container transform");

    const double maximum = std::numeric_limits<double>::max();
    for (double sign : {-1.0, 1.0}) for (double ease : {0.0, maximum, -maximum}) {
        Check(player.Load(model, error) && player.SetVariable("expression", sign * 1e308, 0, 0, error) &&
              player.SetVariable("expression", -sign * 1e308, 1000, ease, error), "start huge signed base segment with finite extreme ease");
        Check(player.SetVariableDiff("leftEye", "expression", sign * 1e308, 0, 0, error) &&
              player.SetVariableDiff("leftEye", "expression", -sign * 1e308, 1000, ease, error), "start huge signed pair segment");
        player.Progress(0);
        Check(player.GetVariable("expression", nullptr) == sign * 1e308 &&
              player.GetVariableDiff("leftEye", "expression") == sign * 1e308,
              "extreme-ease zero Progress returns exact finite start, not Inf*0");
        player.Progress(500);
        const double expected = ease > 0 ? sign * 1e308 : ease < 0 ? -sign * 1e308 : 0;
        Check(player.GetVariable("expression", nullptr) == expected &&
              player.GetVariableDiff("leftEye", "expression") == expected,
              "convex half-step is finite for signed huge endpoints and extreme ease");
        Check(player.SetVariable("expression", sign * 1e308, 1000, 0, error), "retarget huge base from finite current");
        player.Progress(500);
        const double current = player.GetVariable("expression", nullptr);
        Check(std::isfinite(current) && current >= -1e308 && current <= 1e308,
              "huge retarget retains finite current inside segment endpoints");
        player.SetQueuing(true);
        Check(player.SetVariable("expression", -sign * 1e308, 1000, 0, error) &&
              player.SetVariable("expression", sign * maximum, 1000, 0, error) &&
              player.SetVariableDiff("leftEye", "expression", sign * maximum, 1000, 0, error), "queue huge endpoints after retarget");
        player.Progress(750);
        Check(std::isfinite(player.GetVariable("expression", nullptr)) &&
              std::isfinite(player.GetVariableDiff("leftEye", "expression")), "queue boundary Advance never publishes nonfinite current");
        player.Skip();
        Check(player.GetVariable("expression", nullptr) == sign * maximum &&
              player.GetVariableDiff("leftEye", "expression") == sign * maximum && !player.IsAnimating(),
              "Skip still selects exact final queued target and clears queue");
        player.Step();
        Check(player.GetVariable("expression", nullptr) == sign * maximum,
              "repeated Finish preserves huge finite final endpoint");
    }

    // Same-sign paths use a bounded difference too, including adjacent maximum
    // values and decreasing segments; no getter substitutes zero for overflow.
    for (double sign : {-1.0, 1.0}) {
        Check(player.Load(model, error) && player.SetVariable("expression", sign * maximum, 0, 0, error) &&
              player.SetVariable("expression", sign * 1e308, 1000, 0, error), "start same-sign huge transition");
        player.Progress(500);
        const double value = player.GetVariable("expression", nullptr);
        Check(std::isfinite(value) && std::abs(value) >= 1e308 && std::abs(value) <= maximum,
              "same-sign convex interpolation remains in finite endpoint range");
    }

    // Cancellation tests only promise what the platform's intermediate type
    // can represent. The same-width case must explicitly fail, not saturate.
    Check(player.Load(model, error) && player.SetVariable("expression", 1e308, 0, 0, error) &&
          player.SetVariableDiff("leftEye", "expression", -1e308, 0, 0, error) &&
          player.Render(compositor, "cancel", error), "huge cancellation succeeds when every intermediate fits double");
    Check(player.SetVariableDiff("leftEye", "expression", 1e308, 0, 0, error) &&
          player.SetVariableDiff("rightEye", "expression", -1e308, 0, 0, error), "prepare cancellation after oversized intermediate");
    if (std::numeric_limits<long double>::max_exponent > std::numeric_limits<double>::max_exponent)
        Check(player.Render(compositor, "cancel", error), "wide accumulator preserves huge cancellation to a representable result");
    else
        Check(!player.Render(compositor, "cancel", error) && !error.empty(), "same-width accumulator explicitly rejects oversized intermediate");

    // Include private timeline*blend in the same checked sum, not a separate
    // double mixer that overflows before pair cancellation is applied.
    using namespace emote_fixture;
    auto document = PairDocument();
    auto& track = document.root.object["metadata"].object["timelineControl"].array[0]
                          .object["variableList"].array[0];
    for (auto& key : track.object["frameList"].array) key.object["content"].object["value"] = N(1e308);
    auto private_model = std::make_shared<artc::EmoteModel>();
    Check(private_model->Load(document, error) && player.Load(private_model, error) &&
          player.SetVariable("expression", 1e308, 0, 0, error) && player.PlayTimeline("delta", 3, error),
          "prepare private timeline and base huge sum");
    Check(!player.Render(compositor, "private_overflow", error) && !error.empty() &&
          !compositor.GetLayerInfo("private_overflow").found, "private plus base overflow rejects before creating container");
    Check(player.SetTimelineBlendRatio("delta", 0.5, error) &&
          player.SetVariableDiff("leftEye", "expression", -1e308, 0, 0, error) &&
          player.Render(compositor, "private_safe", error), "weighted private/base/pair sum can be representable without saturation");
    Check(player.GetVariable("expression", nullptr) == 1e308 &&
          player.GetVariableDiff("leftEye", "expression") == -1e308,
          "render composition never rewrites huge base or pair getters");
    if (std::numeric_limits<long double>::min_exponent < std::numeric_limits<double>::min_exponent) {
        for (auto& key : track.object["frameList"].array)
            key.object["content"].object["value"] = N(1e-300);
        Check(private_model->Load(document, error) && player.Load(private_model, error) &&
              player.PlayTimeline("delta", 3, error) && player.SetTimelineBlendRatio("delta", 1e-300, error),
              "prepare wide nonzero contribution too small for double output");
        Check(!player.Render(compositor, "private_underflow", error) && !error.empty() &&
              !compositor.GetLayerInfo("private_underflow").found,
              "wide nonzero composition cannot silently narrow to zero");
    }

    Check(player.Load(model, error), "reset for container numeric validation");
    for (int channel = 0; channel < 5; ++channel) {
        Check(player.Load(model, error), "reset numeric container channel");
        artc::Compositor target;
        player.SetCoordAngle(7, 9, 0, 30);
        Check(player.Render(target, "container", error), "install unchanged container observation");
        const auto revision = target.Revision();
        if (channel == 0) player.SetCoordAngle(1e308, 9, 0, 30);
        if (channel == 1) player.SetCoordAngle(7, 9, 0, 1e308);
        if (channel == 2) player.SetScale(1e308, 1, 0, 0);
        if (channel == 3) player.SetScaleOrigin(1, 1e308, 0);
        if (channel == 4) player.SetScale(std::numeric_limits<float>::max() / 2.0, 1, 0, 0);
        bool ok = true, threw = false;
        try { ok = player.Render(target, "container", error); }
        catch (...) { threw = true; }
        Check(!threw && !ok && !error.empty(), "finite but over-float control/percent property returns explicit failure, no stof exception");
        const auto info = target.GetLayerInfo("container");
        Check(info.left == 7 && info.top == 9, "container range failure leaves installed props unchanged");
        Check(target.Revision() == revision, "range rejection cannot silently alter rotation, scale or compositor revision");
        player.SetCoordAngle(2, 3, 0, 45);
        player.SetScaleOrigin(1, 0, 0);
        Check(player.Render(target, "container", error) && error.empty() && target.GetLayerInfo("container").left == 2,
              "container transform can recover after range failure");
    }
    Check(player.Load(model, error), "reset huge Animated transform fixture");
    player.SetCoordAngle(-1e308, 1e308, 0, -1e308);
    player.SetCoord(1e308, -1e308, 1000, 0);
    player.SetRot(1e308, 1000, 0);
    player.SetScale(-1e308, 1e308, 0, 0);
    player.SetScale(1e308, -1e308, 1000, 0);
    player.Progress(500);
    double x, y, sx, sy;
    player.GetCoord(&x, &y);
    player.GetScale(&sx, &sy);
    Check(x == 0 && y == 0 && sx == 0 && sy == 0 && player.GetRot() == 0,
          "opposite huge coord/scale/rotation Animated channels share finite midpoint math");
    player.Skip();
    player.GetCoord(&x, &y);
    player.GetScale(&sx, &sy);
    Check(x == 1e308 && y == -1e308 && sx == 1e308 && sy == -1e308 && player.GetRot() == 1e308,
          "Skip still returns exact finite huge transform targets, not renderer-clamped values");

    document = PairDocument();
    document.root.object["metadata"].object["eyeControl"] = A({O({
        {"label", S("expression")}, {"beginFrame", N(-1e308)}, {"endFrame", N(1e308)},
        {"blinkFrameCount", N(10)}, {"blinkIntervalMin", N(0)}, {"blinkIntervalMax", N(0)}})});
    auto blink_model = std::make_shared<artc::EmoteModel>();
    Check(blink_model->Load(document, error) && player.Load(blink_model, error) &&
          player.SetVariable("expression", -1e308, 0, 0, error), "load huge finite blink endpoint fixture");
    AdvanceFrames(player, 2);
    Check(player.Render(compositor, "huge_blink", error) && player.GetVariable("expression", nullptr) == -1e308,
          "blink convex overlay avoids overflowing huge span/delta without mutating base");
    artc::EmoteSelector selector;
    selector.items = {{"expression", 1e308, -1e308}, {"unused", -1e308, 1e308}};
    Check(artc::EmotePlayer::SelectorOptionValue(selector, 0, 0) == 1e308 &&
          artc::EmotePlayer::SelectorOptionValue(selector, 0.5, 0) == 0 &&
          artc::EmotePlayer::SelectorOptionValue(selector, 1, 0) == -1e308 &&
          artc::EmotePlayer::SelectorOptionValue(selector, 0.5, 1) == 0,
          "selector uses stable convex interpolation for huge on/off values");
}

void PassFadeDeadlineTests() {
    auto model = ControllerModel();
    artc::EmotePlayer player;
    std::string error;
    Check(player.Load(model, error) && player.PlayTimeline("main", 3, error), "load Pass deadline fixture");
    player.Pass();
    AdvanceFrames(player, 10);
    player.Pass();
    AdvanceFrames(player, 9);
    Check(player.IsTimelinePlaying("main") && player.TimelineBlendRatio("main", nullptr) > 0,
          "repeat Pass leaves fade live at 19 frames, not seconds/milliseconds early");
    AdvanceFrames(player, 1);
    Check(!player.IsTimelinePlaying("main") && player.CountPlayingTimelines() == 0,
          "10+9+1 frames completes original 20-frame Pass deadline and removes timeline");
    const double duration = 20.0 / artc::EmotePlayer::kFramesPerMillisecond;
    Check(player.Load(model, error) && player.PlayTimeline("main", 3, error), "reset measurably-before deadline fixture");
    player.Pass();
    player.Progress(duration * (1 - 1e-10));
    Check(player.IsTimelinePlaying("main") && player.TimelineBlendRatio("main", nullptr) > 0,
          "fade does not finish measurably before deadline (tolerance is rounding-scale only)");
    for (double final : {std::nextafter(duration, 0.0), duration,
                         std::nextafter(duration, std::numeric_limits<double>::infinity())}) {
        Check(player.Load(model, error) && player.PlayTimeline("main", 3, error), "reset adjacent deadline fixture");
        player.Pass();
        player.Progress(final);
        Check(!player.IsTimelinePlaying("main"), "one-ULP adjacent fade deadline completes within numerical tolerance");
    }
    for (double duration_ms : {std::numeric_limits<double>::denorm_min(), 1e-300, 1e-12, 1.0, 1000.0}) {
        Check(player.Load(model, error) && player.SetVariable("expression", 1, duration_ms, 0, error),
              "start duration-relative deadline transition");
        player.Progress(0);
        Check(player.IsAnimating() && player.GetVariable("expression", nullptr) == 0,
              "zero elapsed does not finish any positive duration, including smallest subnormal");
        if (duration_ms > std::numeric_limits<double>::denorm_min()) {
            player.Progress(duration_ms * 0.5);
            Check(player.IsAnimating() && std::abs(player.GetVariable("expression", nullptr) - 0.5) < 1e-12,
                  "relative deadline tolerance never finishes a tiny transition at half time");
        }
        player.Progress(duration_ms);
        Check(!player.IsAnimating() && player.GetVariable("expression", nullptr) == 1,
              "positive duration reaches endpoint without an absolute minimum epsilon");
    }
}
} // namespace

int main() {
    auto doc = emote_fixture::PlayerDocument();
    artc::EmoteModel model;
    std::string error;
    Check(model.Load(doc, error), "load player document: " + error);
    Check(model.Timelines().count("delta") && model.Timelines().count("loop") &&
              model.Timelines().count("once"),
          "three timelines parsed");

    std::map<std::string, double> a, b, c;
    Check(model.Sample("loop", 5, a) && model.Sample("loop", 25, b), "sample loop");
    const double *va = Find(a, "expression");
    const double *vb = Find(b, "expression");
    Check(va && vb && std::fabs(*va - *vb) < 1e-6, "loop wraps (t=25 == t=5)");

    std::map<std::string, double> held, held_end;
    Check(model.Sample("once", 60, held) && model.Sample("once", 120, held_end), "sample once");
    const double *vh = Find(held, "expression");
    const double *ve = Find(held_end, "expression");
    Check(vh && ve && std::fabs(*vh - *ve) < 1e-6, "finite timeline holds its last frame");

    BlinkClockTests();
    BlinkNumericalBoundaryTests();
    BlinkBaseBoundaryTests();
    NonfiniteProgressTests();
    PairCompositionTests();
    MirrorMatchMetadataTests();
    NativeTimelineContractTests();
    PrivateTimelineTests();
    StepSkipPassTests();
    AnimatedQueuingTests();
    TimelinePartitionTests();
    TimelineClockIsolationTests();
    UnsupportedTimelineBoundaryTests();
    HugeFiniteNumericTests();
    PassFadeDeadlineTests();

    if (g_failures == 0) std::cout << "emote_timeline_regressions: ok\n";
    return g_failures == 0 ? 0 : 1;
}
