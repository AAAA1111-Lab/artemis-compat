// WP8/E15: retain legacy recursive derivation/C++ clock jump regressions, and
// separately test the read-only motion.tag query. Neither defines wait state or
// IsAnimating. The old sync -> selfSyncTime -> lastTime three-source plan is
// falsified by the native evidence and needs revision, NOT implementation.
// Native Lua does not register skipToSync; selfSync is absent. No Lua connection.
#include "emote_scene_fixture.h"
#include "render/emote_player.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

namespace {
using namespace emote_fixture;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cerr << "FAIL: " << what << '\n'; }
}

void CheckFrame(double actual, double expected, const std::string& what) {
    Check(std::isfinite(actual) && std::abs(actual - expected) < 1e-8,
          what + " (actual " + std::to_string(actual) + ", expected " +
          std::to_string(expected) + ")");
}

artc::PsbValue& Motion(artc::PsbDocument& doc, const char* motion,
                       const char* chara = "actor") {
    return doc.root.object["object"].object[chara].object["motion"].object[motion];
}

artc::PsbValue ContentFrame(double time) {
    return Key(time, 2, O({{"src", S("layout")}}));
}

artc::PsbValue ReferenceFrame(double time, const char* path, double offset = 0) {
    return Key(time, 2, O({{"src", S(path)},
        {"motion", O({{"timeOffset", N(offset)}})}}));
}

artc::PsbValue MotionWith(artc::PsbValue nodes) {
    return O({{"layer", std::move(nodes)}, {"lastTime", N(2000)}, {"loopTime", N(-1)}});
}

artc::PsbDocument Document(artc::PsbValue nodes) {
    auto doc = PlayerDocument();
    Motion(doc, "idle") = MotionWith(std::move(nodes));
    // Keep timeline/variable metadata for player transaction observations, but
    // remove the unrelated face motion so every sync value is authored here.
    doc.root.object["object"].object["actor"].object["motion"].object.erase("face");
    return doc;
}

std::shared_ptr<artc::EmoteModel> LoadModel(artc::PsbDocument document) {
    auto model = std::make_shared<artc::EmoteModel>();
    std::string error;
    const bool loaded = model->Load(std::move(document), error);
    Check(loaded, "load synthetic sync model: " + error);
    return model;
}

void AdvanceFrames(artc::EmotePlayer& player, double frames) {
    player.Progress(frames / artc::EmotePlayer::kFramesPerMillisecond);
}

void ContentAndTerminalTests() {
    auto doc = Document(A({
        Node(2, "early", A({ContentFrame(0), ContentFrame(4),
            Key(900, 0, O({{"src", S("layout")}}))})),
        Node(2, "late", A({ContentFrame(2), ContentFrame(17),
            Key(1000, 0, O({{"src", S("motion/actor/terminal child")}}))})),
        // A nonterminal key without content also does not define sync.
        Node(2, "empty", A({Key(1200, 2), Key(1500, 0)}))
    }));
    // Neither file-level end metadata nor an explicit-looking sync field is
    // substituted for the derived value in phase one.
    Motion(doc, "idle").object["selfSyncTime"] = N(1800);
    Motion(doc, "idle").object["syncTime"] = N(1900);
    Motion(doc, "terminal child") = MotionWith(A({Node(2, "unused", A({ContentFrame(1600)}))}));
    auto model = LoadModel(doc);
    CheckFrame(model->SyncFrame("actor", "idle"), 17,
               "maximum content key excludes terminal markers and empty content");
    CheckFrame(model->SyncFrame("missing", "idle"), -1, "unknown character has no sync");
    CheckFrame(model->SyncFrame("actor", "missing"), -1, "unknown motion has no sync");
    CheckFrame(model->SyncFrame("actor", "terminal child"), 1600,
               "terminal marker's content cannot pull a child sync into its parent");

    auto zero = LoadModel(Document(A({Node(2, "zero", A({ContentFrame(0), Key(1, 0)}))})));
    CheckFrame(zero->SyncFrame("actor", "idle"), 0, "frame zero is a known sync, not unknown");
}

