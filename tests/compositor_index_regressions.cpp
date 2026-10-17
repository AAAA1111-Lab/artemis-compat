// Layer id index / transform memoization regressions.
//
// The compositor resolves ancestors, owners and ids through a lazily rebuilt
// id->layer index and memoizes world transforms inside TransformCache scopes.
// These tests pin (a) agreement with the original dotted-prefix ancestor walk,
// (b) correct invalidation on every layer-table mutation, and (c) complexity
// through Compositor::Counters() instead of wall clocks.
#include "render/compositor.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using artc::Compositor;
using artc::Layer;

static void Check(bool ok, const char* why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}

static void Make(Compositor& c, const std::string& id, float x, float y,
                 bool texture = true, float rotate = 0, float scale = 100,
                 float alpha = 255) {
    c.SetProps(id, {{"left", std::to_string(x)}, {"top", std::to_string(y)},
                    {"rotate", std::to_string(rotate)},
                    {"xscale", std::to_string(scale)}, {"yscale", std::to_string(scale)},
                    {"alpha", std::to_string(alpha)}});
    if (texture) {
        static const uint8_t pixels[4 * 4 * 4] = {255};
        Check(c.SetPixels(id, pixels, 4, 4), "create textured layer");
    }
}

// The original algorithm: for every proper dotted prefix, the first layer in
// table order with exactly that id joins the chain, root first.
static Compositor::Transform Reference(const Compositor& c, const Layer& l) {
    std::vector<const Layer*> chain;
    std::string id = l.id;
    size_t dot;
    while ((dot = id.rfind('.')) != std::string::npos) {
        id.resize(dot);
        for (const auto& parent : c.Layers())
            if (parent.id == id) { chain.push_back(&parent); break; }
    }
    Compositor::Transform m;
    auto apply = [&](const Layer& n) {
        const float radians = std::remainder(n.rotate, 360.f) * 3.14159265358979323846f / 180.f;
        float co = std::cos(radians), si = std::sin(radians);
        if (std::abs(co) < 1e-7f) co = 0;
        if (std::abs(si) < 1e-7f) si = 0;
        const float sx = n.sx * (n.reverse_x ? -1.f : 1.f);
        const float sy = n.sy * (n.reverse_y ? -1.f : 1.f);
        const float a = n.affine ? n.affine_a : co * sx, b = n.affine ? n.affine_b : si * sx;
        const float cc = n.affine ? n.affine_c : -si * sy, d = n.affine ? n.affine_d : co * sy;
        const float tx = n.x + n.ax - a * n.ax - cc * n.ay;
        const float ty = n.y + n.ay - b * n.ax - d * n.ay;
        const auto origin = m.Point(tx, ty);
        const float ma = m.a * a + m.c * b, mb = m.b * a + m.d * b;
        const float mc = m.a * cc + m.c * d, md = m.b * cc + m.d * d;
        m.a = ma; m.b = mb; m.c = mc; m.d = md; m.tx = origin.first; m.ty = origin.second;
        m.alpha *= n.alpha; m.visible = m.visible && n.visible;
    };
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) apply(**it);
    apply(l);
    const auto origin = m.Point(l.content_x, l.content_y);
    m.tx = origin.first; m.ty = origin.second;
    return m;
}

static bool Near(float a, float b) { return std::abs(a - b) <= 1e-3f * (1 + std::abs(a)); }
static bool Same(const Compositor::Transform& a, const Compositor::Transform& b) {
    return Near(a.a, b.a) && Near(a.b, b.b) && Near(a.c, b.c) && Near(a.d, b.d) &&
           Near(a.tx, b.tx) && Near(a.ty, b.ty) && Near(a.alpha, b.alpha) &&
           a.visible == b.visible;
}

static void CheckAgainstReference(const Compositor& c, const char* why) {
    for (const auto& l : c.Layers())
        Check(Same(c.EffectiveTransform(l), Reference(c, l)), why);
    {
        const Compositor::TransformCache cache(c);
        // Two passes: the second one is served entirely from the memo.
        for (int pass = 0; pass < 2; ++pass)
            for (const auto& l : c.Layers())
                Check(Same(c.EffectiveTransform(l), Reference(c, l)), why);
    }
    // Detached probes (the ParentDelta / HomeLayerToTextPen idiom).
    for (const auto& l : c.Layers()) {
        Layer probe;
        probe.id = l.id + ".probe";
        probe.x = 3;
        Check(Same(c.EffectiveTransform(probe), Reference(c, probe)), why);
    }
}

