#pragma once
#include "render/emote_model.h"
#include "render/emote_mesh.h"
#include "render/emote_combinator.h"
#include <memory>

namespace artc {
// Engine safety limits, not PSB/SDK format limits. Document bytes, decoded
// images (128 MiB), parse scratch and compositor-owned copies are separate.
constexpr size_t kEmoteSceneCombinatorCacheBytes=64*1024*1024;
constexpr size_t kEmoteSceneOutputBytes=64*1024*1024;
class Compositor;
struct EmoteSceneLayer {
    std::string key,source,icon;
    // Node label (shape layers are addressed by it in hit tests) and the
    // hit-test geometry of `shape/...` / type==1 nodes.
    std::string label;
    // Content blend mode (`bm` low nibble, E-mote): empty = normal/alpha,
    // otherwise "add", "subtract", "multiply" or "screen".
    std::string blend;
    // Stencil composite mask layer labels active for this part (empty = no
    // mask). Render resolves them to the evaluator's mask source layers.
    std::vector<std::string> masks;
    // Shape-sync deformers inherited from ancestor meshTransform nodes
    // (nearest first). Render bakes them into an 8x8 warped mesh.
    struct Deformer {
        double m11=1,m12=0,m21=0,m22=1,tx=0,ty=0;  // domain -> part/node space
        std::vector<double> points;                // normalized control grid
        int side=0;
        double width=0,height=0;                   // extent size
        double origin_x=0,origin_y=0;              // extent origin
        double off_x=0,off_y=0;                    // content ox/oy offset
        // Extent source when the sync node draws a real icon instead of a
        // blank descriptor: resolved from the icon atlas at render time.
        std::string source,icon;
    };
    // Absolute model affine after resolving the current inheritance source;
    // Render keeps structure holders identity and installs the full matrix on
    // a .000000 content/geometry leaf (icon offset only for textured content).
    // Live external compositor ancestors remain outside this model affine.
    // Transparent sources are bypassed for descendants, not the node's art.
    double abs_m11=1,abs_m12=0,abs_m21=0,abs_m22=1,abs_tx=0,abs_ty=0;
    // Node linear state after inheritMask resolution (used when this node
    // enters a nested motion player).
    double st_rot=0,st_sx=1,st_sy=1,st_shx=0,st_shy=0;
    bool st_fx=false,st_fy=false;
    // Node opacity after inheritMask resolution (nested motion roots).
    double abs_opa=1;
    // Sampled coord[2] plus the inheritance source's Z (coordinate==0 only).
    // Independent of the 2D affine/linear inheritMask channels; nested motions
    // start from their attachment's effective Z. Atlas/node metadata is not an
    // animation channel and never overrides this value.
    double effective_z=0;
    std::vector<double> transform_order;
    std::vector<Deformer> deformers;
    enum class Shape { None, Rect, Circle, Point, Quad };
    Shape shape=Shape::None;
    // Per-frame color (0xRRGGBBAA, MODULATE2X space); 0 = not authored.
    uint32_t color=0;
    double x=0,y=0,angle=0,scale_x=1,scale_y=1,opacity=1,origin_x=0,origin_y=0;
    bool visible=true;
    // Per-icon Bezier mesh warp: interleaved (warped_x, warped_y, u, v) in
    // normalized icon space (triangles, 4 floats per vertex). Empty = affine.
    std::vector<float> mesh;
};
// Frame evaluator/renderer for plain image/layout/child-motion nodes plus
// per-icon Bezier mesh warp (DXT5/BC7/RGBA8 atlases). Stencil/mask composites,
// physics and non-per-icon mesh propagation are still rejected before changing
// the displayed scene; this does not advertise complete SDK support.
class EmoteScene {
public:
    // The model/document must remain immutable for this scene's lifetime,
    // including through other non-const aliases: do not call model.Load again.
    // Use a new model and scene.Load for transactional replacement instead.
    // Finite ordinary-2D meshCombine support requires equal complete domains
    // and same-motion structural ancestry. Combinators OWN frame mesh data,
    // including when all-neutral revokes their effective grid; bp never wins.
    // Nonempty cc, low sync channels (&1/&2/&4), shape-inheritance barriers met
    // by the scan, and unknown/different/dynamic domains are NOT applied and do
    // not fail Load: the affected meshCombine node simply does not combine (it
    // keeps its own patch and the ancestors' deformers, as with meshCombine 0),
    // low sync channels have no effect and cc meshes stay affine. Each is
    // reported once per Load through UnsupportedFeatures() and the log.
    bool Load(std::shared_ptr<const EmoteModel> model,std::string& error);
    // Tag -> count of distinct document nodes (meshSync.lowChannels, mesh.cc,
    // meshCombine.fallback[.lowSync|cc|barrier|domain|dynamic|runtime]).
    EmoteUnsupportedFeatures UnsupportedFeatures() const;
    // Shared-atlas textures fully decoded by the last successful Load (icons of
    // one atlas share a single decode; per-icon textures are not counted).
    size_t LoadAtlasDecodes() const {return atlas_decodes_;}
    // mesh_side selects the output subdivision of warped mesh patches
    // (setMeshDivisionRatio; kEmoteMeshSide is the full-quality default).
    bool Evaluate(double frame,const std::map<std::string,double>& variables,
                  std::vector<EmoteSceneLayer>& output,std::string& error,
                  int mesh_side=kEmoteMeshSide) const;
    // grayscale (0..1) is applied to the textured part layers when > 0; the
    // E-mote player's SetGrayscale drives it. multiply is a straight RGB
    // factor for 0x00RRGGBB (0xFFFFFF = neutral); the player converts the
    // native MODULATE2X tint before calling. mesh_division scales the mesh
    // subdivision (1.0 = full, lower = coarser; the scripts' performance knob).
    bool Render(Compositor& compositor,const std::string& id,double frame,
                const std::map<std::string,double>& variables,std::string& error,
                double grayscale=0,uint32_t multiply=0xFFFFFF,double mesh_division=1.0);
    // Geometric point test over explicit shapes and textured parts installed
    // under id; an empty label matches any node. Stage-pixel coordinates are
    // clipped to the stage target; the compositor supplies live geometry and
    // effective visibility/alpha/transforms. Includes triangle/quad boundaries,
    // but does not sample texture alpha, masks or shader/effect coverage.
    bool HitTest(Compositor& compositor,const std::string& id,const std::string& label,
                 double x,double y) const;
    // Delete every layer this scene installed under id (the bare container id
    // itself is the caller's — the player removes it via its own RemoveLayers).
    void Remove(Compositor& compositor,const std::string& id);
private:
    std::shared_ptr<const EmoteModel> model_;
    // True document node identity, NOT labels or attachment instance keys.
    // Written only by Load; Evaluate calls the typed combinator API only.
    std::map<const PsbValue*,EmoteMeshCombinator> combinators_;
    // Nodes whose meshCombine Load could not prove (they do not combine), the
    // Load-time feature counts, and nodes whose combine a later Evaluate had to
    // abandon (bounded by node count; deduplicated, never grows per frame).
    std::set<const PsbValue*> combine_disabled_;
    EmoteUnsupportedFeatures features_;
    mutable std::set<const PsbValue*> runtime_combine_fallbacks_;
    std::map<std::pair<std::string,std::string>,EmoteImage> images_;
    size_t atlas_decodes_=0;
    std::map<std::string,std::pair<std::string,std::string>> installed_;
    std::set<std::string> installed_layers_;
    // metadata.attrcomp removals, keyed "chara\nmotion\nlayer"; nodes matching
    // one are skipped by both validation and evaluation.
    std::set<std::string> removed_;
    struct InstalledHit {
        std::string id,label;
        EmoteSceneLayer::Shape shape=EmoteSceneLayer::Shape::None;
    };
    std::string installed_hit_owner_;
    std::vector<InstalledHit> installed_hits_;
    static std::string RemovalKey(const std::string& chara,const std::string& motion,
                                  const std::string& layer) {
        return chara+'\n'+motion+'\n'+layer;
    }
};
}