void ParameterizeTests() {
    for (auto parameter : {N(0), O({{"id", S("expression")}, {"rangeBegin", N(0)},
                                    {"rangeEnd", N(100)}, {"division", N(100)}})}) {
        auto controlled = Node(2, "controlled", A({ContentFrame(0), ContentFrame(900)}));
        controlled.object["parameterize"] = parameter;
        auto group = Node(2, "controlled parent", A({ContentFrame(0), ContentFrame(800)}),
            A({Node(2, "uncontrolled descendant", A({ContentFrame(0), ContentFrame(23)}))}));
        group.object["parameterize"] = parameter;
        auto doc = Document(A({Node(2, "ordinary", A({ContentFrame(0), ContentFrame(7)})),
                               controlled, group}));
        auto model = LoadModel(doc);
        CheckFrame(model->SyncFrame("actor", "idle"), 23,
                   "node parameterize excludes its own keys, not nonparameterized descendants");

        Motion(doc, "idle").object["layer"] = A({controlled});
        model = LoadModel(doc);
        CheckFrame(model->SyncFrame("actor", "idle"), -1,
                   "parameterize presence, including numeric index zero, excludes all own keys");
    }

    auto parameters = A({O({{"id", S("expression")}, {"rangeBegin", N(0)},
                            {"rangeEnd", N(100)}, {"division", N(100)}})});
    auto doc = Document(A({Node(3, "controlled motion", A({
        ReferenceFrame(0, "motion/actor/controlled"), Key(1000, 0)})),
        Node(2, "ordinary", A({ContentFrame(0), ContentFrame(11)}))}));
    Motion(doc, "controlled") = MotionWith(A({Node(3, "child", A({
        ReferenceFrame(900, "motion/actor/deep")}))}));
    Motion(doc, "controlled").object["parameter"] = parameters;
    Motion(doc, "deep") = MotionWith(A({Node(2, "deep", A({ContentFrame(0), ContentFrame(950)}))}));
    for (auto parameter : {N(0), parameters.array.front()}) {
        Motion(doc, "controlled").object["parameterize"] = parameter;
        auto model = LoadModel(doc);
        CheckFrame(model->SyncFrame("actor", "controlled"), -1,
                   "motion-level parameterize excludes the entire motion, including references");
        CheckFrame(model->SyncFrame("actor", "idle"), 11,
                   "parameterized child motion does not inflate parent sync");
        CheckFrame(model->SyncFrame("actor", "deep"), 950,
                   "excluded child remains independently queryable");
        artc::EmotePlayer player;
        std::string error;
        Check(player.Load(model, error), "motion parameterize fixture passes player validation: " + error);
    }

    // `variable` is an authoring declaration, not motion-level parameterize.
    Motion(doc, "controlled").object.erase("parameterize");
    Motion(doc, "controlled").object["variable"] = S("expression");
    auto model = LoadModel(doc);
    CheckFrame(model->SyncFrame("actor", "controlled"), 950,
               "motion variable declaration alone does not exclude sync");
    CheckFrame(model->SyncFrame("actor", "idle"), 950,
               "removing motion parameterize restores recursive contribution");
}

void RecursiveOffsetTests() {
    auto doc = Document(A({Node(3, "child", A({
        ReferenceFrame(5, "motion/guest/middle", 9), Key(1000, 0)}))}));
    Motion(doc, "middle", "guest") = MotionWith(A({Node(3, "grandchild", A({
        ReferenceFrame(7, "motion/actor/deep", -3), Key(1000, 0)}))}));
    Motion(doc, "deep") = MotionWith(A({Node(2, "deep", A({
        ContentFrame(0), ContentFrame(41), Key(1000, 0)}))}));
    auto model = LoadModel(doc);
    CheckFrame(model->SyncFrame("actor", "deep"), 41, "grandchild local sync");
    CheckFrame(model->SyncFrame("guest", "middle"), 41, "cross-character recursive sync");
    CheckFrame(model->SyncFrame("actor", "idle"), 41, "recursive maximum reaches base motion");

    // Lock the current derivation contract, not an unproven completion time:
    // runtime child sampling uses parent-key+offset, but phase-one SyncFrame
    // takes the maximum of authored LOCAL content times without rebasing them.
    // Positive/negative timeOffset and nonzero reference keys must not silently
    // turn this diagnostic into a new end-of-animation policy.
    auto& reference = Motion(doc, "idle").object["layer"].array[0].object["frameList"].array[0];
    reference.object["content"].object["motion"].object["timeOffset"] = N(-20);
    auto changed = LoadModel(doc);
    CheckFrame(changed->SyncFrame("actor", "idle"), 41,
               "timeOffset does not rebase the phase-one local-time maximum");

    // Node parameterize excludes that node's keys, while recursion still
    // discovers independently timed child motions (unlike motion parameterize).
    auto& node = Motion(doc, "idle").object["layer"].array[0];
    node.object["parameterize"] = N(0);
    reference.object["time"] = N(900);
    auto controlled = LoadModel(doc);
    CheckFrame(controlled->SyncFrame("actor", "idle"), 41,
               "parameterized reference node excludes own time but follows child motion");

    node.object.erase("parameterize");
    reference.object["time"] = N(55);
    auto later_reference = LoadModel(doc);
    CheckFrame(later_reference->SyncFrame("actor", "idle"), 55,
               "reference's own content time can exceed child sync");

    // A repeated child reference is a DAG, not a cycle: the visiting guard
    // must be unwound between siblings and between top-level derivations.
    Motion(doc, "idle").object["layer"].array.push_back(Node(3, "shared child", A({
        ReferenceFrame(0, "motion/guest/middle", 30)})));
    auto shared = LoadModel(doc);
    for (int i = 0; i < 3; ++i) {
        CheckFrame(shared->SyncFrame("actor", "idle"), 55, "shared child leaves parent sync stable");
        CheckFrame(shared->SyncFrame("guest", "middle"), 41, "shared child remains independently derived");
    }
}

