// emote_mesh_regressions.cpp — E-mote mesh patch parsing/sampling/deformation.

#include "render/emote_mesh.h"
#include "render/emote_combinator.h"
#include "pack/psb.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;
void Check(bool ok, const std::string &what) {
    if (!ok) { ++g_failures; std::cerr << "FAIL: " << what << "\n"; }
}
bool Close(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Identity NxN grid: point (i,j) = (i/(N-1), j/(N-1)).
std::vector<float> Identity(int n) {
    std::vector<float> p;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            p.push_back(static_cast<float>(i) / (n - 1));
            p.push_back(static_cast<float>(j) / (n - 1));
        }
    return p;
}

void TestParse() {
    int side = 0;
    Check(artc::ParseMeshPatch(Identity(4), &side) && side == 4, "4x4 patch parses");
    Check(artc::ParseMeshPatch(Identity(3), &side) && side == 3, "3x3 patch parses");
    std::vector<float> bad = {0, 0, 1, 1}; // 2 points -> not a square
    Check(!artc::ParseMeshPatch(bad, &side), "non-square point count rejected");
    Check(!artc::ParseMeshPatch({0, 0, 1}, &side), "odd length rejected");
}

void TestIdentityBilinear() {
    const auto grid = Identity(3);
    int side = 0;
    artc::ParseMeshPatch(grid, &side);
    for (float u : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float xy[2] = {0, 0};
        Check(artc::SampleMeshPatch(grid, side, u, u, xy), "sample succeeds");
        Check(Close(xy[0], u) && Close(xy[1], u), "identity grid samples to itself");
    }
}

void TestBezierCorners() {
    // 4x4 control points: identity corners, midpoint bulge upward.
    auto grid = Identity(4);
    // Move interior points to create a known center deformation.
    // Center control point (1,1) is index (1*4+1)*2.
    grid[(1 * 4 + 1) * 2 + 1] = 0.5f;
    int side = 0;
    artc::ParseMeshPatch(grid, &side);
    float corner[2] = {0, 0};
    Check(artc::SampleMeshPatch(grid, side, 0.0f, 0.0f, corner), "corner sample");
    Check(Close(corner[0], 0.0f) && Close(corner[1], 0.0f), "bezier corner preserved");
    float far[2] = {0, 0};
    Check(artc::SampleMeshPatch(grid, side, 1.0f, 1.0f, far), "far corner sample");
    Check(Close(far[0], 1.0f) && Close(far[1], 1.0f), "bezier far corner preserved");
}

void TestBuildGrid() {
    const auto grid = Identity(4);
    int side = 0;
    artc::ParseMeshPatch(grid, &side);
    std::vector<float> xy;
    std::vector<uint32_t> indices;
    Check(artc::BuildDeformedGrid(grid, side, artc::kEmoteMeshSide, &xy, &indices),
          "build deformed grid");
    Check(xy.size() == static_cast<size_t>(artc::kEmoteMeshSide) * artc::kEmoteMeshSide * 2,
          "vertex count");
    const int cells = artc::kEmoteMeshSide - 1;
    Check(indices.size() == static_cast<size_t>(cells) * cells * 6, "index count");
    // Identity patch -> first vertex at (0,0), last at (1,1).
    Check(Close(xy.front(), 0.0f) && Close(xy[1], 0.0f), "first vertex");
    Check(Close(xy[xy.size() - 2], 1.0f) && Close(xy.back(), 1.0f), "last vertex");
}

void TestBuildWarpedMesh() {
    const auto grid = Identity(4);
    int side = 0;
    artc::ParseMeshPatch(grid, &side);
    std::vector<float> xyuv;
    Check(artc::BuildWarpedMesh(grid, side, artc::kEmoteMeshSide, &xyuv), "build warped mesh");
    const int cells = artc::kEmoteMeshSide - 1;
    Check(xyuv.size() == static_cast<size_t>(cells) * cells * 6 * 4, "warped mesh size");
    // First triangle's first vertex: identity position and identity uv.
    Check(Close(xyuv[0], 0.0f) && Close(xyuv[1], 0.0f), "warped first position");
    Check(Close(xyuv[2], 0.0f) && Close(xyuv[3], 0.0f), "warped first uv");
}