static float Tx(const Compositor& c, const std::string& id) {
    for (const auto& l : c.Layers())
        if (l.id == id) return c.EffectiveTransform(l).tx;
    return std::nanf("");
}

static std::vector<std::string> Sorted(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    return v;
}

static void Equivalence() {
    Compositor c;
    c.Init(1920, 1080);
    // Sparse tree: "a.b" is missing, so "a.b.c" inherits from "a" only.
    Make(c, "a", 10, 20, true, 15, 120);
    Make(c, "a.b.c", 1, 2, true, -30, 80, 128);
    Make(c, "a.b.c.d", 5, 6, true, 90, 100);
    Make(c, "a.x", 7, 8, false, 0, 150);
    Make(c, "a.x.y", 3, 4, true, 45, 100);
    Make(c, "q.r", 9, 9);
    c.SetProps("a.x", {{"visible", "0"}});
    Check(c.SetLayerAffine("a.b.c", 1.5f, .25f, -.5f, 2.f, 11, 12), "affine layer");
    c.SetProps("a.x.y", {{"alpha", "100"}, {"reversex", "1"}});
    CheckAgainstReference(c, "sparse tree matches the original ancestor walk");
    // Edge ids: leading/trailing/double dots.
    Make(c, "..z", 1, 1);
    Make(c, "a..m", 2, 2);
    Make(c, "a.", 3, 3);
    CheckAgainstReference(c, "odd dotted ids match the original ancestor walk");
}