void SkipTests() {
    auto model = LoadModel(Document(A({Node(2, "base", A({
        ContentFrame(0), ContentFrame(17), Key(18, 0)}))})));
    std::string error;
    artc::EmotePlayer player;
    Check(player.Load(model, error), "load skip fixture: " + error);
    AdvanceFrames(player, 3);
    CheckFrame(player.BaseFrame(), 3, "skip precondition is genuinely not at sync");
    CheckFrame(model->SyncFrame("actor", "idle"), 17, "skip target is derived content key");
    Check(player.SetVariable("expression", 9, 1000, 0, error), "start unrelated variable transition");
    Check(player.PlayTimeline("once", 0, error), "start unrelated timeline");
    player.SkipToSync();
    CheckFrame(player.BaseFrame(), 17, "SkipToSync jumps from frame 3 to frame 17");
    bool found = false;
    CheckFrame(player.GetVariable("expression", &found), 0, "SkipToSync does not finish variable transitions");
    Check(found && player.IsTimelinePlaying("once"), "SkipToSync does not finish the timeline");
    for (int i = 0; i < 5; ++i) {
        player.SkipToSync();
        CheckFrame(player.BaseFrame(), 17, "repeated SkipToSync is idempotent");
        CheckFrame(model->SyncFrame("actor", "idle"), 17, "repeated sync queries remain stable");
    }
    AdvanceFrames(player, 2);
    CheckFrame(player.BaseFrame(), 19, "SkipToSync does not freeze subsequent progress");
    player.SkipToSync();
    CheckFrame(player.BaseFrame(), 17, "SkipToSync assigns the target even after progressing past it");

    auto zero = LoadModel(Document(A({Node(2, "zero", A({ContentFrame(0), Key(1, 0)}))})));
    Check(player.Load(zero, error), "load zero-sync player");
    AdvanceFrames(player, 3);
    player.SkipToSync();
    CheckFrame(player.BaseFrame(), 0, "zero sync is a real jump destination");
}

void NoSyncTests() {
    std::string error;
    artc::EmotePlayer empty;
    empty.SkipToSync();
    CheckFrame(empty.BaseFrame(), 0, "unloaded player's SkipToSync is safe");

    auto controlled = Node(2, "controlled", A({ContentFrame(0), ContentFrame(99), Key(100, 0)}));
    controlled.object["parameterize"] = N(0);
    auto parameter = A({O({{"id", S("expression")}, {"rangeBegin", N(0)},
                           {"rangeEnd", N(100)}, {"division", N(100)}})});
    for (auto nodes : {A({}), A({Node(2, "terminal only", A({Key(100, 0)}))}), A({controlled})}) {
        auto doc = Document(nodes);
        Motion(doc, "idle").object["parameter"] = parameter;
        Motion(doc, "idle").object["selfSyncTime"] = N(500);
        auto model = LoadModel(doc);
        CheckFrame(model->SyncFrame("actor", "idle"), -1, "empty/excluded/terminal-only motion has no sync");
        artc::EmotePlayer player;
        Check(player.Load(model, error), "load no-sync player: " + error);
        AdvanceFrames(player, 6);
        for (int i = 0; i < 3; ++i) {
            player.SkipToSync();
            CheckFrame(player.BaseFrame(), 6, "unknown sync is a no-op, with no self/last fallback");
        }
        AdvanceFrames(player, 2);
        CheckFrame(player.BaseFrame(), 8, "no-sync no-op preserves future clock progress");
    }
}