using Grid = artc::EmoteCombinatorGrid;
using Entry = artc::EmoteCombinatorEntry;
using Combinator = artc::EmoteMeshCombinator;

Grid IdentityGrid() {
    Grid grid;
    const auto identity = Identity(4);
    std::copy(identity.begin(), identity.end(), grid.begin());
    return grid;
}

Grid Perturbation() {
    Grid grid;
    for (size_t i = 0; i < grid.size(); ++i)
        grid[i] = (static_cast<float>(i) - 11) * 0.025f;
    return grid;
}

Grid Add(const Grid &a, const Grid &b, float weight = 1) {
    Grid grid;
    for (size_t i = 0; i < grid.size(); ++i) grid[i] = a[i] + weight * b[i];
    return grid;
}

bool CloseGrid(const Grid &a, const Grid &b) {
    for (size_t i = 0; i < a.size(); ++i)
        if (!Close(a[i], b[i])) return false;
    return true;
}

Entry MakeEntry(std::string key, std::vector<Grid> meshes, double begin = -1, double end = 1) {
    Entry entry;
    entry.variable_key = std::move(key);
    entry.meshes = std::move(meshes);
    entry.range_begin = begin;
    entry.range_end = end;
    return entry;
}

void TestCombinatorMath() {
    const auto identity = IdentityGrid(), a = Perturbation();
    auto base = MakeEntry("base", {Add(identity, a, -1), identity, Add(identity, a)});
    auto delta = MakeEntry("delta", {Add(Grid{}, a, -2), Grid{}, Add(Grid{}, a, 2)});
    Combinator c{{base, delta}};
    Grid output{};
    std::string error;
    auto evaluate = [&](double x, double y, const Grid &expected, const std::string &label) {
        Check(artc::EvaluateEmoteCombinator(c, {{"base", x}, {"delta", y}}, output, error), label + " evaluates");
        Check(CloseGrid(output, expected), label + " all components");
    };
    evaluate(-1, 0, Add(identity, a, -1), "I-A");
    evaluate(1, 0, Add(identity, a), "I+A");
    evaluate(0.25, -0.5, Add(identity, a, -0.75f), "absolute plus delta, not extra identity");
    evaluate(1, 1, Add(identity, a, 3), "upper endpoints do not read past final mesh");
    evaluate(-100, 100, Add(identity, a), "clamped variable endpoints");
    // Every legal neutral index is metadata, including -1. None is subtracted.
    for (int neutral = -1; neutral < 3; ++neutral) {
        c.entries[0].neutral_index = neutral;
        c.entries[1].neutral_index = 2 - std::max(neutral, 0);
        evaluate(1, 1, Add(identity, a, 3), "neutral metadata " + std::to_string(neutral));
    }
    for (int i = 0; i < 20; ++i) {
        evaluate(1, -1, Add(identity, a, -1), "variable change without drift A");
        evaluate(-1, 1, Add(identity, a), "variable change without drift B");
    }
    Check(artc::EvaluateEmoteCombinator(c, {}, output, error) && CloseGrid(output, identity),
          "missing variables use zero, not first mesh or neutral index");
    std::vector<Grid> five;
    for (int i = 0; i < 5; ++i) five.push_back(Add(identity, a, static_cast<float>(i * i)));
    c = {{MakeEntry("five", five, 10, -10)}};
    Check(artc::EvaluateEmoteCombinator(c, {{"five", -2.5}}, output, error) &&
          CloseGrid(output, Add(identity, a, 6.5f)), "five meshes reverse range piecewise linear q=2.5");
    Check(artc::EvaluateEmoteCombinator(c, {}, output, error) &&
          CloseGrid(output, Add(identity, a, 4)), "missing zero maps through reversed range");
    for (int i = 0; i < 5; ++i) {
        Check(artc::EvaluateEmoteCombinator(c, {{"five", 10 - i * 5}}, output, error) &&
              CloseGrid(output, five[i]), "five mesh exact knot " + std::to_string(i));
    }
    c = {{MakeEntry("single", {Add(identity, a, 9)}, 2, 3)}};
    Check(artc::EvaluateEmoteCombinator(c, {}, output, error) && CloseGrid(output, c.entries[0].meshes[0]),
          "one mesh is constant and finite control coordinates are not clamped");
    const double largest = std::numeric_limits<double>::max();
    c = {{MakeEntry("extreme", {identity, Add(identity, a)}, -largest, largest)}};
    Check(artc::EvaluateEmoteCombinator(c, {}, output, error) && CloseGrid(output, Add(identity, a, 0.5f)),
          "finite extreme range avoids subtraction overflow");
    c.entries[0].range_begin = 1;
    c.entries[0].range_end = 2;
    Check(artc::EvaluateEmoteCombinator(c, {{"extreme", largest}}, output, error) &&
          CloseGrid(output, Add(identity, a)), "extreme finite variable clamps before range math");
}