static void Invalidation() {
    {   // Property edits, rename of a root, reuse of the old id, middle rename.
        Compositor c;
        c.Init(1920, 1080);
        Make(c, "a", 10, 0);
        Make(c, "a.b", 1, 0);
        Make(c, "a.b.c", 2, 0);
        Check(Tx(c, "a.b.c") == 13, "baseline chain");
        {
            const Compositor::TransformCache cache(c);
            Check(Tx(c, "a.b.c") == 13, "scoped baseline");
        }
        c.SetProps("a", {{"left", "40"}});
        Check(Tx(c, "a.b.c") == 43, "property edit after a cached scope is visible");
        {
            const Compositor::TransformCache cache(c);
            Check(Tx(c, "a.b.c") == 43, "next scope recomputes");
        }
        Check(c.RenameLayer("a", "z"), "rename subtree root");
        Check(Tx(c, "z.b.c") == 43 && std::isnan(Tx(c, "a.b.c")),
              "renamed subtree keeps its ancestors");
        Make(c, "a", 1000, 0);
        Check(Tx(c, "z.b.c") == 43 && Tx(c, "a") == 1000,
              "recreated old id does not adopt renamed children");
        Check(c.RenameLayer("z.b", "q"), "rename a middle node");
        Check(Tx(c, "q.c") == 3 && Tx(c, "q") == 1, "detached subtree loses the former parent");
        CheckAgainstReference(c, "after renames");
    }
    {   // Rename onto an existing id: the first layer in table order stays the
        // ancestor, exactly like the original first-match walk.
        Compositor c;
        c.Init(1920, 1080);
        Make(c, "w", 1000, 0);
        Make(c, "p", 500, 0);
        Make(c, "p.k", 1, 0);
        Check(Tx(c, "p.k") == 501, "baseline before duplicate");
        Check(c.RenameLayer("w", "p"), "rename onto existing id");
        Check(Tx(c, "p.k") == 1001, "first duplicate (table order) is the ancestor");
        CheckAgainstReference(c, "duplicate ids match the original walk");
    }
    {   // Delete and recreate, parents appearing after their children.
        Compositor c;
        c.Init(1920, 1080);
        Make(c, "z", 50, 0);
        Make(c, "z.b", 5, 0);
        Make(c, "z.b.c", 2, 0);
        Check(Tx(c, "z.b.c") == 57, "baseline");
        c.DeleteLayer("z.b");
        Check(std::isnan(Tx(c, "z.b.c")) && std::isnan(Tx(c, "z.b")) && Tx(c, "z") == 50,
              "deleted subtree is gone");
        Make(c, "z.b.c", 2, 0);
        Check(Tx(c, "z.b.c") == 52, "child skips the missing middle ancestor");
        Make(c, "z.b", 5, 0);
        Check(Tx(c, "z.b.c") == 57, "middle ancestor created after its child joins the chain");
        CheckAgainstReference(c, "parent after child in table order");
        c.DeleteLayer("z");
        Check(c.Layers().empty(), "group delete removed the whole tree");
        Make(c, "z.b.c", 2, 0);
        Check(Tx(c, "z.b.c") == 2, "recreated child no longer sees the deleted parents");
        Make(c, "z", 9, 0);
        Check(Tx(c, "z.b.c") == 11, "late root joins existing children");
    }
    {   // Same ids recreated after delete pick up the new values.
        Compositor c;
        c.Init(1920, 1080);
        Make(c, "r", 7, 0);
        Make(c, "r.s", 1, 0);
        Check(Tx(c, "r.s") == 8, "baseline");
        c.DeleteLayer("r");
        Make(c, "r", 70, 0);
        Make(c, "r.s", 2, 0);
        Check(Tx(c, "r.s") == 72, "recreate same ids after delete");
    }
    {   // ReleaseGl / Shutdown drop everything.
        Compositor c;
        c.Init(1920, 1080);
        Make(c, "r", 7, 0);
        Make(c, "r.s", 1, 0);
        Check(Tx(c, "r.s") == 8, "baseline");
        c.ReleaseGl();
        Check(c.Layers().empty(), "release clears layers");
        Layer probe;
        probe.id = "r.s";
        probe.x = 4;
        Check(c.EffectiveTransform(probe).tx == 4, "no ancestors after ReleaseGl");
        Make(c, "r", 1, 0);
        Make(c, "r.s", 2, 0);
        Check(Tx(c, "r.s") == 3, "rebuild after ReleaseGl");
        c.Shutdown();
        Check(c.Layers().empty(), "shutdown clears layers");
        Make(c, "u", 1, 0);
        Make(c, "u.v", 1, 0);
        Check(Tx(c, "u.v") == 2, "rebuild after Shutdown");
    }
    {   // GetLayerInfo / first-match semantics through the index.
        Compositor c;
        c.Init(1920, 1080);
        Make(c, "i", 3, 4);
        Check(c.GetLayerInfo("i").found && c.GetLayerInfo("i").left == 3 && !c.GetLayerInfo("j").found,
              "layer info through the index");
        c.DeleteLayer("i");
        Check(!c.GetLayerInfo("i").found, "layer info after delete");
        Make(c, "j", 5, 6);
        Check(c.GetLayerInfo("j").top == 6 && !c.GetLayerInfo("i").found, "layer info after recreate");
    }
}