void TransactionTests() {
    const auto good_doc = Document(A({Node(2, "good", A({ContentFrame(0), ContentFrame(17)}))}));
    auto good = LoadModel(good_doc);
    std::string error;
    artc::EmotePlayer player;
    Check(player.Load(good, error), "load transaction baseline");
    AdvanceFrames(player, 3);
    Check(player.PlayTimeline("once", 0, error), "start transaction sentinel timeline");
    Check(player.SetVariable("expression", 7, 0, 0, error), "set transaction sentinel variable");

    for (const char* path : {"motion/actor/idle", "motion/actor/other", "motion/actor/missing"}) {
        auto doc = Document(A({Node(3, "reference", A({ReferenceFrame(5, path)}))}));
        if (std::string(path) == "motion/actor/other")
            Motion(doc, "other") = MotionWith(A({Node(3, "back", A({
                ReferenceFrame(9, "motion/actor/idle")}))}));
        auto invalid = LoadModel(doc);
        // EmoteModel derives data; it is not the playable graph validator.
        // Cycles terminate and missing children have unknown sync. The player
        // must reject the graph without replacing its previous playable state.
        const double expected = std::string(path) == "motion/actor/other" ? 9 : 5;
        for (int i = 0; i < 3; ++i)
            CheckFrame(invalid->SyncFrame("actor", "idle"), expected,
                       "cycle/missing reference derivation terminates deterministically");
        Check(!player.Load(invalid, error), std::string("player rejects invalid graph: ") + path);
        Check(!error.empty(), "invalid graph returns a diagnostic");
        CheckFrame(player.BaseFrame(), 3, "failed player Load preserves base clock");
        bool found = false;
        CheckFrame(player.GetVariable("expression", &found), 7, "failed player Load preserves variables");
        Check(found && player.IsTimelinePlaying("once"), "failed player Load preserves timeline state");
    }
    player.SkipToSync();
    CheckFrame(player.BaseFrame(), 17, "failed loads preserve original model and sync target");

    // A derivation error must not partially replace an already loaded model.
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) {
        auto doc = good_doc;
        Motion(doc, "idle").object["layer"].array[0].object["frameList"].array[1].object["time"] = N(invalid);
        Check(!good->Load(doc, error), "non-finite sync key fails model Load");
        Check(!error.empty(), "failed model Load returns a diagnostic");
        CheckFrame(good->SyncFrame("actor", "idle"), 17, "failed model Load preserves sync cache");
        CheckFrame(good->Document().root.At("object").At("actor").At("motion").At("idle")
                       .At("layer").array[0].At("frameList").array[1].At("time").number,
                   17, "failed model Load preserves document");
    }
    auto replacement = Document(A({Node(2, "replacement", A({ContentFrame(0), ContentFrame(29)}))}));
    Check(good->Load(replacement, error), "successful replacement after derivation failure");
    Check(error.empty(), "successful model Load clears error");
    for (int i = 0; i < 3; ++i)
        CheckFrame(good->SyncFrame("actor", "idle"), 29, "replacement cache has no stale sync");
    auto no_sync = Document(A({}));
    Check(good->Load(no_sync, error), "replace known sync with empty motion");
    CheckFrame(good->SyncFrame("actor", "idle"), -1, "replacement removes old sync cache entry");
}
artc::PsbValue Tag(double time, int type, artc::PsbValue sync = {}) {
    return Key(time, type, O({{"sync", std::move(sync)}}));
}

void CheckTagFrame(const artc::EmoteModel& model, double current, double expected,
                   const std::string& what, const char* motion = "idle",
                   const char* chara = "actor") {
    const auto value = model.NextMotionSyncFrame(chara, motion, current);
    Check(value.has_value(), what + " has a target");
    if (value) CheckFrame(*value, expected, what);
}