artc::PsbValue Number(double n) {
    artc::PsbValue value;
    value.type = artc::PsbValue::Number;
    value.number = n;
    return value;
}
artc::PsbValue String(const std::string &s) {
    artc::PsbValue value;
    value.type = artc::PsbValue::String;
    value.string = s;
    return value;
}
artc::PsbValue Object(std::map<std::string, artc::PsbValue> members) {
    artc::PsbValue value;
    value.type = artc::PsbValue::Object;
    value.object = std::move(members);
    return value;
}
artc::PsbValue Resource(artc::PsbDocument &doc, const std::vector<Grid> &grids, bool extra) {
    const size_t start = doc.bytes.size();
    for (const auto &grid : grids) {
        for (float value : grid) {
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            for (int shift = 0; shift < 32; shift += 8) doc.bytes.push_back(static_cast<uint8_t>(bits >> shift));
        }
    }
    auto &bank = extra ? doc.extra_resources : doc.resources;
    artc::PsbValue ref;
    ref.type = artc::PsbValue::Resource;
    ref.extra = extra;
    ref.resource = static_cast<uint32_t>(bank.size());
    bank.emplace_back(start, doc.bytes.size() - start);
    return ref;
}
artc::PsbValue ResourceEntry(artc::PsbDocument &doc, const Entry &entry, bool extra) {
    return Object({{"meshType", Number(entry.mesh_type)}, {"neutralIndex", Number(entry.neutral_index)},
        {"rawMeshList", Resource(doc, entry.meshes, extra)},
        {"variable", Object({{"key", String(entry.variable_key)}, {"meshCount", Number(entry.meshes.size())},
                            {"rangeBegin", Number(entry.range_begin)}, {"rangeEnd", Number(entry.range_end)}})}});
}
artc::PsbValue Field(std::vector<artc::PsbValue> entries) {
    artc::PsbValue list;
    list.type = artc::PsbValue::Array;
    list.array = std::move(entries);
    return Object({{"combinatorList", std::move(list)}});
}