static void OwnerInvalidation() {
    Compositor c;
    c.Init(1920, 1080);
    for (const char* id : {"30.1", "30.2", "40.1"}) Make(c, id, 0, 0);
    Check(c.SetLayerEmoteOrder("30.1", "30", 0) && c.SetLayerEmoteOrder("30.2", "30", 1),
          "register owner 30");
    Check(c.SetLayerEmoteOrder("40.1", "40", 0), "register owner 40");
    // Equal z: rank 1 is drawn above rank 0, so it is hit first.
    auto order = [&](const std::string& prefix) {
        std::vector<std::string> out;
        for (const auto& id : c.HitLayers(1, 1))
            if (id.compare(0, prefix.size(), prefix) == 0) out.push_back(id);
        return out;
    };
    const auto first = order("30.");
    Check(first.size() == 2 && first.front() == "30.2", "rank order inside owner 30");

    Make(c, "30.1.a", 0, 0);
    Make(c, "30.1.b", 0, 0);
    Check(!c.SetLayerEmoteOrder("30.1.a", "30.1", 0), "nested owner overlaps owner 30");

    // Owner migration: unregistering the members frees the namespace for a
    // nested owner immediately (the cached owner list must not be stale).
    Check(c.SetLayerEmoteOrder("30.1", "", 0) && c.SetLayerEmoteOrder("30.2", "", 0),
          "unregister owner 30 members");
    Check(c.SetLayerEmoteOrder("30.1.a", "30.1", 0), "nested owner allowed once 30 is gone");
    Check(!c.SetLayerEmoteOrder("30.2", "30", 0), "owner 30 now overlaps nested owner 30.1");
    // Deleting the registered member releases the owner again.
    c.DeleteLayer("30.1");
    Check(c.SetLayerEmoteOrder("30.2", "30", 0), "owner namespace released by delete");
    // Whole-owner rename moves the registration.
    Check(c.RenameLayer("30", "50"), "rename owner");
    Check(c.SetLayerEmoteOrder("50.2", "50", 0), "renamed owner is registered");
    Make(c, "50.9", 0, 0);
    Check(c.SetLayerEmoteOrder("50.9", "50", 3), "member joins renamed owner");
    Check(!c.SetLayerEmoteOrder("50.9", "50.9", 0), "overlap still rejected after rename");
    Check(!c.SetLayerEmoteOrder("30.2", "30", 0), "old owner id no longer resolves");
    // Moving part of an owner out clears its registration.
    Check(c.RenameLayer("50.9", "60"), "move member out of the owner");
    Check(c.SetLayerEmoteOrder("60", "60", 0), "moved member may form its own owner");
    c.ReleaseGl();
    Make(c, "50.2", 0, 0);
    Check(c.SetLayerEmoteOrder("50.2", "50", 0), "owner list rebuilt after ReleaseGl");
}

static void HitSetMatchesReference() {
    Compositor c;
    c.Init(1920, 1080);
    for (int i = 0; i < 6; ++i) {
        std::string id = std::to_string(10 + i);
        Make(c, id, float(i * 7), float(i * 3), true, float(i * 20), 100 + i * 5);
        for (int d = 1; d < 5; ++d) {
            id += ".1";
            Make(c, id, 2.f, 1.f, d % 2 == 0, float(d * 11), 100);
        }
    }
    for (const auto& pt : {std::pair<float, float>{1, 1}, {3, 4}, {10, 7}, {20, 13}, {40, 30}}) {
        std::vector<std::string> expected;
        for (const auto& l : c.Layers()) {
            const auto m = Reference(c, l);
            const float det = m.a * m.d - m.b * m.c;
            if (!m.visible || !l.texture || !std::isfinite(det) || std::abs(det) < 1e-8f) continue;
            const float x = pt.first - m.tx, y = pt.second - m.ty;
            const float lx = (m.d * x - m.c * y) / det, ly = (m.a * y - m.b * x) / det;
            if (lx >= 0 && ly >= 0 && lx < l.w && ly < l.h) expected.push_back(l.id);
        }
        const auto hits = c.HitLayers(pt.first, pt.second);
        Check(Sorted(hits) == Sorted(expected),
              "cached hit pass finds the same layers as the original walk");
        Check(c.HitLayer(pt.first, pt.second) == (hits.empty() ? std::string() : hits.front()),
              "HitLayer is the head of HitLayers");
    }
}

struct Scene {
    int chains, depth;
    size_t layers = 0;
};

static Scene Build(Compositor& c, int chains, int depth) {
    Scene s{chains, depth};
    for (int k = 0; k < chains; ++k) {
        std::string id = std::to_string(100 + k);
        for (int d = 1; d <= depth; ++d) {
            Make(c, id, 1, 1, d % 8 == 0, float(d), 100);
            id += ".1";
        }
    }
    for (int k = 0; k < 3; ++k) c.SetProps(std::to_string(900 + k), {});
    s.layers = c.Layers().size();
    return s;
}

// Layer at depth d composes d matrices (d-1 ancestors plus itself); the three
// extra roots compose one each.
static uint64_t DepthSum(const Scene& s) {
    return uint64_t(s.chains) * uint64_t(s.depth) * uint64_t(s.depth + 1) / 2 + 3;
}