void MotionTagQueryTests() {
    auto doc = Document(A({Node(2, "legacy content", A({ContentFrame(0), ContentFrame(17)}))}));
    artc::PsbValue yes = N(1);yes.type = artc::PsbValue::Boolean;
    artc::PsbValue no = N(0);no.type = artc::PsbValue::Boolean;
    Motion(doc, "idle").object["tag"] = A({
        Tag(0, 1, yes), Tag(1, 0, yes), Tag(2, 2, yes),
        Tag(3, 1, no), Key(4, 1), Tag(5, 1), Tag(6, 1, N(0)),
        Tag(8, 1, N(1)), Tag(8, 1, yes), Tag(12, 1, yes), Tag(14, 9, yes)
    });
    Motion(doc, "idle").object["selfSyncTime"] = N(1800);
    Motion(doc, "idle").object["syncTime"] = N(1900);
    Motion(doc, "child") = MotionWith(A({}));
    Motion(doc, "child").object["tag"] = A({Tag(7, 1, yes)});
    auto model = LoadModel(doc);
    CheckTagFrame(*model, 0, 0, "tag at current zero is eligible");
    CheckTagFrame(*model, -1, 0, "finite current is compared without clamping");
    CheckTagFrame(*model, 0.25, 8, "filter type/false/missing sync; nearest ordered candidate");
    CheckTagFrame(*model, 8, 8, "equal-time candidate and duplicate timestamps are allowed");
    CheckTagFrame(*model, 8.25, 12, "multiple candidates select next, not final sync");
    CheckTagFrame(*model, 12, 12, "inclusive comparison at final sync");
    CheckTagFrame(*model, 13, 2000, "fallback is lastTime, not selfSyncTime/syncTime/legacy content");
    CheckTagFrame(*model, 2500, 2000, "fallback remains authored lastTime even behind current");
    CheckTagFrame(*model, 0, 7, "only requested motion's tags contribute", "child");
    CheckFrame(model->SyncFrame("actor", "idle"), 17, "tag query never replaces legacy derivation");
    CheckFrame(model->SyncFrame("actor", "child"), -1, "tag-only child has no legacy sync");
    Check(!model->NextMotionSyncFrame("absent", "idle", 0), "unknown character has no tag target");
    Check(!model->NextMotionSyncFrame("actor", "absent", 0), "unknown motion has no tag target");
    artc::EmoteModel unloaded;
    Check(!unloaded.NextMotionSyncFrame("actor", "idle", 0), "unloaded model has no tag target");
    for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()})
        Check(!model->NextMotionSyncFrame("actor", "idle", bad), "non-finite current has no target");
    for (int i = 0; i < 3; ++i)
        CheckTagFrame(*model, 8.25, 12, "read-only repeated query is stable");
    CheckFrame(model->Document().root.At("object").At("actor").At("motion").At("idle")
                   .At("tag").array[0].At("time").number, 0, "query preserves authored tags");

    for (auto tags : {A({}), A({Tag(3, 0, N(1)), Tag(5, 1, N(0)), Tag(9, 1)})}) {
        auto no_sync = Document(A({}));
        Motion(no_sync, "idle").object["tag"] = tags;
        auto empty = LoadModel(no_sync);
        CheckTagFrame(*empty, 4, 2000, "empty/no qualifying tag falls back to lastTime");
    }
    auto absent_tags = LoadModel(Document(A({})));
    CheckTagFrame(*absent_tags, 4, 2000, "absent tag array falls back to lastTime");
    for (double loop_time : {0.0, 3.0}) {
        auto looping = doc;
        Motion(looping, "idle").object["loopTime"] = N(loop_time);
        auto loop = LoadModel(looping);
        Check(!loop->NextMotionSyncFrame("actor", "idle", 8), "loop motion is no-op despite eligible tag");
        Check(!loop->NextMotionSyncFrame("actor", "idle", 99), "loop motion does not fall back to lastTime");
        CheckFrame(loop->SyncFrame("actor", "idle"), 17, "loop no-op is not imposed on legacy API");
    }
}