void TestCombinatorResources() {
    // All bytes constructed here; no asset/filesystem input or host endianness assumption.
    artc::PsbDocument doc;
    doc.version = 4;
    const auto identity = IdentityGrid(), a = Perturbation();
    const auto base = MakeEntry("x", {Add(identity, a, -1), Add(identity, a)});
    const auto delta = MakeEntry("y", {Grid{}, a}, 0, 1);
    auto field = Field({ResourceEntry(doc, base, true), ResourceEntry(doc, delta, false)});
    std::string error;
    Combinator parsed;
    Grid output{};
    Check(artc::ParseEmoteCombinator(doc, field, parsed, error), "normal and extra resources parse via document accessor");
    Check(parsed.entries.size() == 2 && parsed.entries[0].meshes[1] == base.meshes[1] &&
          parsed.entries[1].meshes[1] == a, "float32LE row-major xy resource decode exact, no transpose");
    Check(artc::EvaluateEmoteCombinator(doc, field, {{"x", 0.5}, {"y", 0.25}}, output, error) &&
          CloseGrid(output, Add(identity, a, 0.75f)), "resource evaluation sums absolute and delta samples");
    Check(error.empty(), "successful operation clears stale errors");
    const auto initial = output;
    auto reject = [&](const artc::PsbDocument &bad_doc, const artc::PsbValue &bad, const std::string &label) {
        Combinator retained = parsed;
        Check(!artc::ParseEmoteCombinator(bad_doc, bad, retained, error) && !error.empty(), label + " parse rejects with reason");
        Check(retained.entries.size() == parsed.entries.size() &&
              retained.entries[0].meshes == parsed.entries[0].meshes, label + " parse transactional");
        output = initial;
        Check(!artc::EvaluateEmoteCombinator(bad_doc, bad, {}, output, error) && output == initial,
              label + " evaluate transactional");
    };
    // Corrupt the SECOND item: a valid prefix must never leak into output.
    auto mutate = [&](const std::string &key, artc::PsbValue value, bool variable, const std::string &label) {
        auto bad = field;
        auto &item = bad.object["combinatorList"].array[1];
        if (variable) item.object["variable"].object[key] = std::move(value);
        else item.object[key] = std::move(value);
        reject(doc, bad, label);
    };
    for (double n : {0., -1., 1.5, 3., std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN()})
        mutate("meshCount", Number(n), true, "invalid mesh count");
    for (double n : {-2., 2., 0.5, std::numeric_limits<double>::quiet_NaN()})
        mutate("neutralIndex", Number(n), false, "invalid neutral index");
    mutate("meshType", Number(2), false, "unsupported mesh type");
    mutate("meshType", String("1"), false, "wrong mesh type tag");
    mutate("key", Number(1), true, "wrong variable key type");
    mutate("rangeEnd", Number(0), true, "degenerate range");
    mutate("rangeBegin", Number(std::numeric_limits<double>::infinity()), true, "nonfinite range");
    mutate("rawMeshList", Number(0), false, "nonresource rejected");
    reject(doc, Field({}), "empty list");
    reject(doc, artc::PsbValue{}, "missing field");
    auto bad_doc = doc;
    bad_doc.resources[0].second -= 1;
    reject(bad_doc, field, "non128 resource length");
    bad_doc = doc;
    bad_doc.resources[0].first = bad_doc.bytes.size() + 1;
    reject(bad_doc, field, "accessor resource bounds");
    bad_doc = doc;
    bad_doc.resources.clear();
    reject(bad_doc, field, "bad bank index");
    for (float bad_value : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto bad_grids = delta;
        bad_grids.meshes[1][7] = bad_value;
        auto bad_field = Field({field.At("combinatorList").array[0], ResourceEntry(doc, bad_grids, true)});
        reject(doc, bad_field, "nonfinite resource float");
    }
}