static void Complexity(int chains) {
    Compositor c;
    c.Init(1920, 1080);
    const auto s = Build(c, chains, 20);
    const size_t n = s.layers;
    Check(n == size_t(chains) * 20 + 3, "scene size");
    c.EffectiveTransform(c.Layers().front());
    Check(c.Counters().index_rebuilds >= 1, "index built on first query");

    auto before = c.Counters();
    for (int i = 0; i < 5; ++i) Check(c.PendingTextMs(1000) == 0, "no pending text");
    for (int i = 0; i < 3; ++i) c.Update(1000 + i);
    auto after = c.Counters();
    Check(after.transform_applies == before.transform_applies &&
          after.index_lookups == before.index_lookups &&
          after.index_rebuilds == before.index_rebuilds,
          "PendingTextMs/Update do no per-layer ancestor work without text");

    before = c.Counters();
    for (const auto& l : c.Layers()) c.EffectiveTransform(l);
    after = c.Counters();
    Check(after.transform_applies - before.transform_applies == DepthSum(s),
          "uncached pass composes exactly the ancestor chain of every layer");
    Check(after.index_lookups == before.index_lookups && after.index_rebuilds == before.index_rebuilds,
          "in-table transforms need no id lookups or rebuilds");

    before = c.Counters();
    {
        const Compositor::TransformCache cache(c);
        for (int pass = 0; pass < 3; ++pass)
            for (const auto& l : c.Layers()) c.EffectiveTransform(l);
    }
    after = c.Counters();
    Check(after.transform_applies - before.transform_applies == n,
          "cached scope composes every layer exactly once");

    // Draw-order construction: no owners -> no id work at all.
    before = c.Counters();
    for (int i = 0; i < 3; ++i) c.HitLayers(5, 5);
    after = c.Counters();
    Check(after.index_lookups == before.index_lookups && after.index_rebuilds == before.index_rebuilds &&
          after.owner_rebuilds <= before.owner_rebuilds + 1 &&
          after.transform_applies - before.transform_applies <= 3 * n,
          "hit pass without owners: no lookups, one cached transform pass per call");

    // With one owner per chain the draw list costs O(depth) lookups per layer.
    for (int k = 0; k < chains; ++k) {
        std::string id = std::to_string(100 + k);
        const std::string owner = id;
        for (int d = 1; d <= 20; ++d) {
            Check(c.SetLayerEmoteOrder(id, owner, d), "register chain member");
            id += ".1";
        }
    }
    c.HitLayers(5, 5);
    before = c.Counters();
    c.HitLayers(5, 5);
    after = c.Counters();
    Check(after.owner_rebuilds == before.owner_rebuilds && after.index_rebuilds == before.index_rebuilds,
          "owner list and id index are reused across draw lists");
    Check(after.index_lookups - before.index_lookups <= uint64_t(n) * (20 + 1),
          "draw list lookups are O(layers*depth), not O(layers^2)");

    // Re-registering the same owner/order is a pure O(owners) no-op.
    before = c.Counters();
    for (int i = 0; i < 100; ++i) Check(c.SetLayerEmoteOrder("100.1.1", "100", 3), "idempotent registration");
    after = c.Counters();
    Check(after.owner_rebuilds == before.owner_rebuilds && after.index_rebuilds == before.index_rebuilds &&
          after.index_lookups - before.index_lookups == 100,
          "idempotent registration costs one id lookup");

    // One structural edit costs exactly one rebuild, shared by all later queries.
    before = c.Counters();
    c.DeleteLayer("900");
    for (int i = 0; i < 4; ++i) { c.HitLayers(5, 5); c.PendingTextMs(10); }
    for (const auto& l : c.Layers()) c.EffectiveTransform(l);
    after = c.Counters();
    Check(after.index_rebuilds - before.index_rebuilds == 1, "one rebuild per structural edit");
    Check(after.owner_rebuilds - before.owner_rebuilds <= 1, "one owner rebuild per structural edit");
}

int main() {
    Equivalence();
    Invalidation();
    OwnerInvalidation();
    HitSetMatchesReference();
    // 783 layers at depth 20 (the Mac save-load scene) and a quarter of it:
    // the per-layer counters must not depend on the number of layers.
    Complexity(39);
    Complexity(10);
    std::cout << "compositor index regressions passed\n";
    return 0;
}