void MotionTagValidationTransactionTests() {
    auto baseline = Document(A({Node(2, "legacy", A({ContentFrame(17)}))}));
    Motion(baseline, "idle").object["tag"] = A({Tag(5, 1, N(1)), Tag(12, 1, N(1))});
    auto model = LoadModel(baseline);
    std::string error;
    auto rejected = [&](const artc::PsbDocument& invalid, const std::string& what) {
        Check(!model->Load(invalid, error), "local policy rejects " + what);
        Check(!error.empty(), "rejection explains " + what);
        CheckTagFrame(*model, 6, 12, "failed Load preserves tag cache: " + what);
        CheckTagFrame(*model, 13, 2000, "failed Load preserves fallback: " + what);
        CheckFrame(model->SyncFrame("actor", "idle"), 17, "failed Load preserves legacy cache: " + what);
        const auto& stored = model->Document().root.At("object").At("actor").At("motion").At("idle");
        Check(stored.At("tag").array.size() == 2, "failed Load preserves tag document: " + what);
        CheckFrame(stored.At("tag").array[0].At("time").number, 5,
                   "failed Load preserves original tag order: " + what);
    };
    // All these rejections are project safety rules, not native conformance
    // assertions for malformed input. Ordered tags are never silently sorted.
    for (auto bad : {N(3), O({}), S("tags")}) {
        auto invalid = baseline;
        Motion(invalid, "idle").object["tag"] = bad;
        rejected(invalid, "non-array tag");
    }
    for (auto bad : {N(3), A({}), artc::PsbValue{}}) {
        auto invalid = baseline;
        Motion(invalid, "idle").object["tag"] = A({bad});
        rejected(invalid, "non-object tag element");
    }
    for (const char* field : {"time", "type"}) {
        for (auto bad : {artc::PsbValue{}, S("1"), O({}), N(1.5),
                         N(std::numeric_limits<double>::quiet_NaN()),
                         N(std::numeric_limits<double>::infinity()),
                         N(-std::numeric_limits<double>::infinity())}) {
            // Fractional tag times are valid; fractional tag type is not.
            if (std::string(field) == "time" && bad.type == artc::PsbValue::Number && bad.number == 1.5)
                continue;
            auto invalid = baseline;
            Motion(invalid, "idle").object["tag"].array[0].object[field] = bad;
            rejected(invalid, std::string("invalid tag ") + field);
        }
    }
    for (auto bad : {S("content"), N(1), A({})}) {
        auto invalid = baseline;
        Motion(invalid, "idle").object["tag"].array[0].object["content"] = bad;
        rejected(invalid, "non-object content");
    }
    for (auto bad : {S("true"), O({}), A({}), N(2), N(-1),
                     N(std::numeric_limits<double>::quiet_NaN()),
                     N(std::numeric_limits<double>::infinity())}) {
        auto invalid = baseline;
        Motion(invalid, "idle").object["tag"].array[0].object["content"].object["sync"] = bad;
        rejected(invalid, "invalid sync flag");
    }
    auto unordered = baseline;
    Motion(unordered, "idle").object["tag"] = A({Tag(12, 1, N(1)), Tag(5, 1, N(1))});
    rejected(unordered, "unordered tags instead of guessing native minimum selection");
    for (const char* field : {"lastTime", "loopTime"}) {
        auto invalid = baseline;
        Motion(invalid, "idle").object[field] = N(std::numeric_limits<double>::infinity());
        rejected(invalid, std::string("non-finite ") + field);
    }

    auto replacement = baseline;
    Motion(replacement, "idle").object["tag"] = A({Tag(2.5, 1, N(1))});
    Motion(replacement, "idle").object["lastTime"] = N(99);
    Check(model->Load(replacement, error) && error.empty(), "valid replacement clears previous tag error");
    CheckTagFrame(*model, 2, 2.5, "replacement installs fractional-time tag");
    CheckTagFrame(*model, 3, 99, "replacement removes stale candidates and fallback");
    Motion(replacement, "idle").object.erase("tag");
    Check(model->Load(replacement, error), "replace tags with absent array");
    CheckTagFrame(*model, 0, 99, "absent replacement removes old tag cache");
    Motion(replacement, "idle").object["loopTime"] = N(0);
    Check(model->Load(replacement, error), "replace normal motion with loop");
    Check(!model->NextMotionSyncFrame("actor", "idle", 0), "replacement loop no-op has no stale fallback");
}
} // namespace

int main() {
    ContentAndTerminalTests();
    ParameterizeTests();
    RecursiveOffsetTests();
    SkipTests();
    NoSyncTests();
    TransactionTests();
    MotionTagQueryTests();
    MotionTagValidationTransactionTests();
    if (g_failures == 0) std::cout << "emote_sync_regressions: ok\n";
    return g_failures == 0 ? 0 : 1;
}
