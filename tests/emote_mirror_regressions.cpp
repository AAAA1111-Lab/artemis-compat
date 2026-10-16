// emote_mirror_regressions.cpp — metadata/user/effective mirror state, geometry
// flip and sign matching of composed variables.

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
int g_failures = 0;
void Check(bool ok, const std::string &what) {
    if (!ok) { ++g_failures; std::cerr << "FAIL: " << what << "\n"; }
}

artc::PsbDocument Document(bool metadata_mirror, const std::vector<std::string> &matches, bool with_list = true) {
    using namespace emote_fixture;
    auto doc = PlayerDocument();
    auto &metadata = doc.root.object["metadata"].object;
    metadata["variableList"] = A({O({{"label", S("expression")}}),
        O({{"label", S("leftEye")}}), O({{"label", S("rightEye")}})});
    if (metadata_mirror) metadata["mirror"] = N(1);
    if (with_list) {
        artc::PsbValue list = A({});
        for (const auto &m : matches) list.array.push_back(S(m.c_str()));
        metadata["mirrorControl"] = O({{"variableMatchList", list}});
    }
    auto &motion = doc.root.object["object"].object["actor"].object["motion"].object["idle"];
    const auto node = [](const char *label, int parameter) {
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
    auto &image = doc.root.object["source"].object["images"].object["icon"].object["face"];
    image.object["originX"] = N(0);
    image.object["originY"] = N(0);
    return doc;
}

std::shared_ptr<artc::EmoteModel> Model(artc::PsbDocument doc) {
    auto model = std::make_shared<artc::EmoteModel>();
    std::string error;
    Check(model->Load(std::move(doc), error), "load mirror fixture: " + error);
    return model;
}

struct Eyes { double left, right; bool holder_reversed, child_reversed; };

Eyes Observe(artc::EmotePlayer &player) {
    artc::Compositor compositor;
    std::string error;
    Check(player.Render(compositor, "eyes", error), "render mirror fixture: " + error);
    const auto left = compositor.GetLayerInfo("eyes.000001.000000");
    const auto right = compositor.GetLayerInfo("eyes.000002.000000");
    Check(left.found && right.found, "both parameterized pictures installed");
    Eyes result{left.left, right.left, false, false};
    for (const auto &layer : compositor.Layers()) {
        if (layer.id == "eyes") result.holder_reversed = layer.reverse_x;
        else if (layer.id.rfind("eyes.", 0) == 0 && layer.reverse_x) result.child_reversed = true;
    }
    return result;
}

void CheckEyes(artc::EmotePlayer &player, double left, double right, const std::string &what) {
    const auto v = Observe(player);
    Check(std::abs(v.left - left) < 2e-5 && std::abs(v.right - right) < 2e-5,
          what + " (actual " + std::to_string(v.left) + ", " + std::to_string(v.right) + ")");
}

void StateTests() {
    std::string error;
    for (int metadata = 0; metadata < 2; ++metadata) {
        for (int user = 0; user < 2; ++user) {
            artc::EmotePlayer player;
            Check(player.Load(Model(Document(metadata, {})), error), "load state player");
            Check(!player.Mirror() && player.EffectiveMirror() == (metadata != 0),
                  "Load starts with user=false, effective=metadata");
            player.SetMirror(user);
            const bool effective = (metadata != 0) != (user != 0);
            const std::string tag = "metadata=" + std::to_string(metadata) + " user=" + std::to_string(user);
            Check(player.EffectiveMirror() == effective, "effective = metadata XOR user: " + tag);
            Check(player.Mirror() == (user != 0), "Mirror() returns the user request: " + tag);
            Check(player.IsMirrored() == effective, "IsMirrored stays the effective value: " + tag);
            const auto v = Observe(player);
            Check(v.holder_reversed == effective && !v.child_reversed,
                  "geometry flips once, on the container, by effective: " + tag);
        }
    }

    artc::EmotePlayer player;
    Check(player.Load(Model(Document(true, {})), error), "load repeat player");
    for (int i = 0; i < 3; ++i) {
        player.SetMirror(true);
        Check(player.Mirror() && !player.EffectiveMirror() && !Observe(player).holder_reversed,
              "repeated SetMirror(true) is an assignment, not a toggle");
    }
    player.SetMirror(false);
    Check(!player.Mirror() && player.EffectiveMirror() && Observe(player).holder_reversed,
          "SetMirror(false) restores metadata-only effective state");
    player.SetMirror(true);
    Check(player.Load(Model(Document(false, {})), error) && !player.Mirror() && !player.EffectiveMirror(),
          "reload resets the user request");
}

void MatchTests() {
    std::string error;
    const auto run = [&](const std::vector<std::string> &list, bool with_list, bool mirror,
                         double want_left, double want_right, double want_expr, const std::string &what) {
        artc::EmotePlayer player;
        Check(player.Load(Model(Document(false, list, with_list)), error), "load match player");
        player.SetMirror(mirror);
        Check(player.SetVariable("leftEye", 3, 0, 0, error) && player.SetVariable("rightEye", 2, 0, 0, error) &&
              player.SetVariable("expression", 5, 0, 0, error), "set bases");
        for (int pass = 0; pass < 2; ++pass)  // second pass exercises the label cache
            CheckEyes(player, want_left, want_right, what);
        bool found = false;
        Check(player.GetVariable("leftEye", &found) == 3 && player.GetVariable("rightEye", &found) == 2 &&
              player.GetVariable("expression", &found) == 5, "getters never see the sign: " + what);
        (void)want_expr;
    };
    run({"left"}, true, true, -3, 2, 0, "substring of label negates only containing labels");
    run({"Eye"}, true, true, -3, -2, 0, "shared substring negates both");
    run({"LEFT"}, true, true, 3, 2, 0, "matching is case sensitive");
    run({"leftEyes"}, true, true, 3, 2, 0, "item longer than label does not match (label must contain item)");
    run({"nomatch", "ight"}, true, true, 3, -2, 0, "any list item may match");
    run({""}, true, true, -3, -2, 0, "empty item matches any label");
    run({}, true, true, 3, 2, 0, "empty list matches nothing");
    run({}, false, true, 3, 2, 0, "absent list matches nothing");
    run({"left"}, true, false, 3, 2, 0, "non-effective mirror never negates");

    // Metadata mirror with a user request cancels out: effective false, no sign.
    artc::EmotePlayer player;
    Check(player.Load(Model(Document(true, {"left"})), error), "load metadata-mirror player");
    Check(player.SetVariable("leftEye", 3, 0, 0, error), "set base");
    CheckEyes(player, -3, 0, "metadata mirror alone negates matches");
    player.SetMirror(true);
    CheckEyes(player, 3, 0, "metadata XOR user cancelling restores the sign");
}

void WholeValueTests() {
    using namespace emote_fixture;
    std::string error;
    auto doc = Document(false, {"leftEye"});
    auto &timelines = doc.root.object["metadata"].object["timelineControl"].array;
    for (auto &timeline : timelines) {
        auto &track = timeline.object["variableList"].array[0];
        track.object["label"] = S("leftEye");
        for (auto &key : track.object["frameList"].array)
            key.object["content"].object["value"] = N(timeline.object["diff"].number ? 2 : 6);
    }
    artc::EmotePlayer player;
    Check(player.Load(Model(std::move(doc)), error), "load whole-value player");
    player.SetMirror(true);
    Check(player.SetVariable("leftEye", 3, 0, 0, error) &&
          player.PlayTimeline("once", 1, error) && player.SetTimelineBlendRatio("once", 0.5, error) &&
          player.PlayTimeline("delta", 3, error) && player.SetTimelineBlendRatio("delta", 0.25, error) &&
          player.SetVariableDiff("rightEye", "leftEye", 1, 0, 0, error), "base/main/private/pair layers");
    // base 6 + private .5 + pair 1 = 7.5, negated once as a whole.
    CheckEyes(player, -7.5, 0, "mirror negates the entire composed value once");
    bool found = false;
    Check(player.GetVariable("leftEye", &found) == 6 && found &&
          player.GetVariableDiff("rightEye", "leftEye", &found) == 1 && found,
          "base and pair getters unchanged by mirror");
    player.SetMirror(false);
    CheckEyes(player, 7.5, 0, "turning mirror off restores the positive composition");
}
}  // namespace

int main() {
    StateTests();
    MatchTests();
    WholeValueTests();
    if (g_failures) { std::cerr << g_failures << " failure(s)\n"; return 1; }
    std::cout << "emote_mirror_regressions: ok\n";
    return 0;
}