void TestCombinatorValidationAndComposition() {
    const auto identity = IdentityGrid(), a = Perturbation();
    const Grid sentinel = Add(identity, a, 10);
    Grid output = sentinel;
    std::string error;
    Combinator valid{{MakeEntry("x", {identity, Add(identity, a)}, 0, 1)}};
    for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        Check(!artc::EvaluateEmoteCombinator(valid, {{"x", bad}}, output, error) && output == sentinel && !error.empty(),
              "nonfinite variable rejected transactionally");
    }
    auto reject = [&](Combinator c, const std::string &label) {
        Check(!artc::EvaluateEmoteCombinator(c, {}, output, error) && output == sentinel && !error.empty(), label);
    };
    reject({}, "empty typed list rejects");
    auto bad = valid; bad.entries[0].meshes.clear(); reject(bad, "empty typed meshes reject");
    bad = valid; bad.entries[0].mesh_type = 0; reject(bad, "typed unknown meshType rejects");
    bad = valid; bad.entries[0].neutral_index = 2; reject(bad, "typed invalid neutral rejects");
    bad = valid; bad.entries[0].range_end = 0; reject(bad, "typed degenerate range rejects");
    bad = valid; bad.entries[0].meshes[0][0] = std::numeric_limits<float>::infinity();
    reject(bad, "typed nonfinite control rejects");
    Grid enormous; enormous.fill(std::numeric_limits<float>::max());
    reject(Combinator{{MakeEntry("a", {enormous}), MakeEntry("b", {enormous})}}, "sum overflow rejects");
    const auto child = Add(identity, a, 2), parent = Add(identity, a, -0.5f);
    Check(artc::CombineSameDomainEmoteGrids(child, parent, output, error) &&
          CloseGrid(output, Add(identity, a, 1.5f)), "same-domain child+parent-I (no double identity)");
    Grid alias = child;
    Check(artc::CombineSameDomainEmoteGrids(alias, parent, alias, error) && alias == output,
          "same-domain helper permits output alias");
    Grid invalid = child; invalid[4] = std::numeric_limits<float>::quiet_NaN();
    const Grid previous = output;
    Check(!artc::CombineSameDomainEmoteGrids(invalid, parent, output, error) && output == previous,
          "same-domain invalid input transactional");
    const auto maximum = std::numeric_limits<float>::max();
    Grid negative; negative.fill(-maximum);
    Combinator endpoint{{MakeEntry("x", {negative, enormous}, 0, 1)}};
    Check(artc::EvaluateEmoteCombinator(endpoint, {{"x", 0}}, output, error) && output == negative,
          "extreme finite lower endpoint does not overflow lerp subtraction");
    Check(artc::EvaluateEmoteCombinator(endpoint, {{"x", 1}}, output, error) && output == enormous,
          "extreme finite upper endpoint does not overflow lerp subtraction");
    output = sentinel;
    Check(!artc::CombineSameDomainEmoteGrids(enormous, enormous, output, error) && output == sentinel,
          "same-domain finite arithmetic overflow leaves output unchanged");
}

void TestCombinatorBudgets() {
    artc::PsbDocument doc;
    // 32 KiB backing resource reused 2049 times: just over 64 MiB decoded,
    // but the test only builds the small resource and reference metadata.
    const size_t meshes = 256;
    auto item = ResourceEntry(doc, MakeEntry("budget", std::vector<Grid>(meshes)), false);
    const size_t repeats = artc::kEmoteCombinatorMaxDecodedBytes / (meshes * 128) + 1;
    Check(repeats <= artc::kEmoteCombinatorMaxEntries, "cumulative test reaches byte budget before entry budget");
    const auto identity = IdentityGrid();
    Combinator retained{{MakeEntry("sentinel", {identity})}};
    Grid output = identity;
    std::string error;
    auto reject = [&](const artc::PsbValue &field, const std::string &reason) {
        Check(!artc::ParseEmoteCombinator(doc, field, retained, error) && error.find(reason) != std::string::npos,
              reason + " fails in whole-list preflight");
        Check(retained.entries.size() == 1 && retained.entries[0].variable_key == "sentinel" &&
              retained.entries[0].meshes[0] == identity, reason + " preserves parsed output");
        Check(!artc::EvaluateEmoteCombinator(doc, field, {}, output, error) && output == identity,
              reason + " preserves evaluated output");
    };
    // The first mesh is deliberately nonfinite: a streaming decode would fail
    // there, while a whole-list preflight must report the later budget breach
    // without copying/decoding even this first resource.
    doc.bytes[2] = 0x80;
    doc.bytes[3] = 0x7f;
    reject(Field(std::vector<artc::PsbValue>(repeats, item)), "cumulative decoded byte budget");
    doc.bytes[2] = doc.bytes[3] = 0;
    reject(Field(std::vector<artc::PsbValue>(artc::kEmoteCombinatorMaxEntries + 1, item)), "entry count budget");
    auto bad = item;
    bad.object["variable"].object["meshCount"] = Number(artc::kEmoteCombinatorMaxEntryBytes / 128 + 1);
    reject(Field({bad}), "entry decoded byte budget");
    bad.object["variable"].object["meshCount"] = Number(std::numeric_limits<int>::max());
    reject(Field({bad}), "entry decoded byte budget");
    bad.object["variable"].object["meshCount"] = Number(1e100);
    reject(Field({bad}), "invalid variable key or meshCount");
    bad = item;
    bad.object["variable"].object["meshCount"] = Number(1);
    reject(Field({bad}), "length does not match");
    // Forged gigantic length must not be copied, regardless of expected count.
    doc.resources[0].second = std::numeric_limits<size_t>::max();
    reject(Field({item}), "resource bounds");
    doc.resources[0].second = meshes * 128;
    Combinator typed;
    typed.entries.resize(artc::kEmoteCombinatorMaxEntries + 1);
    Check(!artc::EvaluateEmoteCombinator(typed, {}, output, error) && output == identity &&
          error.find("entry count budget") != std::string::npos, "typed evaluation enforces entry budget");
}

