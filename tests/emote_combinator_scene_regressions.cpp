#include "emote_scene_fixture.h"
#include "render/emote_scene.h"
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>

using namespace artc;
using namespace emote_fixture;
namespace {
void Check(bool value,const char* what) {if(!value)throw std::runtime_error(what);}
void Near(double a,double b,const char* what) {Check(std::abs(a-b)<1e-5,what);}
PsbValue& Motion(PsbDocument& d,const char* name="idle") {
    return d.root.object["object"].object["actor"].object["motion"].object[name];
}
EmoteCombinatorGrid Grid(float dx=0,float dy=0,bool delta=false) {
    EmoteCombinatorGrid g;
    for(int y=0;y<4;++y)for(int x=0;x<4;++x) {
        g[(y*4+x)*2]=(delta?0:float(x)/3)+dx;
        g[(y*4+x)*2+1]=(delta?0:float(y)/3)+dy;
    }
    return g;
}
PsbValue Resource(PsbDocument& d,const std::vector<EmoteCombinatorGrid>& grids,bool extra=false) {
    PsbValue ref;ref.type=PsbValue::Resource;ref.extra=extra;
    auto& bank=extra?d.extra_resources:d.resources;ref.resource=bank.size();
    const size_t begin=d.bytes.size();
    for(const auto& g:grids)for(float value:g) {
        uint32_t bits;std::memcpy(&bits,&value,4);
        for(int b=0;b<4;++b)d.bytes.push_back(uint8_t(bits>>(8*b)));
    }
    bank.push_back({begin,d.bytes.size()-begin});return ref;
}
PsbValue Entry(PsbDocument& d,const char* key,const std::vector<EmoteCombinatorGrid>& grids,bool extra=false) {
    return O({{"meshType",N(1)},{"neutralIndex",N(-1)},
        {"variable",O({{"key",S(key)},{"meshCount",N(double(grids.size()))},
                       {"rangeBegin",N(0)},{"rangeEnd",N(1)}})},
        {"rawMeshList",Resource(d,grids,extra)}});
}
PsbValue Combinator(PsbDocument& d,float shift=0,bool extra=false) {
    return O({{"combinatorList",A({Entry(d,"a",{Grid(shift),Grid(shift+0.25f)},extra),
        Entry(d,"b",{Grid(0,0,true),Grid(0,0.125f,true)},!extra)})}});
}
PsbValue Content(const char* src="src/images/body") {
    return O({{"src",S(src)},{"mesh",O({{"bp",PsbValue{}},{"cc",A({})}})}});
}
PsbValue Icon(PsbDocument& d,const char* label="same",float shift=0,bool split=false) {
    auto c=Content(split?"images":"src/images/body");if(split)c.object["icon"]=S("body");
    auto n=Node(0,label,A({Key(0,2,c)}));
    n.object["meshCombinator"]=Combinator(d,shift);return n;
}
PsbDocument Document() {
    auto d=Scene();auto& m=Motion(d);m.object["lastTime"]=N(100);
    m.object["loopTime"]=N(-1);m.object["layer"]=A({});return d;
}
std::shared_ptr<EmoteModel> Model(PsbDocument d) {
    auto m=std::make_shared<EmoteModel>();std::string error;
    Check(m->Load(std::move(d),error),error.c_str());return m;
}
std::vector<EmoteSceneLayer> Evaluate(EmoteScene& s,const std::map<std::string,double>& vars={},int side=8) {
    std::vector<EmoteSceneLayer> out;std::string error;
    Check(s.Evaluate(0,vars,out,error,side),error.c_str());return out;
}
bool Loads(EmoteScene& s,PsbDocument d,std::string& e) {return s.Load(Model(std::move(d)),e);}
// Unproven/unsupported inputs DEGRADE: Load succeeds, the feature tag is
// recorded, and the affected channel is not applied (never applied wrongly).
void ExpectDegraded(EmoteScene& s,PsbDocument d,const char* tag,const char* what) {
    std::string e;Check(Loads(s,std::move(d),e),(std::string(what)+": Load must succeed: "+e).c_str());
    Check(s.UnsupportedFeatures().Has(tag),(std::string(what)+": missing feature tag "+tag).c_str());
}
void OwnAndReload() {
    auto d=Document();Motion(d).object["layer"]=A({Icon(d)});
    EmoteScene s;std::string e;Check(s.Load(Model(d),e),e.c_str());
    const auto a=Evaluate(s);Check(a.size()==1 && !a[0].mesh.empty(),"own no-bp combinator mesh");
    Near(a[0].mesh[0],0,"absolute no extra identity");
    const auto b=Evaluate(s,{{"a",1},{"b",1}});
    Near(b[0].mesh[0],0.25,"absolute sample");Near(b[0].mesh[1],0.125,"delta sample");
    for(size_t i=0;i<a[0].mesh.size();i+=4) {
        Near(b[0].mesh[i+2],a[0].mesh[i+2],"unchanged u");
        Near(b[0].mesh[i+3],a[0].mesh[i+3],"unchanged v");
    }
    Check(Evaluate(s)[0].mesh==a[0].mesh,"A-B-A no history drift");
    auto bad=d;auto& n=Motion(bad).object["layer"].array[0];
    n.object["meshCombine"]=N(2);Check(!s.Load(Model(bad),e),"reject unknown meshCombine flag");
    Check(e.find("meshCombine")!=std::string::npos,"combine explicit error");
    Check(Evaluate(s)[0].mesh==a[0].mesh,"failed reload keeps old cache");
    auto changed=Document();Motion(changed).object["layer"]=A({Icon(changed,"same",0.5f)});
    Check(s.Load(Model(changed),e),e.c_str());Near(Evaluate(s)[0].mesh[0],0.5,"successful reload changes cache");
    std::vector<EmoteSceneLayer> old(1);old[0].label="sentinel";old[0].mesh={19};
    Check(!s.Evaluate(0,{{"a",std::numeric_limits<double>::quiet_NaN()}},old,e),"nonfinite variable fails");
    Check(old.size()==1 && old[0].label=="sentinel" && old[0].mesh==std::vector<float>{19},"failed eval preserves output");
    // Empty-array and completely absent mesh placeholders take exactly the same
    // combinator path as explicit null bp. Neither means "disable controller".
    for(bool absent:{false,true}) {
        auto placeholders=d;auto& c=Motion(placeholders).object["layer"].array[0]
            .object["frameList"].array[0].object["content"];
        if(absent)c.object.erase("mesh");else c.object["mesh"].object["bp"]=A({});
        Check(s.Load(Model(placeholders),e),e.c_str());
        Check(Evaluate(s)[0].mesh==a[0].mesh,"empty/null mesh does not suppress combinator");
    }
}
void ScopeAndAttachments() {
    for(bool blank:{false,true})for(bool split:{false,true}) {
        auto d=Document();auto root=Icon(d,"scope",0,split);
        if(blank)root.object["frameList"]=A({Key(0,2,O({{"src",S("blank:20:30:2:3")},
            {"ox",N(4)},{"oy",N(5)}}))});
        root.object["meshTransform"]=N(1);root.object["meshSyncChildMask"]=N(8);
        auto leaf=Node(0,"leaf",A({Key(0,2,Content())}));
        auto suspended=Node(2,"suspend",A({Key(0,2,O({{"src",S("layout")}}))}),A({leaf}));
        suspended.object["inheritMask"]=N(0x1fc);
        root.object["children"]=A({leaf,suspended,leaf});
        Motion(d).object["layer"]=A({root,leaf});
        EmoteScene s;std::string e;Check(s.Load(Model(d),e),e.c_str());
        const auto out=Evaluate(s,{{"a",1}});
        Check(out.size()==6,"scope output count");
        Check(out[0].deformers.empty(),"own patch not self-deformed");
        Check(out[1].deformers.size()==1 && out[1].deformers[0].side==4,"scope without bp");
        Near(out[1].deformers[0].points[0],0.25,"scope consumes evaluated grid");
        if(blank) {
            const auto& scope=out[1].deformers[0];
            Near(scope.width,20,"blank width");Near(scope.origin_x,2,"blank origin");
            Near(scope.off_x,4,"content origin separate");
        } else Check(out[1].deformers[0].source=="images","icon scope extent");
        Check(out[3].deformers.empty(),"shape inheritance suspended");
        Check(out[4].deformers.size()==1,"sibling restores scope");
        Check(out[5].deformers.empty(),"outer sibling no scope leak");
    }
    // True source-node identity, not duplicate labels, and one data node mounted
    // at two independent attachment affines. normal+extra resources coexist.
    auto d=Document();auto a=Icon(d,"duplicate",0),b=Icon(d,"duplicate",0.75f,true);
    auto child=O({{"layer",A({a,b})},{"lastTime",N(100)},{"loopTime",N(-1)}});
    d.root.object["object"].object["actor"].object["motion"].object["child"]=child;
    auto mount=Node(3,"mount",A({Key(0,2,O({{"src",S("motion/actor/child")},{"coord",A({N(10),N(0)})}}))}));
    auto mount2=mount;auto& split_mount=mount2.object["frameList"].array[0].object["content"];
    split_mount.object["src"]=S("actor");split_mount.object["icon"]=S("child");
    split_mount.object["coord"]=A({N(50),N(0)});
    Motion(d).object["layer"]=A({mount,mount2});
    EmoteScene s;std::string e;Check(s.Load(Model(d),e),e.c_str());
    auto out=Evaluate(s);Check(out.size()==6,"two attachments output");
    Near(out[1].mesh[0],0,"cache identity first");Near(out[2].mesh[0],0.75,"same-label independent cache");
    Near(out[1].abs_tx,10,"first attachment affine");Near(out[4].abs_tx,50,"second attachment affine");
    Check(out[1].mesh==out[4].mesh,"shared data stable in both instances");

    auto nested=Document();auto outer=Icon(nested,"outer",0.1f),inner=Icon(nested,"inner",0.2f);
    auto leaf=Node(0,"nested leaf",A({Key(0,2,Content())}));
    for(auto* n:{&outer,&inner}) {
        n->object["meshTransform"]=N(1);n->object["meshSyncChildMask"]=N(8);
    }
    outer.object["frameList"].array[0].object["content"].object["coord"]=A({N(10),N(0),N(2)});
    inner.object["frameList"].array[0].object["content"].object["coord"]=A({N(5),N(0),N(3)});
    inner.object["children"]=A({leaf});outer.object["children"]=A({inner});
    Motion(nested).object["layer"]=A({outer});
    Check(s.Load(Model(nested),e),e.c_str());out=Evaluate(s);
    Check(out[1].deformers.size()==1 && out[2].deformers.size()==2,"own then ancestors exactly once");
    Near(out[2].deformers[0].points[0],0.2,"nearest ancestor first");
    Near(out[2].deformers[1].points[0],0.1,"outer ancestor second");
    Near(out[2].deformers[1].tx,-5,"domain to part affine direction");
    Near(out[2].effective_z,5,"effective Z survives patch path");
}
void Rejections() {
    for(int mode=1;mode<12;++mode) {
        auto d=Document();auto n=Icon(d);
        auto& entry=n.object["meshCombinator"].object["combinatorList"].array[0];
        if(mode==0 || mode==1) {
            auto c=Content();auto& mesh=c.object["mesh"];
            mesh.object[mode==0?"bp":"cc"]=A({N(0)});
            n.object["frameList"].array.push_back(Key(50,2,c));
        } else if(mode==2)entry.object["meshType"]=N(2);
        else if(mode==3)entry.object["meshType"]=S("1");
        else if(mode==4)entry.object["entryType"]=N(1);
        else if(mode==5)n.object["meshCombinator"]=O({});
        else if(mode==6)n.object["frameList"]=A({});
        else if(mode==7)n.object["frameList"].array[0].object["time"]=N(1);
        else if(mode==8) {
            n.object["meshTransform"]=N(1);n.object["meshSyncChildMask"]=N(1);
        }
        else if(mode==9) {
            n.object["meshTransform"]=N(1);n.object["meshSyncChildMask"]=N(8);
            n.object["frameList"].array.push_back(Key(40,2,O({{"src",S("layout")}})));
        } else if(mode==10) {
            n.object["type"]=N(12);entry.object["meshType"]=N(2);
        } else if(mode==11)n.object["entryType"]=N(1);
        else n.object["frameList"].array[0].object["content"].object["mesh"].object["bp"]=S("bad");
        Motion(d).object["layer"]=A({n});EmoteScene s;std::string e;
        if(mode==1 || mode==8) {
            // Nonempty cc / low sync channels beside a combinator are not applied
            // (no combine, affine cc) and recorded; they no longer fail Load.
            ExpectDegraded(s,d,mode==1?"mesh.cc":"meshSync.lowChannels",mode==1?"combinator + cc":"combinator + low sync");
            const auto out=Evaluate(s);Check(out.size()==1 && !out[0].mesh.empty(),"degraded node keeps its own combinator mesh");
            Near(out[0].mesh[0],0,"degraded combinator mesh unchanged");
            Check(!s.UnsupportedFeatures().Has("meshCombine.fallback"),"no combine requested: no combine fallback");
            continue;
        }
        Check(!s.Load(Model(d),e),"unsupported future/metadata must fail Load");Check(!e.empty(),"clear rejection error");
    }
    // Do not broaden the limited combine rejection to established bp-only nodes.
    auto d=Document();auto n=Node(0,"bp only",A({Key(0,2,Content())}));
    auto bp=A({});for(float value:Grid())bp.array.push_back(N(value));
    n.object["frameList"].array[0].object["content"].object["mesh"].object["bp"]=bp;
    n.object["meshCombine"]=N(1);Motion(d).object["layer"]=A({n});
    EmoteScene s;std::string e;Check(s.Load(Model(d),e),e.c_str());
}

PsbValue ShapeNode(PsbDocument& d,const char* label,float shift,bool combine=false) {
    auto n=Icon(d,label,shift);n.object["meshTransform"]=N(1);
    n.object["meshSyncChildMask"]=N(8);n.object["meshCombine"]=N(combine?1:0);
    n.object["frameList"].array[0].object["content"].object["src"]=S("blank:20:30:2:3");
    return n;
}
PsbValue Leaf() {return Node(0,"leaf",A({Key(0,2,Content())}));}
void OwnershipAndNeutral() {
    auto d=Document();auto n=Icon(d);
    auto& list=n.object["meshCombinator"].object["combinatorList"].array;
    for(auto& entry:list)entry.object["neutralIndex"]=N(0);
    auto& frames=n.object["frameList"].array;
    auto bp=A({});for(float p:Grid(0.8f))bp.array.push_back(N(p));
    frames[0].object["type"]=N(3);
    frames[0].object["content"].object["mesh"].object["bp"]=bp;
    auto future=Content();future.object["mesh"].object["bp"]=A({N(999)}); // ignored even malformed
    frames.push_back(Key(50,2,future));
    Motion(d).object["layer"]=A({n});
    EmoteScene s;std::string e;Check(s.Load(Model(d),e),e.c_str());
    Check(Evaluate(s)[0].mesh.empty(),"neutral ownership blocks frame bp");
    auto active=Evaluate(s,{{"a",1}});Near(active[0].mesh[0],0.25,"nonneutral combinator wins bp");
    std::vector<EmoteSceneLayer> out;
    Check(s.Evaluate(25,{{"a",0.5}},out,e),e.c_str());
    Near(out[0].mesh[0],0.125,"interpolation does not copy/interpolate bp over controller");
    Check(s.Evaluate(60,{},out,e),e.c_str());Check(out[0].mesh.empty(),"future neutral never falls back bp");

    auto typed=EmoteMeshCombinator{};
    Check(ParseEmoteCombinator(d,n.At("meshCombinator"),typed,e),e.c_str());
    EmoteCombinatorGrid grid;bool neutral=false;
    Check(EvaluateEmoteCombinator(typed,{},grid,e,neutral)&&neutral,"controller-state neutral output");
    Check(EvaluateEmoteCombinator(typed,{{"a",0x1p-24}},grid,e,neutral)&&neutral,"neutral fraction threshold below");
    Check(EvaluateEmoteCombinator(typed,{{"a",0x1p-22}},grid,e,neutral)&&!neutral,"neutral fraction threshold above");
    Check(EvaluateEmoteCombinator(typed,{{"a",0x1p-23}},grid,e,neutral)&&!neutral,"neutral fraction is strictly below 2^-23");
    {
        // Every entry must be neutral; the second (delta) controller alone leaving
        // neutral makes the whole combinator effective again.
        auto two=typed;two.entries[1].neutral_index=0;
        Check(EvaluateEmoteCombinator(two,{},grid,e,neutral)&&neutral,"all entries neutral");
        Check(EvaluateEmoteCombinator(two,{{"b",1}},grid,e,neutral)&&!neutral,"one entry off neutral is effective");
    }
    typed.entries[0].neutral_index=-1;
    Check(EvaluateEmoteCombinator(typed,{},grid,e,neutral)&&!neutral,"identity grid with unknown neutral is effective");
    typed.entries[0].neutral_index=1;
    Check(EvaluateEmoteCombinator(typed,{{"a",1}},grid,e,neutral)&&neutral,"neutral integer last endpoint");
    neutral=true;const auto saved=grid;
    Check(!EvaluateEmoteCombinator(typed,{{"a",std::numeric_limits<double>::quiet_NaN()}},grid,e,neutral),"neutral error");
    Check(neutral&&grid==saved,"neutral and grid outputs transactional");

    auto revoked=Document();auto root=ShapeNode(revoked,"neutral own",0.6f,true);
    for(auto& entry:root.object["meshCombinator"].object["combinatorList"].array)entry.object["neutralIndex"]=N(0);
    root.object["children"]=A({Leaf()});Motion(revoked).object["layer"]=A({root});
    Check(s.Load(Model(revoked),e),e.c_str());out=Evaluate(s);
    Check(out[0].mesh.empty()&&out[1].deformers.empty(),"all neutral revokes effective own shape despite nonidentity grid");
    out=Evaluate(s,{{"a",1}});Check(out[1].deformers.size()==1,"effective grid restores own shape scope");
}
void SameDomainCombine() {
    EmoteScene s;std::string e;
    // Every structural ancestor contributes OWN-I; terminal ancestor contributes
    // before stop. Intermediate non-shape nodes control scanning, not grid sum.
    for(bool middle:{false,true})for(bool keep_scanning:{false,true}) {
        auto d=Document();auto outer=ShapeNode(d,"outer",0.1f,false);
        auto inner=ShapeNode(d,"inner",0.2f,true);inner.object["children"]=A({Leaf()});
        if(middle) {
            auto holder=Node(2,"ordinary middle",A({Key(0,2,O({{"src",S("layout")}}))}),A({inner}));
            holder.object["meshCombine"]=N(keep_scanning?1:0);outer.object["children"]=A({holder});
        } else outer.object["children"]=A({inner});
        Motion(d).object["layer"]=A({outer});Check(s.Load(Model(d),e),e.c_str());
        auto out=Evaluate(s);const auto& leaf=out.back();
        const double expected=middle&&!keep_scanning?0.2:0.3;
        const size_t child=middle?2:1;Near(out[child].mesh[0],expected,"terminal OWN contribution / ordinary stop");
        Check(leaf.deformers.size()==size_t(middle&&!keep_scanning?2:1),"only incorporated shape head removed");
        Near(leaf.deformers[0].points[0],expected,"scope uses combined effective grid");
    }
    auto d=Document();auto a=ShapeNode(d,"A",0.1f,false),b=ShapeNode(d,"B",0.2f,true),c=ShapeNode(d,"C",0.3f,true);
    c.object["children"]=A({Leaf()});b.object["children"]=A({c,Leaf()});a.object["children"]=A({b,Leaf()});
    Motion(d).object["layer"]=A({a});Check(s.Load(Model(d),e),e.c_str());
    auto out=Evaluate(s);
    Near(out[1].mesh[0],0.3,"B combines A once");Near(out[2].mesh[0],0.6,"C sums B OWN not B combined");
    Check(out[3].deformers.size()==1,"combined ancestors not double-applied");
    Near(out[3].deformers[0].points[0],0.6,"combined child scope");
    Near(out[4].deformers[0].points[0],0.3,"inner sibling restores B scope");
    Near(out[5].deformers[0].points[0],0.1,"outer sibling restores A scope");
    Check(out[5].deformers.size()==1,"siblings no consumed scope leak");

    auto identity=Document();a=ShapeNode(identity,"identity parent",0,false);b=ShapeNode(identity,"child",0.25f,true);
    b.object["children"]=A({Leaf()});a.object["children"]=A({b});Motion(identity).object["layer"]=A({a});
    Check(s.Load(Model(identity),e),e.c_str());out=Evaluate(s);
    Near(out[1].mesh[0],0.25,"identity parent no extra identity");
    Check(out[2].deformers.size()==1,"identity parent consumed by identity not grid equality");

    // A metadata-neutral ancestor has no effective shape grid to contribute,
    // regardless of its stored numeric grid. Its combine flag still controls
    // scanning to the next structural ancestor.
    auto neutral_parent=Document();a=ShapeNode(neutral_parent,"A",0.1f,false);
    b=ShapeNode(neutral_parent,"revoked B",0.8f,true);c=ShapeNode(neutral_parent,"C",0.3f,true);
    for(auto& entry:b.object["meshCombinator"].object["combinatorList"].array)entry.object["neutralIndex"]=N(0);
    c.object["children"]=A({Leaf()});b.object["children"]=A({c});a.object["children"]=A({b});
    Motion(neutral_parent).object["layer"]=A({a});Check(s.Load(Model(neutral_parent),e),e.c_str());out=Evaluate(s);
    Check(out[1].mesh.empty(),"neutral parent revoked");
    Near(out[2].mesh[0],0.4,"neutral nonshape ancestor still permits scan");
    Check(out[3].deformers.size()==1,"revoked parent not reintroduced in scopes");

    // Complete D equality, not identical descriptor, is sufficient: child pos
    // and source/content origin compensate exactly. No cross-domain conversion.
    auto compensated=identity;auto& comp=Motion(compensated).object["layer"].array[0].object["children"].array[0]
        .object["frameList"].array[0].object["content"];
    comp.object["coord"]=A({N(5),N(0)});comp.object["ox"]=N(5);
    Check(s.Load(Model(compensated),e),e.c_str());

    // New motion keeps outer deformation but starts an empty combine scan.
    auto nested=Document();a=ShapeNode(nested,"outer",0.1f,true);b=ShapeNode(nested,"local root",0.2f,true);
    b.object["children"]=A({Leaf()});
    nested.root.object["object"].object["actor"].object["motion"].object["child"]=O({{"layer",A({b})},{"lastTime",N(100)}});
    auto mount=Node(3,"mount",A({Key(0,2,O({{"src",S("motion/actor/child")}}))}));mount.object["meshCombine"]=N(1);
    a.object["children"]=A({mount});Motion(nested).object["layer"]=A({a});
    Check(s.Load(Model(nested),e),e.c_str());out=Evaluate(s);
    Near(out[2].mesh[0],0.2,"nested combine never crosses motion boundary");
    Check(out[3].deformers.size()==2,"nested keeps outer shape chain");
    Near(out[3].deformers[0].points[0],0.2,"nested local shape first");
    Near(out[3].deformers[1].points[0],0.1,"nested outer shape retained");
}
PsbValue Bp(float shift) {
    auto bp=A({});for(float v:Grid(shift))bp.array.push_back(N(v));return bp;
}
// Frame-bp (no combinator) shape node over a blank extent.
PsbValue BpShape(const char* label,float shift,int mask,bool combine,bool cc=false) {
    auto c=O({{"src",S("blank:20:30:2:3")},{"mesh",O({{"bp",Bp(shift)},{"cc",cc?A({N(0)}):A({})}})}});
    auto n=Node(0,label,A({Key(0,2,c)}));
    n.object["meshTransform"]=N(1);n.object["meshSyncChildMask"]=N(mask);
    n.object["meshCombine"]=N(combine?1:0);return n;
}
int g_failures=0;
void Section(const char* name,const std::function<void()>& body) {
    try {body();}
    catch(const std::exception& e) {++g_failures;std::fprintf(stderr,"FAIL [%s]: %s\n",name,e.what());}
}
void LowSyncChannelsAndProofs() {
    EmoteScene s;std::string e;
    // sync=8 is a shape scope; sync=1 is a different (unproven) channel. A scan
    // that meets the latter must refuse, never silently skip it.
    Section("sync=8 ancestor contributes",[&] {
        auto d=Document();auto p=BpShape("p",0.1f,8,false),c=BpShape("c",0.2f,8,true);
        c.object["children"]=A({Leaf()});p.object["children"]=A({c});Motion(d).object["layer"]=A({p});
        Check(Loads(s,d,e),e.c_str());const auto out=Evaluate(s);
        Near(out[1].mesh[0],0.3,"sync=8 ancestor contributes");
        Check(out[2].deformers.size()==1,"sync=8 ancestor consumed");
    });
    Section("sync=1 ancestor degrades",[&] {
        auto d=Document();auto p=BpShape("p",0.1f,1,false),c=BpShape("c",0.2f,8,true);
        c.object["children"]=A({Leaf()});p.object["children"]=A({c});Motion(d).object["layer"]=A({p});
        ExpectDegraded(s,d,"meshCombine.fallback.lowSync","scan meeting sync=1 ancestor degrades");
        Check(s.UnsupportedFeatures().Has("meshSync.lowChannels") && s.UnsupportedFeatures().Has("meshCombine.fallback"),
              "low channel and fallback both recorded");
        Check(s.UnsupportedFeatures().Count("meshCombine.fallback")==1,"exactly the combining node falls back");
        const auto out=Evaluate(s);
        Near(out[1].mesh[0],0.2,"fallback node keeps its OWN grid, no 0.3 combine");
        Check(out[2].deformers.size()==1,"fallback node's own scope is still published");
    });
    Section("sync=1 self",[&] {   // sync=1 without 8 is not a shape: no scope; combine on it falls back.
        auto d=Document();auto only=BpShape("only",0.2f,1,false);only.object["children"]=A({Leaf()});
        Motion(d).object["layer"]=A({only});Check(Loads(s,d,e),e.c_str());
        Check(Evaluate(s)[1].deformers.empty(),"sync=1 publishes no shape scope");
        Check(s.UnsupportedFeatures().Count("meshSync.lowChannels")==1 && !s.UnsupportedFeatures().Has("meshCombine.fallback"),
              "low sync recorded even when nothing combines");
        only.object["meshCombine"]=N(1);Motion(d).object["layer"]=A({only});
        ExpectDegraded(s,d,"meshCombine.fallback.lowSync","combine on a sync=1 node degrades");
        Check(Evaluate(s)[1].deformers.empty() && std::abs(Evaluate(s)[0].mesh[0]-0.2)<1e-5,"nothing combined or published");
    });
    Section("sync=1 beyond terminal ancestor",[&] {   // the scan stops before it reaches sync=1.
        auto d=Document();auto g=BpShape("g",0.05f,1,false),p=BpShape("p",0.1f,8,false),c=BpShape("c",0.2f,8,true);
        p.object["children"]=A({c});g.object["children"]=A({p});Motion(d).object["layer"]=A({g});
        Check(Loads(s,d,e),e.c_str());Near(Evaluate(s)[2].mesh[0],0.3,"scan terminated before sync=1 ancestor");
        Check(!s.UnsupportedFeatures().Has("meshCombine.fallback"),"proven same-domain combine unchanged");
        auto reach=d;auto& gp=Motion(reach).object["layer"].array[0].object["children"].array[0];
        gp.object["meshCombine"]=N(1);
        ExpectDegraded(s,reach,"meshCombine.fallback.lowSync","scan continuing into sync=1 ancestor degrades");
        Near(Evaluate(s)[2].mesh[0],0.2,"unproven scan does not combine");
    });
    for(int mode=0;mode<3;++mode)   // cc is unresolved for every participant
        Section(mode==0?"cc absent":mode==1?"cc ancestor":"cc self",[&] {
        auto d=Document();auto p=BpShape("p",0.1f,8,false,mode==1),c=BpShape("c",0.2f,8,true,mode==2);
        c.object["children"]=A({Leaf()});p.object["children"]=A({c});Motion(d).object["layer"]=A({p});
        if(mode==0) {
            Check(Loads(s,d,e),e.c_str());
            Check(s.UnsupportedFeatures().Empty(),"no cc, no unsupported features");
        } else {
            ExpectDegraded(s,d,"meshCombine.fallback.cc","nonempty cc on a participating shape degrades");
            Check(s.UnsupportedFeatures().Has("mesh.cc"),"cc recorded");
            Near(Evaluate(s)[1].mesh[0],0.2,"cc participant does not combine");
        }
    });
    // A shape-inheritance barrier met by the scan degrades at Load, even
    // when the combining shape only appears on a later frame.
    Section("barrier at Load",[&] {
        auto d=Document();auto root=Node(2,"barrier root",A({Key(0,2,O({{"src",S("layout")}}))}));
        root.object["inheritMask"]=N(0x1fc);root.object["meshCombine"]=N(1);
        auto c=BpShape("late",0.2f,8,true);
        auto late=c.object["frameList"].array[0].object["content"];
        c.object["frameList"].array[0].object["content"].object["mesh"].object["bp"]=A({});
        c.object["frameList"].array.push_back(Key(50,2,late));
        root.object["children"]=A({c});Motion(d).object["layer"]=A({root});
        ExpectDegraded(s,d,"meshCombine.fallback.barrier","barrier proven at Load across all frames");
        std::vector<EmoteSceneLayer> out;Check(s.Evaluate(60,{},out,e),e.c_str());
        Near(out[1].mesh[0],0.2,"late frame keeps its own mesh behind a barrier");
    });
    // Load failure keeps the previous scene's features.
    Section("failed Load keeps features",[&] {
        auto d=Document();auto only=BpShape("only",0.2f,1,true);Motion(d).object["layer"]=A({only});
        ExpectDegraded(s,d,"meshSync.lowChannels","degraded baseline");
        const auto before=s.UnsupportedFeatures().Counts();
        auto bad=Document();auto n=Icon(bad);n.object["meshCombine"]=N(2);Motion(bad).object["layer"]=A({n});
        Check(!Loads(s,bad,e),"genuinely malformed model still fails");
        Check(s.UnsupportedFeatures().Counts()==before,"failed Load keeps previous features");
    });
    // With no ancestor to combine, a degenerate later self domain is not a
    // playback-time failure.
    Section("degenerate self without ancestors",[&] {
        auto d=Document();auto c=BpShape("late",0.2f,8,true);
        auto later=c.object["frameList"].array[0].object["content"];later.object["zx"]=N(0);
        c.object["frameList"].array.push_back(Key(50,2,later));Motion(d).object["layer"]=A({c});
        Check(Loads(s,d,e),e.c_str());std::vector<EmoteSceneLayer> out;
        Check(s.Evaluate(60,{},out,e),e.c_str());Near(out[0].mesh[0],0.2,"no-op combine at degenerate frame");
    });
}
void OriginsAndMultiLevel() {
    EmoteScene s;std::string e;
    // Equal complete D with nonzero origin, content offset and a moved parent.
    auto build=[](const char* child_src,double cx,double cy,double cox,double coy) {
        auto d=Document();auto p=ShapeNode(d,"P",0.1f),c=ShapeNode(d,"C",0.2f,true);
        auto& pc=p.object["frameList"].array[0].object["content"];
        pc.object["coord"]=A({N(7),N(3)});pc.object["ox"]=N(4);pc.object["oy"]=N(5);
        auto& cc=c.object["frameList"].array[0].object["content"];
        cc.object["src"]=S(child_src);cc.object["coord"]=A({N(cx),N(cy)});cc.object["ox"]=N(cox);cc.object["oy"]=N(coy);
        c.object["children"]=A({Leaf()});p.object["children"]=A({c});Motion(d).object["layer"]=A({p});return d;
    };
    Check(Loads(s,build("blank:20:30:2:3",5,-3,9,2),e),e.c_str());
    auto out=Evaluate(s);Near(out[1].mesh[0],0.3,"origin+offset compensated by child position");
    Near(out[2].deformers[0].points[0],0.3,"combined scope after compensated domain");
    Near(out[2].deformers[0].off_x,9,"content offset kept separate from origin");
    ExpectDegraded(s,build("blank:20:30:2:3",5,-3,9,3),"meshCombine.fallback.domain","one pixel of offset mismatch degrades");
    Near(Evaluate(s)[1].mesh[0],0.2,"one pixel mismatch: child keeps its own grid, no combine");
    // Different origin/offset decomposition of the same complete D is accepted;
    // moving it into the origin alone (offset unchanged) is a different D.
    Check(Loads(s,build("blank:20:30:3:3",5,-3,8,2),e),e.c_str());
    Near(Evaluate(s)[1].mesh[0],0.3,"same D from another origin/offset split");
    ExpectDegraded(s,build("blank:20:30:3:3",5,-3,9,2),"meshCombine.fallback.domain","different origin same offset degrades");
    Near(Evaluate(s)[1].mesh[0],0.2,"different origin: child keeps its own grid, no combine");

    // Two continuing ordinary levels between structural shapes; every shape
    // ancestor contributes OWN-I once and only incorporated heads are consumed.
    auto d=Document();auto a=ShapeNode(d,"A",0.1f,false),b=ShapeNode(d,"B",0.2f,true),c=ShapeNode(d,"C",0.3f,true);
    auto o1=Node(2,"o1",A({Key(0,2,O({{"src",S("layout")}}))})),o2=o1;
    o1.object["label"]=S("o1");o2.object["label"]=S("o2");o1.object["meshCombine"]=N(1);o2.object["meshCombine"]=N(1);
    c.object["children"]=A({Leaf()});
    b.object["children"]=A({c});o2.object["children"]=A({b});o1.object["children"]=A({o2});
    a.object["children"]=A({o1});Motion(d).object["layer"]=A({a});
    Check(Loads(s,d,e),e.c_str());out=Evaluate(s);
    Near(out[4].mesh[0],0.6,"C sums B and A OWN grids through continuing ordinary levels");
    Near(out[3].mesh[0],0.3,"B combines A only");
    Check(out.back().deformers.size()==1,"leaf sees only the combined C scope");
    Near(out.back().deformers[0].points[0],0.6,"combined C grid is the sole scope");
}
void DomainRejections() {
    for(int mode=0;mode<9;++mode) {
        auto d=Document();auto parent=ShapeNode(d,"parent",0.1f),child=ShapeNode(d,"child",0.2f,true);
        auto& content=child.object["frameList"].array[0].object["content"];
        if(mode==0)content.object["coord"]=A({N(1),N(0)});
        else if(mode==1)content.object["src"]=S("blank:21:30:2:3");
        else if(mode==2)content.object["ox"]=N(1);
        else if(mode==3)content.object["zx"]=N(0);
        else if(mode==4)child.object["inheritMask"]=N(0x1fc);
        else if(mode==5) {
            auto future=content;future.object["coord"]=A({N(1),N(0)});
            child.object["frameList"].array.push_back(Key(50,2,future));
        } else if(mode==6)child.object["type"]=N(1);
        else if(mode==7)child.object["meshSyncChildMask"]=N(9);
        else {
            auto future=parent.object["frameList"].array[0].object["content"];
            future.object["zx"]=N(0);parent.object["frameList"].array.push_back(Key(50,2,future));
        }
        parent.object["children"]=A({child});Motion(d).object["layer"]=A({parent});
        // Unproven dynamic/full-domain/self-scope combines DEGRADE: the child
        // keeps its own grid (0.2), never the unproven 0.3 combination.
        EmoteScene s;std::string e;
        ExpectDegraded(s,d,"meshCombine.fallback","unproven dynamic/full-domain/selfscope degrades");
        Check(s.UnsupportedFeatures().Count("meshCombine.fallback")==1,"only the unprovable child falls back");
        const auto out=Evaluate(s);
        Check(out.size()==2 && !out[1].mesh.empty(),"fallback child still draws its own mesh");
        Near(out[1].mesh[0],0.2,"fallback child does not apply the unproven combine");
    }
    // combine=true on a non-shape node does not manufacture a grid/scope.
    auto d=Document();auto ordinary=Icon(d);ordinary.object["meshCombine"]=N(1);
    ordinary.object["children"]=A({Leaf()});Motion(d).object["layer"]=A({ordinary});
    EmoteScene s;std::string e;Check(s.Load(Model(d),e),e.c_str());
    Check(Evaluate(s)[1].deformers.empty(),"selfscope requires meshTransform+sync8+extent");

    // A shape-inheritance barrier removes the old shape chain. Do not search
    // around it then guess where to remove an incorporated middle anchor.
    auto barrier=Document();auto a=ShapeNode(barrier,"outer",0.1f);
    auto b=ShapeNode(barrier,"child",0.2f,true);
    auto holder=Node(2,"barrier",A({Key(0,2,O({{"src",S("layout")}}))}),A({b}));
    holder.object["meshCombine"]=N(1);holder.object["inheritMask"]=N(0x1fc);
    a.object["children"]=A({holder});Motion(barrier).object["layer"]=A({a});
    ExpectDegraded(s,barrier,"meshCombine.fallback.barrier","explicit unsupported shape barrier/anchor boundary degrades");
    Near(Evaluate(s)[2].mesh[0],0.2,"barrier child keeps its own grid");
}
void Budgets() {
    // Each list is below the core 64 MiB limit. Aggregate preflight sees >64
    // MiB BEFORE decode. Inf in first list proves decoding did not happen first.
    auto d=Document();auto grids=std::vector<EmoteCombinatorGrid>(256,Grid());
    grids[0][0]=std::numeric_limits<float>::infinity();
    const auto entry=Entry(d,"a",grids);
    auto list=A({});for(int i=0;i<1024;++i)list.array.push_back(entry); // 32 MiB
    auto n=Node(0,"big",A({Key(0,2,Content())}));n.object["meshCombinator"]=O({{"combinatorList",list}});
    Motion(d).object["layer"]=A({n,n,n});
    EmoteScene s;std::string e;Check(!s.Load(Model(std::move(d)),e),"aggregate cache limit");
    Check(e.find("cache byte budget")!=std::string::npos,"whole-cache preflight before first decode");

    auto many=Document();auto icon=Icon(many);auto nodes=A({});
    for(int i=0;i<850;++i)nodes.array.push_back(icon);
    Motion(many).object["layer"]=std::move(nodes);
    Check(s.Load(Model(std::move(many)),e),e.c_str()); // default side=8 fits
    std::vector<EmoteSceneLayer> old(1);old[0].label="old";
    Check(!s.Evaluate(0,{},old,e,32),"side32 cumulative output limit");
    Check(e.find("output byte budget")!=std::string::npos,"output budget diagnostic");
    Check(old.size()==1 && old[0].label=="old","output budget transaction");

    // A huge frame-bp scope, copied to many descendants, exceeds Load's
    // instance budget even though the parsed-combinator cache is tiny/empty.
    auto scopes=Document();auto leaf=Node(0,"leaf",A({Key(0,2,Content())}));
    auto children=A({});for(int i=0;i<1000;++i)children.array.push_back(leaf);
    auto root=Node(2,"scope",A({Key(0,2,O({{"src",S("blank:20:20:0:0")}}))}),children);
    auto bp=A({});for(int y=0;y<100;++y)for(int x=0;x<100;++x) {bp.array.push_back(N(x/99.));bp.array.push_back(N(y/99.));}
    root.object["frameList"].array[0].object["content"].object["mesh"]=O({{"bp",bp}});
    root.object["meshTransform"]=N(1);root.object["meshSyncChildMask"]=N(8);
    Motion(scopes).object["layer"]=A({root});
    Check(!s.Load(Model(std::move(scopes)),e),"aggregate instance output budget");
    Check(e.find("aggregate output byte budget")!=std::string::npos,"Load output preflight diagnostic");
}
}
int main() {
    // Every section runs even after a failure so one run reports them all.
    int failures=0;
    const auto run=[&](const char* name,void(*body)()) {
        try {body();}
        catch(const std::exception& e) {++failures;std::fprintf(stderr,"FAIL [%s]: %s\n",name,e.what());}
    };
    run("OwnAndReload",OwnAndReload);run("ScopeAndAttachments",ScopeAndAttachments);
    run("Rejections",Rejections);run("OwnershipAndNeutral",OwnershipAndNeutral);
    run("SameDomainCombine",SameDomainCombine);run("LowSyncChannelsAndProofs",LowSyncChannelsAndProofs);
    run("OriginsAndMultiLevel",OriginsAndMultiLevel);run("DomainRejections",DomainRejections);
    run("Budgets",Budgets);
    if(failures || g_failures)return 1;
    std::puts("emote_combinator_scene_regressions: passed");return 0;
}