void TestBernsteinExtrapolation() {
    auto points = Identity(4);
    // Deliberately nonsymmetric: B2(u)*B1(v) in x, B0(u)*B3(v) in y.
    points[(1 * 4 + 2) * 2] += 0.7f;
    points[(3 * 4 + 0) * 2 + 1] -= 0.4f;
    auto expected = [](float u, float v) {
        return std::pair<float, float>{u + 0.7f * (3 * (1-u) * u*u) * (3 * (1-v)*(1-v)*v),
                                      v - 0.4f * (1-u)*(1-u)*(1-u) * v*v*v};
    };
    for (const auto uv : {std::pair<float,float>{0.2f, 0.7f}, {0.65f, 0.3f},
                           {-0.25f, 1.2f}, {1.3f, -0.4f}, {0, 0}, {1, 1}}) {
        float xy[2] = {99, 98};
        Check(artc::SampleCubicMeshPatchExtrapolated(points, uv.first, uv.second, xy), "explicit cubic sample succeeds");
        const auto value = expected(uv.first, uv.second);
        Check(Close(xy[0], value.first) && Close(xy[1], value.second),
              "known Bernstein tensor polynomial, asymmetric row/column order and domain-out");
    }
    float clamped[2] = {99, 98};
    Check(artc::SampleMeshPatch(points, 4, -0.25f, 1.2f, clamped) &&
          Close(clamped[0], 0) && Close(clamped[1], 0.6f), "legacy sampling still clamps to (0,1)");
    for (float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        float xy[2] = {99, 98};
        Check(!artc::SampleCubicMeshPatchExtrapolated(points, bad, 0, xy) && xy[0] == 99 && xy[1] == 98,
              "nonfinite extrapolation parameter rejected transactionally");
        auto invalid = points; invalid[1] = bad;
        Check(!artc::SampleCubicMeshPatchExtrapolated(invalid, 0, 0, xy), "nonfinite extrapolation control rejected");
    }
    Check(!artc::SampleCubicMeshPatchExtrapolated(Identity(3), 0, 0, clamped), "extrapolation only supports cubic patch");
    Check(!artc::SampleCubicMeshPatchExtrapolated(points, 0, 0, nullptr), "extrapolation null output rejected");
    float overflow[2] = {99, 98};
    Check(!artc::SampleCubicMeshPatchExtrapolated(points, std::numeric_limits<float>::max(), 0.5f, overflow) &&
          overflow[0] == 99 && overflow[1] == 98, "finite domain-out polynomial overflow preserves output");
    auto enormous = Identity(4);
    enormous[(1 * 4 + 2) * 2] = std::numeric_limits<float>::max();
    Check(!artc::SampleCubicMeshPatchExtrapolated(enormous, -1, 2, overflow) &&
          overflow[0] == 99 && overflow[1] == 98, "finite control extrapolation overflow preserves output");
}

} // namespace

int main() {
    TestParse();
    TestIdentityBilinear();
    TestBezierCorners();
    TestBuildGrid();
    TestBuildWarpedMesh();
    TestCombinatorMath();
    TestCombinatorResources();
    TestCombinatorValidationAndComposition();
    TestCombinatorBudgets();
    TestBernsteinExtrapolation();
    if (g_failures == 0) std::cout << "emote_mesh_regressions: ok\n";
    return g_failures == 0 ? 0 : 1;
}
