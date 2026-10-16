#include "pack/psb.h"
#include "render/emote_model.h"
#include "render/emote_scene.h"
#include "render/emote_player.h"
#include "render/compositor.h"
#include "log/logger.h"
#include "emote_scene_fixture.h"
#include <cmath>
#include <limits>
#include <memory>
#include <zlib.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#if defined(ARTC_HAS_GLES)
#include <EGL/egl.h>
#endif

static void Check(bool ok,const char* why) {
    if(!ok){std::cerr<<why<<'\n';std::exit(1);}
}
static std::vector<uint8_t> Fixture() {
    std::vector<uint8_t> b(44);b[0]='P';b[1]='S';b[2]='B';b[4]=3;
    auto field=[&](size_t at){for(int i=0;i<4;++i)b[at+i]=uint8_t(b.size()>>(8*i));};
    auto array=[&](const std::vector<uint8_t>& a){b.push_back(13);b.push_back(uint8_t(a.size()));b.push_back(13);b.insert(b.end(),a.begin(),a.end());};
    field(12);array({0});std::vector<uint8_t> parent(99);parent[98]=97;array(parent);array({98}); // trie: "a"
    field(16);array({0});field(20);b.insert(b.end(),{'h','i',0});
    field(24);array({0});field(28);array({4});field(32);b.insert(b.end(),{255,0,0,255});
    field(36);b.push_back(33);array({0});array({0}); // {a: ["hi", resource0, -2, true]}
    b.push_back(32);array({0,2,4,6});b.insert(b.end(),{21,0,25,0,5,254,3});return b;
}
static artc::PsbValue N(double n) {artc::PsbValue v;v.type=artc::PsbValue::Number;v.number=n;return v;}
static artc::PsbValue S(const std::string& s) {artc::PsbValue v;v.type=artc::PsbValue::String;v.string=s;return v;}
static artc::PsbValue A(std::initializer_list<artc::PsbValue> a) {artc::PsbValue v;v.type=artc::PsbValue::Array;v.array=a;return v;}
static artc::PsbValue O(std::initializer_list<std::pair<const std::string,artc::PsbValue>> o) {artc::PsbValue v;v.type=artc::PsbValue::Object;v.object=o;return v;}
static void ModelTests() {
    auto key=[](double t,double v,double e=0){return O({{"time",N(t)},{"type",N(2)},{"content",O({{"value",N(v)},{"easing",N(e)}})}});};
    auto track=[&](const char* label){return O({{"label",S(label)},{"frameList",A({key(0,0),key(10,10,1),O({{"time",N(20)},{"type",N(0)}})})}});};
    artc::PsbDocument doc;doc.root=O({{"spec",S("krkr")},{"object",O({{"body",O({{"motion",O({{"idle",O({})}})}})}})},
        {"metadata",O({{"base",O({{"chara",S("body")},{"motion",S("idle")}})},
        {"variableList",A({O({{"label",S("head")}}),O({{"label",S("eye")}})})},
        {"instantVariableList",A({S("eye")})},
        {"timelineControl",A({O({{"label",S("blink")},{"lastTime",N(-1)},{"loopBegin",N(0)},{"loopEnd",N(20)},
        {"variableList",A({track("head"),track("eye")})}})})}})}});
    artc::EmoteModel model;std::string error;Check(model.Load(doc,error),error.c_str());
    std::map<std::string,double> values;
    Check(model.Sample("blink",5,values) && std::abs(values.at("head")-2.5)<1e-6 && values.at("eye")==0,"continuous easing and discrete expression tracks");
    Check(model.Sample("blink",15,values) && values.at("head")==10 && values.at("eye")==10,"terminal marker retains previous value");
    Check(model.Sample("blink",45,values) && std::abs(values.at("head")-2.5)<1e-6,"timeline seek wraps multiple loops");
    Check(!model.Sample("missing",0,values) && values.count("head"),"unknown timeline preserves output");
    doc.root.object["metadata"].object["base"].object["motion"]=S("missing");
    Check(!model.Load(doc,error) && model.Timelines().size()==1,"invalid model preserves prior timelines");
    doc.root.object["metadata"].object["base"].object["motion"]=S("idle");
    artc::PsbValue resource;resource.type=artc::PsbValue::Resource;
    doc.root.object["source"]=O({{"face",O({{"icon",O({{"smile",O({{"width",N(1)},{"height",N(1)},
        {"originX",N(3.5)},{"originY",N(-2)},{"type",S("RGBA8")},{"pixel",resource},{"pal",resource}})}})}})}});
    doc.bytes={10,20,30,128};doc.resources={{0,4}};
    Check(model.Load(doc,error),error.c_str());artc::EmoteImage image;
    Check(model.Image("face","smile",image,error) && image.rgba==std::vector<uint8_t>({30,20,10,128}) && image.origin_x==3.5 && image.origin_y==-2,
        "desktop color order, independent sprite origins and unused palette reference");
    auto& icon=doc.root.object["source"].object["face"].object["icon"].object["smile"];
    icon.object["type"]=S("CI8");icon.object["pal"].resource=1;
    doc.bytes={0,10,20,30,128};doc.resources={{0,1},{1,4}};
    Check(model.Load(doc,error) && model.Image("face","smile",image,error) && image.rgba[0]==30,"CI8 palette expansion");
    doc.bytes[0]=1;Check(model.Load(doc,error),error.c_str());
    Check(!model.Image("face","smile",image,error) && image.rgba[0]==30,"invalid texture retains prior pixels");
    // win/common shared-texture atlas: each icon crops left/top/width/height.
    artc::PsbDocument atlas_doc;atlas_doc.root=O({{"spec",S("common")},
        {"object",O({{"body",O({{"motion",O({{"idle",O({})}})}})}})},
        {"metadata",O({{"base",O({{"chara",S("body")},{"motion",S("idle")}})}})},
        {"source",O({{"body",O({{"texture",O({{"width",N(4)},{"height",N(2)},
            {"type",S("RGBA8")},{"pixel",resource}})},
            {"icon",O({{"art",O({{"left",N(2)},{"top",N(1)},{"width",N(2)},{"height",N(1)},
                {"originX",N(1)},{"originY",N(0.5)}})}})}})}})}});
    atlas_doc.bytes={10,20,30,40, 50,60,70,80, 90,100,110,120, 130,140,150,160,
                     170,180,190,200, 210,220,230,240, 250,1,2,3, 4,5,6,7};
    atlas_doc.resources={{0,32}};
    artc::EmoteModel atlas_model;
    Check(atlas_model.Load(atlas_doc,error),error.c_str());
    Check(atlas_model.Image("body","art",image,error) && image.width==2 && image.height==1 &&
          image.origin_x==1 && image.origin_y==0.5 &&
          image.rgba==std::vector<uint8_t>({250,1,2,3,4,5,6,7}),
          "shared texture atlas crop");
    // E8: the remaining desktop atlas formats decode to RGBA8.
    auto decode_pixels=[&](int w,int h,const char* type,std::vector<uint8_t> bytes,
                           std::vector<uint8_t>& rgba,bool mips=false)->bool {
        artc::PsbDocument doc;
        artc::PsbValue pixel_ref;pixel_ref.type=artc::PsbValue::Resource;
        artc::PsbValue icon=O({{"width",N(w)},{"height",N(h)},
            {"type",S(type)},{"pixel",pixel_ref}});
        if(mips)icon.object["mipMap"]=N(1);
        doc.root=O({{"spec",S("common")},
            {"object",O({{"body",O({{"motion",O({{"idle",O({})}})}})}})},
            {"metadata",O({{"base",O({{"chara",S("body")},{"motion",S("idle")}})}})},
            {"source",O({{"s",O({{"icon",O({{"i",icon}})}})}})}});
        doc.resources={{0,bytes.size()}};
        doc.bytes=std::move(bytes);
        artc::EmoteModel m;std::string error;artc::EmoteImage image;
        if(!m.Load(doc,error) || !m.Image("s","i",image,error))return false;
        rgba=image.rgba;return true;
    };
    {
        std::vector<uint8_t> rgba;
        // DXT1: c0 red > c1 blue, all indices 0 -> red.
        Check(decode_pixels(4,4,"DXT1",{0x00,0xF8,0x1F,0x00,0,0,0,0},rgba) &&
              rgba.size()==64 && rgba[0]==255 && rgba[1]==0 && rgba[2]==0 && rgba[3]==255,
              "DXT1 opaque block");
        // DXT1 punch-through: c0 < c1, pixel 0 index 3 -> transparent black.
        Check(decode_pixels(4,4,"DXT1",{0x1F,0x00,0x00,0xF8,3,0,0,0},rgba) &&
              rgba[0]==0 && rgba[3]==0,"DXT1 punch-through alpha");
        // DXT3: explicit alpha nibbles (pixel0 0xF, pixel1 0x2), red colour.
        Check(decode_pixels(4,4,"DXT3",{0x2F,0,0,0,0,0,0,0,0x00,0xF8,0,0,0,0,0,0},rgba) &&
              rgba[0]==255 && rgba[3]==255 && rgba[7]==34,"DXT3 explicit alpha");
        Check(decode_pixels(1,1,"A8L8",{0x80,0x40},rgba) &&
              rgba==std::vector<uint8_t>({0x40,0x40,0x40,0x80}),"A8L8 luminance+alpha");
        Check(decode_pixels(1,1,"RGBA4444",{0x0F,0xF0},rgba) &&
              rgba==std::vector<uint8_t>({255,0,0,255}),"RGBA4444 expansion");
        Check(decode_pixels(1,1,"RGBA5551",{0xFF,0xFF},rgba) &&
              rgba==std::vector<uint8_t>({255,255,255,255}),"RGBA5551 expansion");
        Check(decode_pixels(1,1,"RGBA5650",{0x00,0xF8},rgba) &&
              rgba==std::vector<uint8_t>({255,0,0,255}),"RGBA5650 expansion");
        Check(decode_pixels(1,1,"RGBX8",{10,20,30,99},rgba) &&
              rgba==std::vector<uint8_t>({10,20,30,255}),"RGBX8 drops the X byte");
        // mip chains append levels after level 0; level 0 is still read, but
        // trailing bytes are rejected when no mip descriptor is present.
        Check(decode_pixels(1,1,"RGBA8",{1,2,3,4,9,9,9,9},rgba,true),"raw level 0 with trailing mips");
        Check(!decode_pixels(1,1,"RGBA8",{1,2,3,4,9,9,9,9},rgba),"trailing bytes without mips rejected");
        Check(decode_pixels(1,1,"RGBA8",{1,2,3,4},rgba) &&
              rgba==std::vector<uint8_t>({1,2,3,4}),"raw exact length still required without mips");
    }
}
// The playback contract: frame clock, official flags, fades, loop control,
// skip/pass, base-vs-private difference controls (observed through the
// parameterized face icon) and Load atomicity.
static void PlayerTests() {
    std::string error;
    auto document=emote_fixture::PlayerDocument();
    // The encoder round-trips through the real decoder before anything trusts it.
    {artc::PsbDocument decoded;const auto bytes=emote_fixture::EncodePsb(document);
     Check(artc::DecodePsb(bytes,decoded,error),error.c_str());
     Check(decoded.root.At("metadata").At("timelineControl").array.size()==3,"test PSB writer round-trips timelineControl");}
    auto model=std::make_shared<artc::EmoteModel>();
    Check(model->Load(std::move(document),error),error.c_str());

    artc::EmotePlayer player;
    Check(!player.Active(),"player starts inactive");
    Check(!player.Load(nullptr,error) && error.find("missing")!=std::string::npos,"null model rejected");
    Check(player.Load(model,error),error.c_str());
    Check(player.Active(),"loaded player is active");
    Check(player.CountMainTimelines()==2 && player.MainTimelineLabelAt(0)=="loop" &&
          player.MainTimelineLabelAt(1)=="once","main timeline enumeration follows the model map");
    Check(player.CountDiffTimelines()==1 && player.DiffTimelineLabelAt(0)=="delta","difference timeline enumeration");
    Check(player.CountVariables()==1 && player.VariableLabelAt(0)=="expression","variable enumeration");
    Check(model->SyncFrame("actor","idle")==10,"sync frame derivation follows child motions");
    Check(player.TimelineTotalFrames("once")==60 && player.TimelineTotalFrames("loop")==20 &&
          player.TimelineTotalFrames("missing")<0,"timeline lengths");
    Check(player.IsLoopTimeline("loop") && !player.IsLoopTimeline("once"),"loop detection");
    Check(!player.PlayTimeline("missing",0,error) && error.find("unknown")!=std::string::npos,"unknown timeline rejected");
    Check(player.PlayTimeline("once",0,error) && player.IsTimelinePlaying("once"),"play starts the timeline");
    Check(player.PlayTimeline("delta",artc::EmotePlayer::kTimelinePrivateDiff,error) &&
          player.IsTimelinePlaying("delta") && !player.IsTimelinePlaying("once") &&
          player.CountPlayingTimelines()==1 && player.PlayingTimelineLabelAt(0)=="delta" &&
          player.PlayingTimelineFlagsAt(0)==2,
          "flags 2 clears existing playback and starts private difference playback immediately");
    player.Progress(1000);  // 60 frames: the infinite delta remains live, never queued
    Check(player.IsTimelinePlaying("delta") && player.CountPlayingTimelines()==1,
          "private difference playback remains live across loop boundaries");

    // transform round-trips
    player.SetCoord(5,6,0,0);double x=0,y=0;player.GetCoord(&x,&y);
    Check(x==5 && y==6,"coordinate round-trip");
    player.SetScale(2,3,0,0);player.GetScale(&x,&y);
    Check(x==2 && y==3,"scale round-trip");
    player.SetRot(90,0,0);Check(player.GetRot()==90,"rotation round-trip");
    player.SetColor(0x80ABCDEFu,0,0);Check(player.GetColor()==0x80ABCDEFu,"color round-trip");
    player.SetMirror(true);Check(player.IsMirrored(),"mirror flag");
    player.Hide();Check(player.IsHidden(),"hide flag");player.Show();

    // Artemis script contract: setCoord(x, y, z, angle) and setScale(scale,
    // origin, origin) are instant placements; setColor is 0xRRGGBBAA.
    player.SetCoordAngle(7,8,0,45);
    player.GetCoord(&x,&y);
    Check(x==7 && y==8 && player.GetZ()==0 && player.GetRot()==45,"setCoord angle contract");
    player.SetScaleOrigin(0.5,4,6);
    player.GetScale(&x,&y);
    Check(x==0.5 && y==0.5 && player.GetOriginX()==4 && player.GetOriginY()==6,
          "setScale origin contract");
    player.SetColor(0x112233ffu,0,0);
    Check(player.GetColor()==0x112233ffu,"RGBA color round-trip (opaque)");
    player.SetColor(0x44556600u,0,0);
    Check(player.GetColor()==0x44556600u,"RGBA color round-trip (transparent)");

    // Engine knobs recorded from the shipped scripts.
    player.SetMeshDivisionRatio(0.4);
    player.SetHairScale(0.5);
    player.SetBustScale(0.75);
    Check(player.MeshDivisionRatio()==0.4 && player.HairScale()==0.5 && player.BustScale()==0.75,
          "mesh/hair/bust knobs recorded");

    // render + frame clock through the compositor (host SetPixels build)
    artc::Compositor compositor;
    Check(player.Render(compositor,"p",error),error.c_str());
    Check(compositor.GetLayerInfo("p").found && compositor.GetLayerInfo("p.000001").found,
          "render installs the container and scene layers");
    player.Progress(5000.0/3.0);  // +100 frames on top of the 60 already played
    Check(player.Render(compositor,"p",error),error.c_str());
    // 160 frames wraps to 6 → group coord 8+10*0.6 = 14; the body icon origin
    // (2,2) offsets the part layer.
    Check(std::abs(compositor.GetLayerInfo("p.000001.000001.000000").left-12.0)<1e-3,
          "progress advances the base motion at 60 fps");
    artc::EmotePlayer ticking;
    Check(ticking.Load(model,error),error.c_str());
    Check(ticking.Update(0,&compositor,"q") && compositor.GetLayerInfo("q").found,"update renders");
    Check(ticking.Update(500,&compositor,"q"),error.c_str());  // dt 500 ms → 30 frames → frame 8
    Check(std::abs(compositor.GetLayerInfo("q.000001.000001.000000").left-14.0)<1e-3,
          "update ticks wall-clock deltas");  // frame 8 → 16, minus the 2px icon origin

    // progress=false (the createEmoteLayer option): the wall clock is ignored
    // until the script calls progress(); step() operates on controls, not time.
    artc::EmotePlayer manual;
    Check(manual.Load(model,error),error.c_str());
    manual.SetAutoProgress(false);
    Check(manual.PlayTimeline("once",1,error),error.c_str());
    Check(manual.Update(0,&compositor,"m") && manual.Update(1000,&compositor,"m"),error.c_str());
    Check(manual.IsTimelinePlaying("once"),"manual progress ignores the wall clock");
    manual.Progress(1000);  // 60 frames → the finite timeline finishes
    Check(!manual.IsTimelinePlaying("once"),"explicit progress advances a manual player");

    // native pivot fold: translate(coord) * scale * translate(-origin)
    artc::EmotePlayer pivoted;
    Check(pivoted.Load(model,error),error.c_str());
    pivoted.SetCoordAngle(0,0,0,0);
    pivoted.SetScaleOrigin(2,1,0);
    Check(pivoted.Render(compositor,"z",error),error.c_str());
    Check(std::abs(compositor.GetLayerInfo("z").left-(-2.0))<1e-6 &&
          std::abs(compositor.GetLayerInfo("z").top-0.0)<1e-6,
          "scale origin folds into the container position");

    // createEmoteLayer's width/height box centres the model origin: the
    // container is placed at (w/2, h/2) before setCoord (reference host
    // model_origin). Regression for the device 立绘 placement.
    artc::EmotePlayer sized;
    Check(sized.Load(model,error),error.c_str());
    sized.SetLayerSize(1920,2048);
    sized.SetCoordAngle(3,4,0,0);
    Check(sized.Render(compositor,"s",error),error.c_str());
    Check(std::abs(compositor.GetLayerInfo("s").left-(963.0))<1e-6 &&
          std::abs(compositor.GetLayerInfo("s").top-(1028.0))<1e-6,
          "layer size centres the model origin");

    // Immediate placements must survive Skip()/Pass() (device 立绘 regression:
    // a skip after creation zeroed scale/alpha/coord and hid the sprite).
    artc::EmotePlayer skipped;
    Check(skipped.Load(model,error),error.c_str());
    skipped.SetCoordAngle(7,8,0,45);
    skipped.SetScaleOrigin(0.5,0,0);
    skipped.SetColor(0x404040ffu,0,0);
    skipped.Pass();
    skipped.Skip();
    double skip_sx=0,skip_sy=0;
    skipped.GetCoord(&x,&y);
    skipped.GetScale(&skip_sx,&skip_sy);
    Check(x==7 && y==8 && skip_sx==0.5 && skip_sy==0.5 && skipped.GetRot()==45 &&
          skipped.GetColor()==0x404040ffu,"skip/pass keeps immediate placements");

    // E11: SetColor tints the part layers with MODULATE2X (0x80 = neutral);
    // the raw 0xRRGGBBAA value round-trips through GetColor.
    artc::EmotePlayer tinted;
    Check(tinted.Load(model,error),error.c_str());
    tinted.SetColor(0x7f4040ffu,0,0);
    Check(tinted.Render(compositor,"t",error),error.c_str());
    bool tinted_layer=false;
    for(const auto& l:compositor.Layers())
        if(l.id.rfind("t",0)==0 && l.id.size()>=7 &&
           l.id.compare(l.id.size()-7,7,".000000")==0 && l.effect.multiply==0xFE8080u)
            tinted_layer=true;
    Check(tinted_layer,"MODULATE2X color tint reaches the part layers");
    Check(tinted.GetColor()==0x7f4040ffu,"color value round-trips through the tint");

    // WP5: per-frame color multiplies the part tint (MODULATE2X, 0x80 neutral).
    {
        auto color_document=emote_fixture::Scene();
        auto& face_content=color_document.root.object["object"].object["actor"].object["motion"]
            .object["face"].object["layer"].array[0].object["frameList"].array[0].object["content"];
        face_content.object["color"]=N(0x404080ff);
        auto color_model=std::make_shared<artc::EmoteModel>();
        Check(color_model->Load(std::move(color_document),error),error.c_str());
        artc::EmotePlayer colored;
        Check(colored.Load(color_model,error),error.c_str());
        Check(colored.Render(compositor,"w1",error),error.c_str());
        bool tinted_part=false;
        for(const auto& l:compositor.Layers())
            if(l.id.rfind("w1",0)==0 && l.id.size()>=7 &&
               l.id.compare(l.id.size()-7,7,".000000")==0 && l.effect.multiply==0x8080FFu)
                tinted_part=true;
        Check(tinted_part,"frame color multiplies the part tint");
    }

    // WP8: SkipToSync parks the base motion at the derived sync frame.
    artc::EmotePlayer synced;
    Check(synced.Load(model,error),error.c_str());
    synced.Progress(10*1000.0/60.0);  // exactly 10 frames
    Check(synced.Render(compositor,"y",error),error.c_str());
    const float at_sync=compositor.GetLayerInfo("y.000001.000001.000000").left;
    synced.SkipToSync();
    Check(synced.Render(compositor,"y",error),error.c_str());
    Check(compositor.GetLayerInfo("y.000001.000001.000000").left==at_sync,
          "skipToSync parks at the sync frame");

    // ComposeVariables is private — observe mixing through the parameterized
    // face icon: body picture is 4px wide, the face 2px ("face") or 4px ("wide").
    auto picture_sum=[&](const char* id) {
        int sum=0;
        for(const auto& l:compositor.Layers())
            if(l.id.rfind(id,0)==0 && l.id.size()>=7 && l.id.compare(l.id.size()-7,7,".000000")==0)
                sum+=int(compositor.GetLayerInfo(l.id).width);
        return sum;
    };
    artc::EmotePlayer mixer;
    Check(mixer.Load(model,error),error.c_str());
    Check(mixer.PlayTimeline("loop",1,error) &&
          mixer.SetTimelineBlendRatio("loop",2.0/3.0,error),error.c_str());
    mixer.Progress(8000.0/60.0);  // timeline setters reach 8, independently of blend
    bool mixed_found=false;
    Check(std::fabs(mixer.GetVariable("expression",&mixed_found)-8)<1e-6 && mixed_found &&
          mixer.Render(compositor,"r",error) && picture_sum("r")==6,
          "flags 1 commands the base variable rather than lerping by timeline blend");
    mixer.StopTimeline("loop");
    Check(mixer.SetVariable("expression",6,0,0,error) && mixer.FadeInTimeline("loop",200,0,error),error.c_str());
    mixer.Progress(8000.0/60.0);
    Check(mixer.PlayingTimelineFlagsAt(0)==3 &&
          mixer.GetVariable("expression",&mixed_found)==6 && mixed_found &&
          mixer.Render(compositor,"r",error) && picture_sum("r")==8,
          "fade-in uses flags 3 private difference even for metadata main timelines");  // 6+8*2/3 → wide
    mixer.StopTimeline("loop");
    Check(mixer.PlayTimeline("delta",1,error),error.c_str());
    mixer.Progress(8000.0/60.0);
    Check(std::fabs(mixer.GetVariable("expression",&mixed_found)-8)<1e-6 && mixed_found &&
          mixer.Render(compositor,"r",error) && picture_sum("r")==6,
          "official flags 1 takes precedence over metadata.diff and writes the base");

    // WP1: automatic eye blink (metadata.eyeControl). Native phase model:
    // 40% closing / 20% hold / 40% opening at 2.5x; waits are not animation.
    auto blink_document=emote_fixture::Scene();
    blink_document.root.object["metadata"].object["eyeControl"]=emote_fixture::A({
        emote_fixture::O({{"label",emote_fixture::S("expression")},
                          {"beginFrame",emote_fixture::N(0)},
                          {"endFrame",emote_fixture::N(10)},
                          {"blinkFrameCount",emote_fixture::N(4)},
                          {"blinkIntervalMin",emote_fixture::N(10)},
                          {"blinkIntervalMax",emote_fixture::N(10)}})});
    auto blink_model=std::make_shared<artc::EmoteModel>();
    Check(blink_model->Load(std::move(blink_document),error),error.c_str());
    Check(blink_model->Blinks().size()==1 && blink_model->Blinks()[0].variable=="expression" &&
          blink_model->Blinks()[0].frames==4,"eye control parsed");
    artc::EmotePlayer blinker;
    Check(blinker.Load(blink_model,error),error.c_str());
    Check(blinker.Render(compositor,"b",error) && picture_sum("b")==6 && !blinker.IsAnimating(),
          "a pending blink sits at the base value and is not animation");
    blinker.Progress(10*1000.0/60.0);  // interval elapses → closing starts
    blinker.Progress(2*1000.0/60.0);   // 2 frames ≥ the 1.6-frame closing arc
    Check(blinker.Render(compositor,"b",error) && picture_sum("b")==8,
          "blink closes to the end frame");
    blinker.Progress(10*1000.0/60.0);  // hold + open + next wait
    Check(blinker.Render(compositor,"b",error) && picture_sum("b")==6,
          "blink returns to the base value");
    Check(!blinker.IsAnimating(),"a blink cycle never reports animation");
    Check(blinker.Load(blink_model,error),error.c_str());  // reload resets the blink state

    // WP2: selectorControl — option crossfade (selected gets onValue, the
    // blend factor is min(|value-index|,1)); selector labels are settable.
    auto selector_document=emote_fixture::Scene();
    selector_document.root.object["metadata"].object["variableList"].array.push_back(
        emote_fixture::O({{"label",emote_fixture::S("partner")}}));
    selector_document.root.object["metadata"].object["selectorControl"]=emote_fixture::A({
        emote_fixture::O({
            {"label",emote_fixture::S("clothes")},
            {"enabled",emote_fixture::N(1)},
            {"optionList",emote_fixture::A({
                emote_fixture::O({{"label",emote_fixture::S("expression")},
                                  {"onValue",emote_fixture::N(10)},
                                  {"offValue",emote_fixture::N(0)}}),
                emote_fixture::O({{"label",emote_fixture::S("partner")},
                                  {"onValue",emote_fixture::N(1)},
                                  {"offValue",emote_fixture::N(0)}}),
            })},
        }),
    });
    auto selector_model=std::make_shared<artc::EmoteModel>();
    Check(selector_model->Load(std::move(selector_document),error),error.c_str());
    Check(selector_model->Selectors().size()==1 && selector_model->Selectors()[0].items.size()==2,
          "selector control parsed");
    {
        const auto& sel=selector_model->Selectors()[0];
        Check(std::abs(artc::EmotePlayer::SelectorOptionValue(sel,0.0,0)-10)<1e-9 &&
              std::abs(artc::EmotePlayer::SelectorOptionValue(sel,1.0,0)-0)<1e-9 &&
              std::abs(artc::EmotePlayer::SelectorOptionValue(sel,0.5,0)-5)<1e-9 &&
              std::abs(artc::EmotePlayer::SelectorOptionValue(sel,1.0,1)-1)<1e-9,
              "selector crossfade formula");
    }
    artc::EmotePlayer clothes;
    Check(clothes.Load(selector_model,error),error.c_str());
    Check(clothes.Render(compositor,"s",error) && picture_sum("s")==8,
          "selector defaults to option 0 (onValue)");
    Check(clothes.SetVariable("clothes",1,0,0,error),error.c_str());
    Check(clothes.Render(compositor,"s",error) && picture_sum("s")==6,
          "selecting option 1 hands the first option its offValue");

    // WP3: attrcomp removes layers (value<=0) before validation and evaluation.
    auto removal_document=emote_fixture::Scene();
    removal_document.root.object["metadata"].object["attrcomp"]=emote_fixture::A({
        emote_fixture::O({
            {"label",emote_fixture::S("clothes")},
            {"data",emote_fixture::O({
                {"remove",emote_fixture::A({
                    emote_fixture::O({
                        {"value",emote_fixture::N(0)},
                        {"id",emote_fixture::O({{"chara",emote_fixture::S("actor")},
                                                {"motion",emote_fixture::S("idle")},
                                                {"layer",emote_fixture::S("face child")}})},
                    }),
                })},
            })},
        }),
    });
    // The removed node carries an unsupported stencil flag on purpose: the
    // load must still succeed because validation skips removed nodes.
    removal_document.root.object["object"].object["actor"].object["motion"].object["idle"]
        .object["layer"].array[0].object["children"].array[1].object["stencilType"]=emote_fixture::N(1);
    auto removal_model=std::make_shared<artc::EmoteModel>();
    Check(removal_model->Load(std::move(removal_document),error),error.c_str());
    Check(removal_model->Removals().size()==1,"attrcomp removal parsed");
    artc::EmotePlayer removed;
    Check(removed.Load(removal_model,error),error.c_str());
    Check(removed.Render(compositor,"k",error) && picture_sum("k")==4,
          "attrcomp hides the removed node");

    // WP4: metadata.mirror flips by default; Lua setMirror overrides.
    auto mirror_document=emote_fixture::Scene();
    mirror_document.root.object["metadata"].object["mirror"]=emote_fixture::N(1);
    auto mirror_model=std::make_shared<artc::EmoteModel>();
    Check(mirror_model->Load(std::move(mirror_document),error),error.c_str());
    Check(mirror_model->Mirrored(),"mirror metadata parsed");
    artc::EmotePlayer mirrored;
    Check(mirrored.Load(mirror_model,error),error.c_str());
    Check(mirrored.IsMirrored(),"model mirror applies by default");
    mirrored.SetMirror(false);
    Check(mirrored.IsMirrored() && !mirrored.Mirror(),"SetMirror(false) assigns user=false; effective stays metadata");
    mirrored.SetMirror(true);
    Check(!mirrored.IsMirrored() && mirrored.Mirror(),"user mirror XORs with the model bit");

    // WP9: `shape/...` (type==1) nodes give hit-test geometry; the SDK figure
    // is a 16x16 unit square scaled/rotated by the node transform.
    auto shape_document=emote_fixture::Scene();
    auto shape_node=emote_fixture::Node(1,"hit",emote_fixture::A({
        emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("shape/rect")},
            {"coord",emote_fixture::A({emote_fixture::N(20),emote_fixture::N(10),emote_fixture::N(0)})},
            {"zx",emote_fixture::N(2)},{"zy",emote_fixture::N(2)}})),
        emote_fixture::Key(11,0)}));
    auto circle_node=emote_fixture::Node(1,"ring",emote_fixture::A({
        emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("shape/circle")},
            {"coord",emote_fixture::A({emote_fixture::N(60),emote_fixture::N(10),emote_fixture::N(0)})}})),
        emote_fixture::Key(11,0)}));
    shape_document.root.object["object"].object["actor"].object["motion"].object["idle"]
        .object["layer"].array.push_back(shape_node);
    shape_document.root.object["object"].object["actor"].object["motion"].object["idle"]
        .object["layer"].array.push_back(circle_node);
    auto shape_model=std::make_shared<artc::EmoteModel>();
    Check(shape_model->Load(std::move(shape_document),error),error.c_str());
    artc::EmotePlayer shapes;
    Check(shapes.Load(shape_model,error),error.c_str());
    Check(shapes.Render(compositor,"h",error),error.c_str());
    Check(shapes.Contains(compositor,"h","hit",20,10) &&
          !shapes.Contains(compositor,"h","hit",40,10),
          "rect shape hit test uses the scaled node transform");
    Check(shapes.Contains(compositor,"h","ring",60,10) &&
          !shapes.Contains(compositor,"h","ring",60,19),
          "circle shape hit test uses the inscribed radius");
    Check(shapes.Contains(compositor,"h","",60,10) && !shapes.Contains(compositor,"h","missing",60,10),
          "unlabeled hit tests span every shape, labels filter");

    // WP10: setMeshDivisionRatio scales the warped-grid subdivision (the full
    // quality default 1.0 keeps the 8x8 output).
    {
        auto mesh_document=emote_fixture::Scene();
        artc::PsbValue bp;bp.type=artc::PsbValue::Array;
        for(int j=0;j<4;++j)for(int i=0;i<4;++i) {
            bp.array.push_back(N(double(i)/3.0));
            bp.array.push_back(N(double(j)/3.0));
        }
        auto& face_content=mesh_document.root.object["object"].object["actor"].object["motion"]
            .object["face"].object["layer"].array[0].object["frameList"].array[0].object["content"];
        face_content.object["mesh"]=O({{"bp",bp}});
        auto mesh_model=std::make_shared<artc::EmoteModel>();
        Check(mesh_model->Load(std::move(mesh_document),error),error.c_str());
        artc::EmotePlayer full,coarse;
        Check(full.Load(mesh_model,error) && coarse.Load(mesh_model,error),error.c_str());
        auto mesh_vertices=[&](const char* id) {
            size_t count=0;
            for(const auto& l:compositor.Layers())
                if(l.id.rfind(id,0)==0 && l.id.size()>=7 &&
                   l.id.compare(l.id.size()-7,7,".000000")==0 && !l.mesh.empty())
                    count+=l.mesh.size()/4;
            return count;
        };
        full.SetMeshDivisionRatio(1.0);
        Check(full.Render(compositor,"v1",error),error.c_str());
        coarse.SetMeshDivisionRatio(0.5);
        Check(coarse.Render(compositor,"v2",error),error.c_str());
        // Unindexed triangle list: (side-1)^2 cells * 2 triangles * 3 vertices.
        Check(mesh_vertices("v1")==7*7*6 && mesh_vertices("v2")==3*3*6,
              "mesh division ratio scales the warped grid subdivision");    }

    // transitions and ease weights
    artc::EmotePlayer vars;
    Check(vars.Load(model,error),error.c_str());
    Check(!vars.IsAnimating(),"fresh player is still");
    Check(vars.SetVariable("expression",10,1000,1,error),error.c_str());
    vars.Progress(500);
    bool found=false;
    Check(vars.GetVariable("expression",&found)==2.5 && found,"ease >= 0 weighs ease+1");
    Check(vars.SetVariable("expression",10,1000,-1,error),error.c_str());
    vars.Progress(500);
    Check(std::abs(vars.GetVariable("expression",&found)-(2.5+7.5*std::sqrt(0.5)))<1e-9 && found,
          "ease < 0 weighs 1/(1-ease)");
    Check(vars.IsAnimating(),"a running transition keeps the player animating");
    const double before_pass=vars.GetVariable("expression",&found);
    vars.Pass();
    Check(vars.GetVariable("expression",&found)==before_pass && found && vars.IsAnimating(),
          "pass leaves independent global transitions running without completing them");
    vars.Progress(500);
    Check(vars.GetVariable("expression",&found)==10 && found && !vars.IsAnimating(),
          "a global transition continues to its target after pass");
    Check(!vars.SetVariable("missing",1,0,0,error) && error.find("unknown")!=std::string::npos,
          "unknown variable rejected");
    Check(vars.GetVariable("missing",&found)==0 && !found,"unknown variable read reports missing");

    // Native additions recovered from the driver: SetGrayscale animates a
    // luminance blend on the textured scene layers, SetVariableDiff drives a
    // matched pair from one independent control; Step forwards timeline controls.
    artc::EmotePlayer shading;
    Check(shading.Load(model,error),error.c_str());
    shading.SetGrayscale(1,0,0);
    Check(shading.GetGrayscale()==1,"grayscale round-trip");
    Check(shading.Render(compositor,"g",error),error.c_str());
    bool grayscale_set=false;
    for(const auto& l:compositor.Layers())
        if(l.id.rfind("g",0)==0 && l.id.size()>=7 &&
           l.id.compare(l.id.size()-7,7,".000000")==0 && l.effect.grayscale>0.99f)
            grayscale_set=true;
    Check(grayscale_set,"grayscale reaches the textured scene layer");
    shading.SetGrayscale(0,1000,0);
    shading.Progress(500);
    Check(std::abs(shading.GetGrayscale()-0.5)<1e-9,"grayscale transitions");

    auto pair_document=emote_fixture::PlayerDocument();
    pair_document.root.object["metadata"].object["variableList"].array.push_back(O({{"label",S("partner")}}));
    auto pair_model=std::make_shared<artc::EmoteModel>();
    Check(pair_model->Load(std::move(pair_document),error),error.c_str());
    artc::EmotePlayer pairs;
    Check(pairs.Load(pair_model,error),error.c_str());
    Check(!pairs.SetVariableDiff("expression","missing",5,0,0,error) &&
          error.find("unknown")!=std::string::npos,"a diff pair requires model variables");
    Check(pairs.SetVariableDiff("expression","partner",5,1000,0,error),error.c_str());
    pairs.Progress(500);
    Check(pairs.GetVariable("expression",&found)==0 && found &&
          pairs.GetVariable("partner",&found)==0 && found,
          "an ordered difference controller leaves both base variables unchanged");
    Check(pairs.GetVariableDiff("expression","partner")==2.5,
          "the independent ordered difference controller reaches its halfway value");

    artc::EmotePlayer stepper;
    Check(stepper.Load(model,error),error.c_str());
    Check(stepper.PlayTimeline("once",1,error),error.c_str());
    stepper.Progress(100);
    const double step_frame=stepper.BaseFrame();
    stepper.Step();
    Check(!stepper.IsTimelinePlaying("once") && stepper.CountPlayingTimelines()==0 &&
          stepper.GetVariable("expression",&found)==60 && found && stepper.BaseFrame()==step_frame,
          "one step forwards a nonloop to its end, removes it and finishes its setters without ticking base time");
    Check(stepper.PlayTimeline("loop",1,error),error.c_str());
    stepper.Progress(100);
    const double loop_frame=stepper.BaseFrame();
    stepper.Step();
    Check(stepper.IsTimelinePlaying("loop") && stepper.BaseFrame()==loop_frame &&
          stepper.GetVariable("expression",&found)==20 && found,
          "step crosses a loop boundary once and completes the resulting global commands");
    Check(stepper.SetVariable("expression",7,0,0,error),error.c_str());
    stepper.Step();
    Check(stepper.IsTimelinePlaying("loop") && stepper.BaseFrame()==loop_frame &&
          stepper.GetVariable("expression",&found)==7 && found,
          "a second step does not recross the same loop boundary or reissue its setters");

    // Official flags: bit0 preserves peers, bit1 is private difference, never FIFO.
    artc::EmotePlayer queue;
    Check(queue.Load(model,error),error.c_str());
    Check(queue.PlayTimeline("loop",0,error) && queue.PlayTimeline("once",1,error),error.c_str());
    Check(queue.PlayTimeline("loop",artc::EmotePlayer::kTimelineParallel,error) &&
          queue.PlayingTimelineLabelAt(0)=="loop" &&
          queue.PlayingTimelineLabelAt(1)=="once" && queue.PlayingTimelineFlagsAt(0)==1,
          "replaying a label restarts it in place with the new flags");
    queue.StopTimeline("once");
    Check(queue.CountPlayingTimelines()==1 && queue.PlayingTimelineLabelAt(0)=="loop","stop removes the live timeline");
    Check(queue.PlayTimeline("delta",3,error) && queue.IsTimelinePlaying("delta") &&
          queue.IsTimelinePlaying("loop") && queue.CountPlayingTimelines()==2 &&
          queue.PlayingTimelineFlagsAt(1)==3,"flags 3 starts private difference playback alongside peers");
    Check(queue.PlayTimeline("once",0,error) && queue.IsTimelinePlaying("once") &&
          !queue.IsTimelinePlaying("loop") && !queue.IsTimelinePlaying("delta") &&
          queue.CountPlayingTimelines()==1,"flags 0 clears every peer before starting ordinary playback");
    queue.StopTimeline("once");
    queue.Progress(500);
    Check(queue.CountPlayingTimelines()==0 && queue.GetVariable("expression",&found)==30 && found,
          "stopping a timeline does not cancel the base setter command it already submitted");
    Check(queue.PlayTimeline("delta",2,error) && queue.IsTimelinePlaying("delta"),error.c_str());
    Check(queue.PlayTimeline("delta",3,error) && queue.CountPlayingTimelines()==1,
          "replaying a private timeline replaces its live state rather than queuing a copy");
    queue.StopTimeline("delta");
    Check(queue.CountPlayingTimelines()==0 && !queue.IsTimelinePlaying("delta"),"stop removes the private timeline without queued playback");

    // fades and manual blend
    Check(queue.FadeInTimeline("loop",200,0,error),error.c_str());
    Check(queue.TimelineBlendRatio("loop",&found)==0 && found,"fade-in starts silent");
    queue.Progress(100);
    Check(std::abs(queue.TimelineBlendRatio("loop",&found)-0.5)<1e-9 && found,"fade-in interpolates");
    Check(queue.FadeOutTimeline("loop",200,0,error),error.c_str());
    queue.Progress(200);  // blend reaches 0 → auto-stop
    Check(!queue.IsTimelinePlaying("loop") && queue.TimelineBlendRatio("loop",&found)==0 && !found,
          "fade-out removes the timeline at zero blend");
    Check(!queue.FadeOutTimeline("loop",100,0,error) && error.find("not playing")!=std::string::npos,
          "fading an idle timeline fails");
    Check(queue.FadeInTimeline("loop",100,0,error),error.c_str());
    queue.Progress(100);  // fade completes → blend 1
    Check(queue.FadeOutTimeline("loop",1000,0,error),error.c_str());
    queue.Progress(100);  // blend 1 → 0.9
    Check(queue.SetTimelineBlendRatio("loop",0.5,error),error.c_str());
    queue.Progress(2000);
    Check(queue.TimelineBlendRatio("loop",&found)==0.5 && found && queue.IsTimelinePlaying("loop"),
          "a direct blend set cancels the fade and its auto-stop");
    Check(queue.SetTimelineHoldEnd("loop",false,error),error.c_str());
    queue.Progress(1000);  // looping disabled: run through the authored end and remove
    Check(!queue.IsTimelinePlaying("loop"),"disabling looping removes a timeline at its authored end");

    // The native contract carries a transition: (label, ratio, time, ease, flags).
    artc::EmotePlayer blend;
    Check(blend.Load(model,error),error.c_str());
    Check(blend.PlayTimeline("loop",0,error),error.c_str());
    Check(blend.SetTimelineBlendRatio("loop",0.25,200,0,false,error),error.c_str());
    Check(std::abs(blend.TimelineBlendRatio("loop",&found)-1.0)<1e-9 && found,
          "a blend transition starts from the current ratio");
    blend.Progress(100);
    Check(std::abs(blend.TimelineBlendRatio("loop",&found)-0.625)<1e-9 && found,
          "a blend transition interpolates");
    blend.Progress(100);
    Check(std::abs(blend.TimelineBlendRatio("loop",&found)-0.25)<1e-9 && found,
          "a blend transition lands on the target");
    Check(blend.SetTimelineBlendRatio("loop",0.0,100,0,true,error),error.c_str());
    blend.Progress(100);
    Check(!blend.IsTimelinePlaying("loop"),"a blend transition with flags removes the timeline at zero");

    // Skip forwards/removes nonloops, preserves loops and finishes global commands.
    artc::EmotePlayer skipper;
    Check(skipper.Load(model,error),error.c_str());
    Check(skipper.PlayTimeline("once",1,error) && skipper.PlayTimeline("delta",3,error),error.c_str());
    Check(skipper.SetVariable("expression",10,5000,0,error),error.c_str());
    const double skip_frame=skipper.BaseFrame();
    skipper.Skip();
    Check(skipper.GetVariable("expression",&found)==10 && found && skipper.BaseFrame()==skip_frame,
          "skip completes already submitted global transitions without advancing base time");
    Check(skipper.IsTimelinePlaying("delta") && !skipper.IsTimelinePlaying("once") &&
          skipper.CountPlayingTimelines()==1,"skip removes a finite timeline and keeps an already live loop");
    skipper.StopAllTimelines();
    Check(skipper.CountPlayingTimelines()==0,"stop-all clears the player");

    // Pass submits future ordinary commands, but does not finish them globally.
    Check(skipper.PlayTimeline("once",1,error),error.c_str());
    skipper.Pass();
    Check(!skipper.IsTimelinePlaying("once") && skipper.GetVariable("expression",&found)==0 && found &&
          skipper.IsAnimating(),"pass removes a nonloop main timeline but leaves its future setter command animated");
    skipper.Progress(500);
    Check(skipper.GetVariable("expression",&found)==30 && found,
          "a future command submitted by pass advances after timeline removal");

    Check(skipper.SetVariable("expression",6,0,0,error) &&
          skipper.PlayTimeline("once",3,error) && skipper.PlayTimeline("delta",3,error),error.c_str());
    skipper.Pass();
    Check(skipper.IsTimelinePlaying("once") && skipper.IsTimelinePlaying("delta") &&
          skipper.TimelineBlendRatio("once",&found)==1 && found &&
          skipper.GetVariable("expression",&found)==6 && found,
          "pass retains private nonloops for a fade and leaves loops and base getters untouched");
    skipper.Progress(10*1000.0/60.0);
    Check(std::abs(skipper.TimelineBlendRatio("once",&found)-0.5)<1e-9 && found &&
          skipper.TimelineBlendRatio("delta",&found)==1 && found,
          "pass fades private nonloops over 20 frames, not 20 milliseconds, while loops keep full blend");
    skipper.Pass();
    skipper.Progress(10*1000.0/60.0);
    Check(!skipper.IsTimelinePlaying("once") && skipper.IsTimelinePlaying("delta") &&
          skipper.GetVariable("expression",&found)==6 && found,
          "repeated pass does not restart the private nonloop fade and never passes a loop");

    // a failed Load keeps the previous player intact
    auto broken=emote_fixture::PlayerDocument();
    broken.root.object["metadata"].object["base"].object["motion"]=S("missing");
    auto bad=std::make_shared<artc::EmoteModel>();
    Check(!bad->Load(std::move(broken),error),"broken model rejected");
    Check(!player.Load(bad,error) && player.Active() && player.IsTimelinePlaying("delta"),
          "a failed reload keeps the previous player intact");
}
static void SceneEffectResetTests() {
    // Reuse one icon in three nodes, and keep every node/part id unchanged as
    // the authored blend switches to normal. Re-uploading/recreating a layer
    // would conceal the incremental SetProps state leak this test guards.
    struct BlendCase {int bm;const char* mode;};
    const BlendCase cases[]={{3,"multiply"},{4,"screen"},{1,"add"}};
    auto document=emote_fixture::Scene();
    auto& idle=document.root.object["object"].object["actor"].object["motion"].object["idle"];
    idle.object["layer"]=A({});
    for(const auto& test:cases) {
        idle.object["layer"].array.push_back(emote_fixture::Node(0,test.mode,A({
            emote_fixture::Key(0,2,O({{"src",S("src/images/face")},{"bm",N(test.bm)}})),
            emote_fixture::Key(10,2,O({{"src",S("src/images/face")},{"bm",N(0)}})),
            emote_fixture::Key(11,0)})));
    }
    std::string error;
    auto model=std::make_shared<artc::EmoteModel>();
    Check(model->Load(std::move(document),error),error.c_str());
    artc::EmoteScene scene;
    Check(scene.Load(model,error),error.c_str());
    std::vector<artc::EmoteSceneLayer> initial,normal;
    Check(scene.Evaluate(0,{},initial,error),error.c_str());
    Check(scene.Evaluate(10,{},normal,error),error.c_str());
    Check(initial.size()==3 && normal.size()==initial.size(),"effect reset fixture has three persistent nodes");
    for(size_t i=0;i<initial.size();++i) {
        Check(initial[i].blend==cases[i].mode && normal[i].blend.empty(),
              "authored bm switches from multiply/screen/add to normal at frame 10");
        Check(initial[i].key==normal[i].key && initial[i].source=="images" &&
              initial[i].icon=="face" && initial[i].source==normal[i].source &&
              initial[i].icon==normal[i].icon,"effect reset keeps the same node key and shared icon");
    }
    artc::Compositor compositor;
    const std::string id="effect_reset";
    const artc::LayerEffect neutral;
    const uint32_t tint=0x804020u;
    auto part=[&](size_t i)->const artc::Layer& {
        const auto key=id+initial[i].key+".000000";
        for(const auto& layer:compositor.Layers())if(layer.id==key)return layer;
        Check(false,("effect reset missing persistent part "+key).c_str());
        std::abort();
    };
    Check(scene.Render(compositor,id,0,{},error,1,tint),error.c_str());
    const size_t layer_count=compositor.Layers().size();
    std::vector<std::string> layer_ids;
    for(const auto& layer:compositor.Layers())layer_ids.push_back(layer.id);
    std::vector<uint32_t> textures;
    for(size_t i=0;i<initial.size();++i) {
        const auto& layer=part(i);
        Check(layer.effect.grayscale==1 && layer.effect.multiply==tint &&
              layer.effect.blend==cases[i].mode,"initial render installs all non-neutral effects");
        textures.push_back(layer.texture);
        // An unrelated property must survive each Render: deleting and
        // recreating the same id is not an acceptable effect-reset fix either.
        compositor.SetProps(layer.id,{{"draggable","1"}});
    }
    auto persistent=[&]() {
        Check(compositor.Layers().size()==layer_count,"effect reset does not add or remove layers");
        for(size_t i=0;i<layer_ids.size();++i)
            Check(compositor.Layers()[i].id==layer_ids[i],"effect reset preserves every installed id");
        for(size_t i=0;i<initial.size();++i)
            Check(part(i).draggable && part(i).texture==textures[i],
                  "effect reset updates existing parts without recreating or re-uploading their icon");
    };
    // Collect effect failures so one run reports all three independent leaks,
    // while fixture/persistence failures still abort immediately via Check.
    size_t failures=0;
    auto expect=[&](size_t i,const char* field,const auto& expected,const auto& actual) {
        if(expected==actual)return;
        std::cerr<<"scene effect reset "<<part(i).id<<" "<<field
                 <<": expected="<<expected<<" actual="<<actual<<'\n';
        ++failures;
    };
    Check(scene.Render(compositor,id,0,{},error,0,tint),error.c_str());
    persistent();
    for(size_t i=0;i<initial.size();++i) {
        expect(i,"grayscale (1 -> 0)",neutral.grayscale,part(i).effect.grayscale);
        Check(part(i).effect.multiply==tint && part(i).effect.blend==cases[i].mode,
              "clearing grayscale preserves the still-authored tint and blend");
    }
    Check(scene.Render(compositor,id,0,{},error,0,neutral.multiply),error.c_str());
    persistent();
    for(size_t i=0;i<initial.size();++i) {
        expect(i,"multiply (0x804020 -> 0xFFFFFF)",neutral.multiply,part(i).effect.multiply);
        Check(part(i).effect.blend==cases[i].mode,"clearing tint preserves the still-authored blend");
    }
    Check(scene.Render(compositor,id,10,{},error,0,neutral.multiply),error.c_str());
    persistent();
    for(size_t i=0;i<initial.size();++i)
        expect(i,"blend (bm -> normal at frame 10)",neutral.blend,part(i).effect.blend);
    Check(failures==0,"scene effect reset must restore LayerEffect defaults on reused part ids");
}
static void SceneHitGeometryTests() {
    auto document=emote_fixture::Scene();
    auto node=[](int type,const char* label,const char* source,double x,double y) {
        return emote_fixture::Node(type,label,A({emote_fixture::Key(0,2,O({
            {"src",S(source)},{"coord",A({N(x),N(y),N(0)})}}))}));
    };
    auto triangle=node(0,"triangle","src/images/body",12,12);
    // Bilinear patch with the last corner collapsed produces one triangle
    // (the subdivisions still contain degenerate triangles on its far edge).
    triangle.object["frameList"].array[0].object["content"].object["mesh"]=O({
        {"bp",A({N(0),N(0),N(1),N(0),N(0),N(1),N(0),N(1)})}});
    auto blank=node(2,"blank","blank:100:100:50:50",40,40);
    blank.object["children"]=A({node(0,"child","src/images/face",0,0)});
    document.root.object["object"].object["actor"].object["motion"].object["idle"].object["layer"]=A({
        triangle,node(0,"quad","src/images/body",22,12),
        node(1,"point","shape/point",32,12),node(1,"diamond","shape/quad",52,12),
        blank,node(2,"layout","layout",70,50),node(0,"clip","clip",80,50),
        node(0,"left","src/images/body",0,30),node(0,"right","src/images/body",100,30),
        node(0,"top","src/images/body",30,0),node(0,"bottom","src/images/body",30,80),
        node(1,"shape left","shape/rect",0,60),node(1,"shape right","shape/rect",100,60)});
    auto model=std::make_shared<artc::EmoteModel>();std::string error;
    Check(model->Load(document,error),error.c_str());
    artc::EmoteScene scene;Check(scene.Load(model,error),error.c_str());
    artc::Compositor compositor;compositor.Init(100,80);
    const std::string owner="geometry",part=owner+".000001.000000";
    Check(scene.Render(compositor,owner,0,{},error),error.c_str());
    auto hit=[&](const char* label,double x,double y) {return scene.HitTest(compositor,owner,label,x,y);};
    Check(hit("triangle",11,11),"visible mesh interior participates in E-mote hit testing");
    Check(!hit("triangle",13,13),"mesh bounding rectangle is not triangle coverage");
    Check(hit("triangle",12,12) && hit("triangle",10,12) && hit("triangle",12,10),
          "mesh diagonal and outer edges are included");
    Check(hit("triangle",10,10) && hit("triangle",14,10) && hit("triangle",10,14),
          "mesh vertices are included even when adjacent triangles degenerate");
    Check(!hit("triangle",12.01,12.01) && !hit("triangle",9.99,10),
          "nearby exterior points do not become boundary hits");
    Check(hit("",11,11) && !hit("quad",11,11) && !hit("missing",11,11) &&
          !scene.HitTest(compositor,"other","triangle",11,11),
          "empty labels include art, exact labels and owner ids filter it");
    Check(hit("quad",22,12) && hit("quad",20,10) && hit("quad",24,14) && !hit("quad",24.01,14),
          "ordinary textured quad includes its edges and vertices");
    Check(hit("point",32,12) && hit("point",33,13) && !hit("point",33.01,12),
          "shape point retains the existing local one-unit tolerance");
    Check(hit("diamond",52,12) && hit("diamond",60,12) && hit("diamond",56,16) &&
          !hit("diamond",57,16),"shape quad retains the existing diamond including its boundary");
    Check(!hit("blank",40,40) && !hit("layout",70,50) && !hit("clip",80,50) && hit("child",40,40),
          "blank/layout/clip do not invent hit geometry or prevent real child geometry");
    Check(hit("left",0,30) && !hit("left",-0.01,30) && hit("right",99.99,30) && !hit("right",100,30) &&
          hit("top",30,0) && !hit("top",30,-0.01) && hit("bottom",30,79.99) && !hit("bottom",30,80),
          "textured coverage is clipped to the half-open stage target");
    Check(hit("shape left",0,60) && !hit("shape left",-0.01,60) &&
          hit("shape right",99.99,60) && !hit("shape right",100,60),
          "shape coverage is also clipped to the stage target");
    compositor.SetProps(owner,{{"visible","0"}});
    Check(!hit("triangle",11,11) && !hit("point",32,12),"hidden ancestors suppress art and shapes");
    compositor.SetProps(owner,{{"visible","1"},{"alpha","0"}});
    Check(!hit("triangle",11,11) && !hit("point",32,12),"transparent ancestors suppress art and shapes");
    compositor.SetProps(owner,{{"alpha","128"}});
    Check(hit("triangle",11,11) && hit("point",32,12),"positive effective opacity retains geometric hits");
    compositor.SetProps(owner+".000005",{{"visible","0"}});
    Check(!hit("child",40,40),"an intermediate hidden layout suppresses its drawable descendants");
    compositor.SetProps(owner+".000005",{{"visible","1"},{"alpha","0"}});
    Check(!hit("child",40,40),"an intermediate transparent layout suppresses its drawable descendants");
    compositor.SetProps(owner,{{"alpha","255"},{"left","70"},{"top","5"},{"rotate","90"},
        {"xscale","200"},{"yscale","50"},{"reversex","1"}});
    // Independent expected map of (x,y): (70 - .5*y, 5 - 2*x).
    Check(!hit("triangle",64.5,-17),"transformed offstage art cannot be hit");
    compositor.SetProps(owner,{{"top","65"}});
    Check(hit("triangle",64.5,43) && !hit("triangle",63.5,39) && hit("triangle",65,37),
          "mesh follows scaled, mirrored and rotated ancestor transforms including a vertex");
    Check(hit("quad",64,21) && !hit("quad",62.9,21) && hit("point",64,1),
          "quad and shape use the same effective ancestor transform");
    compositor.SetProps(owner,{{"rotate","0"},{"reversex","0"},{"left","0"},{"top","0"},
        {"xscale","0"},{"yscale","100"}});
    Check(!hit("triangle",0,11) && !hit("point",0,12),"singular transforms do not create hits");
    compositor.SetProps(owner,{{"xscale","100"}});
    compositor.SetProps(part,{{"visible","0"}});
    Check(!hit("triangle",11,11),"hidden art is not a geometric hit candidate");
    compositor.SetProps(part,{{"visible","1"},{"alpha","0"}});
    Check(!hit("triangle",11,11),"zero-opacity art is not a geometric hit candidate");
    compositor.SetProps(part,{{"alpha","255"}});
    // Mesh geometry can live outside the original image quad; use the exact
    // compositor triangle list, not texture dimensions or evaluator bounds.
    compositor.SetLayerMesh(part,{8,0,0,0, 12,0,1,0, 8,4,0,1});
    Check(hit("triangle",19,11) && !hit("triangle",11,11),"hit geometry follows the live rendered triangle list");
    compositor.SetLayerMesh(part,{8,4,0,1, 12,0,1,0, 8,0,0,0});
    Check(hit("triangle",19,11) && hit("triangle",20,12) && !hit("triangle",21,13),
          "clockwise and counterclockwise triangles have identical coverage");
    {
        size_t failures=0;
        auto expect=[&](const char* name,double x,double y,bool expected) {
            const bool actual=hit("triangle",x,y);
            if(actual!=expected) {
                std::cerr<<"triangle numeric "<<name<<": expected="<<expected<<" actual="<<actual<<'\n';
                ++failures;
            }
        };
        // Inverse translation keeps the million-unit local coordinates on the
        // stage; the identical unshifted triangle must have identical coverage.
        for(bool shifted:{false,true}) {
            const float offset=shifted?1000000.f:0.f;
            compositor.SetProps(part,{{"left",shifted?"-999990":"10"},{"top","10"}});
            compositor.SetLayerMesh(part,{offset,0,0,0, offset+4,0,1,0, offset,4,0,1});
            expect(shifted?"translated outside -0.5":"unshifted outside -0.5",9.5,11,false);
            expect("translated interior",11,11,true);
            expect("translated diagonal edge",12,12,true);
            expect("translated vertex",14,10,true);
        }
        compositor.SetProps(part,{{"left","10"},{"top","10"}});
        const float side=1e-8f;
        for(bool reversed:{false,true}) {
            compositor.SetLayerMesh(part,reversed?std::vector<float>{0,side,0,1, side,0,1,0, 0,0,0,0}:
                std::vector<float>{0,0,0,0, side,0,1,0, 0,side,0,1});
            expect("tiny distant exterior",10+5e-7,10+5e-7,false);
            expect("tiny just outside",10-double(side)*.01,10+double(side)*.25,false);
            expect("tiny interior",10+double(side)*.25,10+double(side)*.25,true);
            expect("tiny diagonal edge",10+double(side)*.5,10+double(side)*.5,true);
            expect("tiny vertex",10+double(side),10,true);
        }
        // Large edge length must not inflate a nonzero, very thin triangle.
        for(bool reversed:{false,true}) {
            compositor.SetLayerMesh(part,reversed?std::vector<float>{0,side,0,1, 4,0,1,0, 0,0,0,0}:
                std::vector<float>{0,0,0,0, 4,0,1,0, 0,side,0,1});
            expect("thin exterior",11,10+5e-7,false);
            expect("thin interior",11,10+double(side)*.25,true);
            expect("thin diagonal edge",12,10+double(side)*.5,true);
            expect("thin vertex",10,10+double(side),true);
        }
        compositor.SetProps(part,{{"left","10"},{"top","10"}});
        Check(failures==0,"triangle tolerance must be translation/scale aware without expanding thin geometry");
    }
    compositor.SetLayerMesh(part,{-12,0,0,0, -8,0,1,0, -12,4,0,1});
    Check(hit("triangle",0,11) && !hit("triangle",-0.01,11),"mesh coverage is clipped at the left stage boundary");
    compositor.SetLayerMesh(part,{88,0,0,0, 92,0,1,0, 88,4,0,1});
    Check(hit("triangle",99.99,11) && !hit("triangle",100,11),"mesh coverage is clipped at the right stage boundary");
    compositor.SetLayerMesh(part,{0,0,0,0, 4,0,1,0, 8,0,0,1});
    Check(!hit("triangle",12,10) && !hit("triangle",40,10),"zero-area triangles never cover the target");
    compositor.SetLayerMesh(part,{});
    compositor.SetProps(part,{{"clip","0,0,2,1"}});
    Check(hit("triangle",11,10.5) && !hit("triangle",13,10.5) && !hit("triangle",11,11.01),
          "known source UV crop uses the renderer's reduced quad dimensions");
    // Geometry is intentionally not pixel-alpha/mask/shader coverage: even a
    // fully transparent texture remains a geometric quad after upload.
    const uint8_t clear_pixels[4*4*4]={};
    Check(compositor.SetPixels(part,clear_pixels,4,4),"upload synthetic transparent hit-test texture");
    Check(hit("triangle",11,11),"geometric hit testing does not pretend to sample texture alpha");
    Check(!hit("",std::nan(""),12) && !hit("",12,std::nan("")) && !hit("",INFINITY,12),
          "non-finite query coordinates are rejected");
    compositor.DeleteLayer(part);
    Check(!hit("triangle",11,10.5),"deleted render parts cannot remain hit candidates");
    scene.Remove(compositor,owner);
    Check(!hit("",32,12),"removing the scene clears shape and drawable hit candidates");
}
static void SceneDiagnosticTests() {
    struct Environment {
        std::string name,value;
        bool present;
        explicit Environment(const char* key):name(key),present(std::getenv(key)!=nullptr) {
            if(present)value=std::getenv(key);
            unsetenv(key);
        }
        ~Environment(){if(present)setenv(name.c_str(),value.c_str(),1);else unsetenv(name.c_str());}
    } hide("ARTC_EMOTE_HIDE"),pixel("ARTC_EMOTE_PIXEL");
    // All art is generated by Scene(); duplicate icon metadata to exercise
    // exact tokens and optional source qualification without external assets.
    auto document=emote_fixture::Scene();
    auto& sources=document.root.object["source"].object;
    sources["images"].object["icon"].object["face_extra"]=sources["images"].object["icon"].object["face"];
    sources["other"]=sources["images"];
    auto picture=[](const char* label,const char* src) {
        return emote_fixture::Node(0,label,A({emote_fixture::Key(0,2,O({{"src",S(src)}}))}));
    };
    auto parent=picture("parent art","src/images/face");
    parent.object["children"]=A({picture("child art","src/images/wide")});
    auto& idle=document.root.object["object"].object["actor"].object["motion"].object["idle"];
    idle.object["layer"]=A({parent,picture("similar icon","src/images/face_extra"),
        picture("other source","src/other/face")});
    auto model=std::make_shared<artc::EmoteModel>();std::string error;
    Check(model->Load(document,error),error.c_str());
    artc::EmoteScene scene;Check(scene.Load(model,error),error.c_str());
    artc::Compositor compositor;
    const std::string owner="diagnostic",node=owner+".000001";
    const std::string parent_part=node+".000000",child_part=node+".000001.000000";
    const std::string similar_part=owner+".000002.000000",other_part=owner+".000003.000000";
    auto layer=[&](const std::string& id)->const artc::Layer& {
        for(const auto& l:compositor.Layers())if(l.id==id)return l;
        Check(false,("missing diagnostic layer "+id).c_str());std::abort();
    };
    auto render=[&](){Check(scene.Render(compositor,owner,0,{},error),error.c_str());};
    render();
    std::vector<std::pair<std::string,uint32_t>> installed;
    for(const auto& l:compositor.Layers())installed.emplace_back(l.id,l.texture);
    compositor.SetProps(parent_part,{{"draggable","1"}});
    compositor.SetProps(child_part,{{"draggable","1"}});
    auto persistent=[&]() {
        Check(compositor.Layers().size()==installed.size(),"diagnostic toggles preserve layer count");
        for(size_t i=0;i<installed.size();++i) {
            const auto& l=compositor.Layers()[i];
            Check(l.id==installed[i].first && l.texture==installed[i].second,
                  "diagnostic toggles retain each ID and texture");
        }
        Check(layer(parent_part).draggable && layer(child_part).draggable,
              "diagnostic toggles do not recreate existing art");
    };
    setenv("ARTC_EMOTE_HIDE"," ,face_extra_suffix; prefix_face; images/face_suffix;wrong/face,; ",1);
    render();
    Check(layer(parent_part).visible && layer(similar_part).visible && layer(other_part).visible,
          "similar icon and wrong-source tokens never match substrings");
    setenv("ARTC_EMOTE_HIDE"," ; missing, images/face ;,",1);
    render();persistent();
    Check(!layer(parent_part).visible && layer(node).visible && layer(child_part).visible &&
          compositor.EffectiveTransform(layer(child_part)).visible && layer(similar_part).visible &&
          layer(other_part).visible,"qualified token hides only the parent's own art, not its subtree or other sources");
    unsetenv("ARTC_EMOTE_HIDE");render();persistent();
    Check(layer(parent_part).visible && layer(child_part).visible,"clearing hide restores the same parent part");
    setenv("ARTC_EMOTE_HIDE","wide",1);render();persistent();
    Check(layer(parent_part).visible && !layer(child_part).visible && layer(node).visible,
          "child art can be hidden independently of parent art");
    setenv("ARTC_EMOTE_HIDE","face;wide",1);render();persistent();
    Check(!layer(parent_part).visible && !layer(child_part).visible && !layer(other_part).visible &&
          layer(similar_part).visible,"semicolon tokens match whole icons across sources");
    setenv("ARTC_EMOTE_HIDE","images/face,wide",1);render();persistent();
    Check(!layer(parent_part).visible && !layer(child_part).visible && layer(other_part).visible,
          "comma tokens support qualified and unqualified icons together");
    unsetenv("ARTC_EMOTE_HIDE");render();persistent();
    Check(layer(parent_part).visible && layer(child_part).visible && layer(other_part).visible,
          "unsetting diagnostics restores all persistent art");

    std::vector<std::string> reports;
    artc::SetLogSink([&](int,const std::string& text) {
        if(text.rfind("emote-pixel ",0)==0)reports.push_back(text);
    });
    struct ClearSink {~ClearSink(){artc::SetLogSink({});}} clear_sink;
    setenv("ARTC_EMOTE_PIXEL","0,0",1);
    auto probe=[&]()->std::string {
        reports.clear();render();Check(reports.size()==1,"opt-in probe reports one source-candidate query");
        const auto& report=reports.front();
        Check(report.find("mode=source-candidates")!=std::string::npos &&
              report.find("final-pixel=unresolved")!=std::string::npos && report.find("top=")==std::string::npos,
              "probe never claims final framebuffer ownership");
        return report;
    };
    auto report=probe();
    Check(report.find("candidate="+parent_part)!=std::string::npos &&
          report.find("candidate="+child_part)!=std::string::npos,
          "visible parent and child are independent source candidates");
    size_t previous=0;
    for(const auto& part:compositor.HitLayers(0,0)) {
        const size_t at=report.find("candidate="+part+" ");
        if(at==std::string::npos)continue;
        Check(at>previous,"source candidates follow the compositor production query order");previous=at;
    }
    setenv("ARTC_EMOTE_HIDE","images/face",1);report=probe();
    Check(report.find("candidate="+parent_part+" ")==std::string::npos &&
          report.find("candidate="+child_part+" ")!=std::string::npos,
          "probe excludes hidden parent art without excluding visible children");
    unsetenv("ARTC_EMOTE_HIDE");
    compositor.SetProps(owner,{{"visible","0"}});report=probe();
    Check(report.find("candidates=0")!=std::string::npos && report.find("candidate=")==std::string::npos,
          "probe respects effective ancestor visibility, not just the part's own flag");
    compositor.SetProps(owner,{{"visible","1"}});
    compositor.SetProps(owner,{{"alpha","0"}});report=probe();
    Check(report.find("candidates=0")!=std::string::npos,"probe excludes zero effective opacity");
    compositor.SetProps(owner,{{"alpha","255"}});
    // face is 2x2 with origin (1,1): -1.5 would truncate to texel zero
    // if the diagnostic failed to check source-space bounds first.
    setenv("ARTC_EMOTE_PIXEL","-1.5,0",1);report=probe();
    Check(report.find("candidate="+parent_part+" ")==std::string::npos,
          "negative fractional source coordinates do not become texel zero");
    unsetenv("ARTC_EMOTE_PIXEL");reports.clear();render();
    Check(reports.empty(),"source-candidate logging is disabled without the explicit environment option");

    // A second synthetic scene exercises unsupported compositing. A translated
    // mesh can cover points outside the rectangle queried by HitLayers.
    auto& authored_parent=idle.object["layer"].array[0];
    auto& content=authored_parent.object["frameList"].array[0].object["content"];
    content.object["bm"]=N(3);
    content.object["mesh"]=O({{"bp",A({N(2),N(0),N(3),N(0),N(2),N(1),N(3),N(1)})}});
    auto mask_group=emote_fixture::Node(12,"mask group",A({emote_fixture::Key(0,2,O({{"src",S("layout")}}))}),
        A({authored_parent}));
    mask_group.object["stencilType"]=N(4);
    mask_group.object["stencilCompositeMaskLayerList"]=A({S("missing mask")});
    authored_parent=std::move(mask_group);
    auto unresolved_model=std::make_shared<artc::EmoteModel>();
    Check(unresolved_model->Load(document,error),error.c_str());
    Check(scene.Load(unresolved_model,error),error.c_str());
    setenv("ARTC_EMOTE_PIXEL","0,0",1);report=probe();
    Check(report.find("unresolved=mesh,mask,blend,effect")!=std::string::npos,
          "mesh, authored masks and blend remain explicitly unresolved in source sampling");
    setenv("ARTC_EMOTE_PIXEL","4,0",1);report=probe();
    Check(report.find("candidate="+child_part+" ")!=std::string::npos &&
          report.find("order=unresolved(outside-affine-query)")!=std::string::npos,
          "mesh outside affine query remains an unranked candidate rather than disappearing");
    compositor.SetProps(owner,{{"visible","0"}});report=probe();
    Check(report.find("candidates=0")!=std::string::npos,"hidden ancestors also exclude unresolved mesh candidates");
}
// E9/WP10: synthetic evidence for the finite shape-sync subset, not the
// unimplemented low sync channels, cc, meshCombine, curves or particles.
static void SceneMeshSyncTests() {
    int failures=0;
    auto expect=[&](bool ok,const char* why) {
        if(!ok){++failures;std::cerr<<"meshSync: "<<why<<'\n';}
    };
    auto near=[](double a,double b){return std::abs(a-b)<1e-5;};
    auto grid=[](double ax,double bx,double ay=1,double by=0) {
        return O({{"bp",A({N(bx),N(by),N(ax+bx),N(by),
                            N(bx),N(ay+by),N(ax+bx),N(ay+by)})}});
    };
    auto node=[](const char* label,artc::PsbValue content,artc::PsbValue children=A({})) {
        return emote_fixture::Node(0,label,A({emote_fixture::Key(0,2,std::move(content))}),std::move(children));
    };
    auto sync=[](artc::PsbValue n) {
        n.object["meshTransform"]=N(1);n.object["meshSyncChildMask"]=N(8);return n;
    };
    auto document=[](artc::PsbValue nodes) {
        auto d=emote_fixture::Scene();
        d.root.object["object"].object["actor"].object["motion"].object["idle"].object["layer"]=std::move(nodes);
        return d;
    };
    auto inspect=[&](artc::PsbDocument d,auto assertions) {
        std::string error;auto model=std::make_shared<artc::EmoteModel>();
        Check(model->Load(std::move(d),error),error.c_str());
        artc::EmoteScene scene;Check(scene.Load(model,error),error.c_str());
        std::vector<artc::EmoteSceneLayer> layers;
        Check(scene.Evaluate(0,{},layers,error),error.c_str());
        artc::Compositor compositor;
        Check(scene.Render(compositor,"sync",0,{},error),error.c_str());
        assertions(layers,compositor);
    };
    auto find=[](const std::vector<artc::EmoteSceneLayer>& layers,const char* label)->const artc::EmoteSceneLayer* {
        for(const auto& l:layers)if(l.label==label)return &l;
        return nullptr;
    };
    auto vertex=[&](const std::vector<artc::EmoteSceneLayer>& layers,const artc::Compositor& c,
                    const char* label,double u,double v,double x,double y,const char* why) {
        const auto* l=find(layers,label);bool matched=false;
        if(l)for(const auto& part:c.Layers())if(part.id=="sync"+l->key+".000000") {
            for(size_t i=0;i+3<part.mesh.size();i+=4)
                if(near(part.mesh[i+2],u)&&near(part.mesh[i+3],v)) {
                    matched=true;expect(near(part.mesh[i],x)&&near(part.mesh[i+1],y),why);break;
                }
        }
        expect(matched,why);
    };
    // Both blank spellings preserve descriptor extent AND content ox/oy.
    for(bool inline_extent:{false,true}) {
        auto content=O({{"src",S(inline_extent?"blank:10:20:3:4":"blank")},
            {"icon",S("10:20:3:4")},{"ox",N(2)},{"oy",N(1)},{"mesh",grid(1,.1,1,.2)}});
        auto parent=sync(node("blank",content,A({node("leaf",O({{"src",S("src/images/face")}}))})));
        inspect(document(A({parent})),[&](const auto& layers,const auto& c) {
            const auto* leaf=find(layers,"leaf");
            expect(leaf && leaf->deformers.size()==1,"blank creates one descendant scope");
            if(leaf && leaf->deformers.size()==1) {
                const auto& d=leaf->deformers[0];
                expect(d.width==10 && d.height==20 && d.origin_x==3 && d.origin_y==4 &&
                       d.off_x==2 && d.off_y==1,"blank domain keeps extent, descriptor origin and content offset separate");
            }
            vertex(layers,c,"leaf",0,0,1,4,"blank domain shifts face vertex by (1,4) pixels");
        });
    }
    // Icon extents are resolved from the atlas, with both legacy/split sources.
    for(bool split:{false,true}) {
        auto parent=sync(node("icon scope",O({{"src",S(split?"images":"src/images/body")},
            {"icon",S("body")},{"mesh",grid(1,.25)}}),
            A({node("leaf",O({{"src",S("src/images/face")}}))})));
        inspect(document(A({parent,node("sibling",O({{"src",S("src/images/face")},{"coord",A({N(7),N(9)})}}))})),
            [&](const auto& layers,const auto& c) {
                const auto* own=find(layers,"icon scope");const auto* leaf=find(layers,"leaf");
                const auto* sibling=find(layers,"sibling");
                expect(own && own->source=="images" && own->icon=="body" && own->deformers.empty(),
                       "icon sync node draws its own bp once, not through its own scope");
                expect(leaf && leaf->deformers.size()==1 && leaf->deformers[0].source=="images" &&
                       leaf->deformers[0].icon=="body","icon extent scope reaches descendants");
                expect(sibling && sibling->deformers.empty() && sibling->abs_tx==7 && sibling->abs_ty==9,
                       "icon scope restores affine/scopes before sibling");
                vertex(layers,c,"icon scope",0,0,1,0,"icon sync own bp translates 4px icon by 1px exactly once");
                vertex(layers,c,"leaf",0,0,1,0,"atlas width 4 converts .25 shift into 1px");
            });
    }
    auto icon_scope=sync(node("inner icon",O({{"src",S("src/images/body")},{"mesh",grid(1,.25)}}),
        A({node("icon child",O({{"src",S("src/images/face")}}))})));
    auto blank_scope=sync(node("outer blank",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(1,.1)}}),A({icon_scope})));
    inspect(document(A({blank_scope})),[&](const auto& layers,const auto& c) {
        const auto* own=find(layers,"inner icon");const auto* child=find(layers,"icon child");
        expect(own && own->deformers.size()==1 && child && child->deformers.size()==2,
               "nested icon scope excludes itself but retains its ancestor");
        vertex(layers,c,"inner icon",0,0,2,0,"sync node composes own 1px and ancestor 1px exactly once each");
        vertex(layers,c,"icon child",0,0,2,0,"descendant composes icon and blank extents once each");
    });
    // A layout patch has no geometric extent: it is not a scope, but may not
    // skip its descendants or leak its transform/mask context to siblings.
    auto no_extent=sync(node("後髪 missing extent",O({{"src",S("layout")},
        {"coord",A({N(10),N(20)})},{"mesh",grid(1,.1)}}),
        A({node("child",O({{"src",S("src/images/face")}}))})));
    no_extent.object["stencilType"]=N(4);
    no_extent.object["stencilCompositeMaskLayerList"]=A({S("absent")});
    inspect(document(A({no_extent,node("追加パーツ sibling",O({{"src",S("src/images/face")}}))})),
        [&](const auto& layers,const auto&) {
            const auto* child=find(layers,"child");const auto* sibling=find(layers,"追加パーツ sibling");
            expect(child && child->abs_tx==10 && child->abs_ty==20 && child->deformers.empty(),
                   "missing extent does not skip children");
            expect(sibling && sibling->abs_tx==0 && sibling->abs_ty==0 && sibling->masks.empty(),
                   "missing extent restores transform and masks context");
        });
    // Noncommutative nested patches: inner x += 1 then outer x *= 2. The
    // blank domain [-5,5] makes the face's first local x=-1 become x=5,
    // hence icon-local x=6 (outer-first would give x=5 instead).
    auto inner=sync(node("inner",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(1,.1)}}),
        A({node("leaf",O({{"src",S("src/images/face")}}))})));
    auto outer=sync(node("outer",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(2,0)}}),A({inner})));
    inspect(document(A({outer})),[&](const auto& layers,const auto& c) {
        const auto* leaf=find(layers,"leaf");
        expect(leaf && leaf->deformers.size()==2 && near(leaf->deformers[0].points[0],.1) &&
               near(leaf->deformers[1].points[2],2),"nested patches are recorded nearest-first");
        vertex(layers,c,"leaf",0,0,6,0,"nearest-first patch composition is numerically noncommutative");
    });
    // Shape inheritance is suspended for an entire branch, then restored to a
    // sibling. A descendant re-enabling the bit cannot recover a removed scope.
    auto suspended=node("suspended",O({{"src",S("layout")}}),
        A({node("blocked",O({{"src",S("src/images/face")}}))}));
    suspended.object["inheritMask"]=N(0x7fc);
    auto scope=sync(node("scope",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(1,.1)}}),
        A({suspended,node("restored",O({{"src",S("src/images/face")}}))})));
    inspect(document(A({scope})),[&](const auto& layers,const auto& c) {
        const auto* blocked=find(layers,"blocked");const auto* restored=find(layers,"restored");
        expect(blocked && blocked->deformers.empty(),"shape bit suspension affects descendants");
        expect(restored && restored->deformers.size()==1 && near(restored->deformers[0].points[0],.1),
               "shape scopes resume for sibling after suspended branch");
        vertex(layers,c,"restored",0,0,1,0,"restored sibling receives 1px shift");
    });
    // Shape inheritance is node-level even without a sampled frame. Children
    // still evaluate before a future first key, and siblings recover the scope.
    for(bool future_first:{false,true}) {
        auto blocked=node("unsampled child",O({{"src",S("src/images/face")},{"mesh",grid(1,0)}}));
        auto branch=node("unsampled branch",O({{"src",S("layout")}}),A({blocked}));
        branch.object["inheritMask"]=N(0x7fc);
        branch.object["frameList"]=future_first
            ?A({emote_fixture::Key(5,2,O({{"src",S("layout")}}))}):A({});
        auto ancestor=sync(node("unsampled ancestor",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(1,.1)}}),
            A({branch,node("unsampled sibling",O({{"src",S("src/images/face")},{"mesh",grid(1,0)}}))})));
        ancestor.object["stencilType"]=N(4);
        ancestor.object["stencilCompositeMaskLayerList"]=A({S("ancestor mask")});
        inspect(document(A({ancestor})),[&](const auto& layers,const auto& c) {
            const auto* child=find(layers,"unsampled child");
            const auto* sibling=find(layers,"unsampled sibling");
            expect(child && child->source=="images" && child->icon=="face",
                   "empty/future first frame keeps evaluating children");
            expect(child && child->deformers.empty(),
                   "empty/future first frame suspends ancestor shape scope");
            expect(child && child->masks==std::vector<std::string>{"ancestor mask"} && sibling &&
                   sibling->masks==child->masks,"shape suspension leaves ancestor stencil masks intact");
            expect(sibling && sibling->deformers.size()==1 && near(sibling->deformers[0].points[0],.1),
                   "empty/future first frame restores ancestor scope for sibling");
            vertex(layers,c,"unsampled child",0,0,0,0,"unsampled branch child remains numerically undeformed");
            vertex(layers,c,"unsampled sibling",0,0,1,0,"unsampled branch sibling recovers 1px deformation");
        });
    }
    // Own bp must be applied before ancestor deformation, not discarded.
    scope=sync(node("scope",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(2,0)}}),
        A({node("own patch",O({{"src",S("src/images/face")},{"mesh",grid(1,.25)}}))})));
    inspect(document(A({scope})),[&](const auto& layers,const auto& c) {
        vertex(layers,c,"own patch",0,0,5,0,"own .5px shift composes with ancestor x2 (not replaced)");
        vertex(layers,c,"own patch",1,1,9,2,"own bp preserves texture UVs through ancestor deformation");
    });
    // Scope/part coordinates differ: parent T(10,20) S(2,3), child T(1,0)
    // S(.5,1). Thus part->domain is (.5*x+1,y), domain->part (2*x-2,y).
    // A domain shift of 1px becomes 2px in part-local coordinates.
    scope=sync(node("scope",O({{"src",S("blank:10:10:5:5")},{"coord",A({N(10),N(20)})},
        {"zx",N(2)},{"zy",N(3)},{"mesh",grid(1,.1)}}),
        A({node("mapped",O({{"src",S("src/images/face")},{"coord",A({N(1),N(0)})},{"zx",N(.5)}}))})));
    inspect(document(A({scope})),[&](const auto& layers,const auto& c) {
        const auto* mapped=find(layers,"mapped");
        expect(mapped && mapped->abs_tx==12 && mapped->abs_ty==20 && mapped->abs_m11==1 &&
               mapped->abs_m22==3,"transformed descendant absolute matrix");
        expect(mapped && mapped->deformers.size()==1 && near(mapped->deformers[0].m11,2) &&
               near(mapped->deformers[0].m22,1) && near(mapped->deformers[0].tx,-2) &&
               near(mapped->deformers[0].ty,0),"deformer exposes the expected domain-to-part affine");
        vertex(layers,c,"mapped",0,0,2,0,"deformer maps domain to part with inverse scale and translation");
    });
    // Child linear S(.5,1)*R(90): inverse maps a +1 domain-x
    // displacement into -2 part-y, testing off-diagonal matrix terms too.
    scope=sync(node("rotated scope",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(1,.1)}}),
        A({node("rotated",O({{"src",S("src/images/face")},{"angle",N(90)},{"zx",N(.5)}}))})));
    inspect(document(A({scope})),[&](const auto& layers,const auto& c) {
        vertex(layers,c,"rotated",0,0,0,-2,"domain to part mapping includes off-diagonal inverse terms");
    });
    // A non-affine 4x4 Bezier patch, not only the 2x2 bilinear cases above.
    scope=sync(node("45 scope",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(1,.1)}}),
        A({node("45 leaf",O({{"src",S("src/images/face")},{"angle",N(45)},{"zx",N(.5)}}))})));
    inspect(document(A({scope})),[&](const auto& layers,const auto& c) {
        vertex(layers,c,"45 leaf",0,0,std::sqrt(2.0),-std::sqrt(2.0),
               "45-degree deformer produces inverse-affine local displacement");
        const auto* leaf=find(layers,"45 leaf");
        if(leaf)for(const auto& part:c.Layers())if(part.id=="sync"+leaf->key+".000000" && !part.mesh.empty()) {
            const auto m=c.EffectiveTransform(part);
            const auto p=m.Point(part.mesh[0],part.mesh[1]);const auto base=m.Point(0,0);
            expect(near(p.first-base.first,1) && near(p.second-base.second,0),
                   "45-degree nonuniform deformation is exactly (1,0) in stage space, not (1.3,.6)");
        }
    });
    scope=sync(node("45 own scope",O({{"src",S("blank:10:10:5:5")},{"mesh",grid(1,.1)}}),
        A({node("45 own",O({{"src",S("src/images/face")},{"angle",N(45)},{"zx",N(.5)},
            {"mesh",grid(1,.25)}}))})));
    inspect(document(A({scope})),[&](const auto& layers,const auto& c) {
        const auto* leaf=find(layers,"45 own");
        if(leaf)for(const auto& part:c.Layers())if(part.id=="sync"+leaf->key+".000000") {
            expect(part.mesh.size()==leaf->mesh.size(),"own bp triangle list is retained through 45-degree scope");
            const auto m=c.EffectiveTransform(part);
            for(size_t i=0;i+3<part.mesh.size();i+=4) {
                expect(part.mesh[i+2]==leaf->mesh[i+2] && part.mesh[i+3]==leaf->mesh[i+3],
                       "own bp plus ancestor affine never changes texture UVs");
                const auto p=m.Point(part.mesh[i],part.mesh[i+1]);
                const auto own=m.Point(leaf->mesh[i]*2,leaf->mesh[i+1]*2);
                expect(near(p.first-own.first,1) && near(p.second-own.second,0),
                       "own bp then ancestor patch yields exactly (1,0) stage displacement at every vertex");
            }
        }
    });
    // At domain (.25,.25), B1=3*(.75)^2*.25=27/64. Perturbing P11.x
    // by .25 adds 4*.25*(27/64)^2 = .177978515625 pixels.
    artc::PsbValue bezier;bezier.type=artc::PsbValue::Array;
    for(int y=0;y<4;++y)for(int x=0;x<4;++x) {
        bezier.array.push_back(N(double(x)/3+(x==1 && y==1?.25:0)));
        bezier.array.push_back(N(double(y)/3));
    }
    scope=sync(node("bezier scope",O({{"src",S("blank:4:4:2:2")},{"mesh",O({{"bp",bezier}})}}),
        A({node("bezier leaf",O({{"src",S("src/images/face")}}))})));
    inspect(document(A({scope})),[&](const auto& layers,const auto& c) {
        vertex(layers,c,"bezier leaf",0,0,.177978515625,0,"4x4 Bezier propagation matches independent Bernstein value");
    });
    Check(failures==0,"finite meshSync numerical regressions");
}

// The affine API is shared by GL and no-GL compositors.
static void CompositorAffineApiTests() {
    artc::Compositor c;
    c.SetProps("owner",{{"left","10"},{"top","20"},{"xscale","200"},{"yscale","300"}});
    c.SetProps("owner.part",{{"left","1"},{"top","2"},{"rotate","90"},
        {"anchorx","2"},{"anchory","3"},{"w","4"},{"h","5"},{"draggable","1"}});
    auto part=[&]()->const artc::Layer& {
        for(const auto& l:c.Layers())if(l.id=="owner.part")return l;
        std::abort();
    };
    auto near=[](double a,double b){return std::abs(a-b)<1e-5;};
    Check(!part().affine,"ordinary layers opt out of affine by default");
    auto rev=c.Revision();
    Check(!c.SetLayerAffine("absent",1,0,0,1,0,0) && !c.ClearLayerAffine("absent") &&
          c.Revision()==rev,"absent affine APIs do not create or mutate layers");
    Check(c.SetLayerAffine("owner.part",1,2,3,4,5,6) && c.Revision()==rev+1,
          "affine setter records geometry and bumps revision once");
    rev=c.Revision();
    Check(c.SetLayerAffine("owner.part",1,2,3,4,5,6) && c.Revision()==rev,
          "identical affine write does not dirty the retained frame");
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(int i=0;i<6;++i)for(float invalid:{nan,inf,-inf}) {
        float values[6]={1,2,3,4,5,6};values[i]=invalid;
        Check(!c.SetLayerAffine("owner.part",values[0],values[1],values[2],values[3],values[4],values[5]) &&
              c.Revision()==rev && part().x==5 && part().affine_a==1,
              "each nonfinite affine input is rejected atomically");
    }
    auto m=c.EffectiveTransform(part());
    // T(10,20) S(2,3) [T(5,6) T(2,3) A T(-2,-3)].
    Check(near(m.a,2)&&near(m.b,6)&&near(m.c,6)&&near(m.d,12)&&near(m.tx,2)&&near(m.ty,-1),
          "affine linear obeys anchors and full live ancestor composition");
    auto content=part();content.content_x=1;content.content_y=2;
    auto cm=c.EffectiveTransform(content);
    Check(near(cm.tx,16)&&near(cm.ty,29),"content offset is transformed once after anchored affine");
    const auto info=c.GetLayerInfo("owner.part");
    Check(info.left==5 && info.top==6 && info.width==4 && info.height==5 && info.draggable,
          "get_layer_info remains stored translation and size, not affine world AABB");
    rev=c.Revision();c.SetProps("owner",{{"rotate","90"}});
    m=c.EffectiveTransform(part());
    Check(c.Revision()>rev && near(m.a,-6) && near(m.b,2) && near(m.c,-12) && near(m.d,6),
          "ancestor changes after affine installation remain live");
    c.SetProps("owner",{{"rotate","0"}});
    c.SetProps("owner.part",{{"left","7"},{"top","8"},{"alpha","128"},{"grayscale","1"}});
    Check(part().affine && part().x==7 && part().y==8,"position alpha and effect writes retain affine");
    float dx=0,dy=0;
    Check(c.ParentDelta("owner.part",4,9,&dx,&dy) && dx==2 && dy==3,
          "drag delta remains in live parent's coordinate space");
    c.AddTween("owner.part",{{"param","left"},{"from","7"},{"to","9"},{"time","100"}},0);
    c.Update(50);
    Check(part().affine && part().x==8,"position tween retains affine and sole translation storage");
    rev=c.Revision();
    Check(c.ClearLayerAffine("owner.part") && c.Revision()==rev+1 && !part().affine &&
          part().x==8 && part().rotate==90,"clear restores stored legacy TRS retaining translated position");
    m=c.EffectiveTransform(part());
    Check(m.a==0 && m.b==3 && m.c==-2 && m.d==0 && m.tx==36 && m.ty==47,
          "clear resumes anchored legacy rotation using the current stored translation");
    rev=c.Revision();Check(c.ClearLayerAffine("owner.part") && c.Revision()==rev,"clear legacy is a no-op");
    for(const char* channel:{"rotate","xscale","yscale","zoom","reversex","reversey"}) {
        Check(c.SetLayerAffine("owner.part",1,2,3,4,5,6),"re-enable affine");
        c.SetProps("owner.part",{{channel,"0"}});
        Check(!part().affine,"explicit legacy linear write clears affine");
    }
    for(const char* channel:{"rotate","xscale","yscale","zoom"}) {
        Check(c.SetLayerAffine("owner.part",1,2,3,4,5,6),"re-enable affine for tween");
        c.AddTween("owner.part",{{"param",channel},{"from","0"},{"to","100"},{"time","100"}},200);
        c.Update(250);Check(!part().affine,"linear tween ApplyParam clears affine");
        c.DeleteTweens("owner.part");
    }
    Check(c.SetLayerAffine("owner.part",0,0,2,3,5,6),"singular first column with live second column is valid");
    m=c.EffectiveTransform(part());
    Check(m.a==0 && m.b==0 && m.c==4 && m.d==9,"singular affine does not lose its second column");
    Check(c.RenameLayer("owner","renamed"),"rename affine subtree");
    const auto& renamed=c.Layers().back();
    Check(renamed.id=="renamed.part" && renamed.affine && renamed.affine_c==2,"rename preserves affine state");
    c.DeleteLayer("renamed");
    Check(c.Layers().empty() && !c.ClearLayerAffine("renamed.part"),"subtree deletion drops affine state");
    c.SetProps("fresh",{});Check(c.SetLayerAffine("fresh",0,0,0,0,1,2),"zero affine is accepted");
    c.ReleaseGl();c.SetProps("fresh",{});
    Check(!c.Layers().front().affine,"GL release and recreation reset affine state");c.ReleaseGl();
}

static void SceneAffineGeometryTests() {
    using emote_fixture::Node;
    using emote_fixture::Key;
    int failures=0;
    auto expect=[&](bool ok,const char* why) {
        if(!ok){++failures;std::cerr<<"affine: "<<why<<'\n';}
    };
    auto near=[](double a,double b){return std::abs(a-b)<1e-5;};
    auto node=[](const char* label,const char* src,double x,double y,artc::PsbValue children=A({})) {
        return Node(0,label,A({Key(0,2,O({{"src",S(src)},{"coord",A({N(x),N(y)})}}))}),std::move(children));
    };
    auto document=emote_fixture::Scene();
    auto quad=node("sheared quad","src/images/face",20,20);
    auto& qc=quad.object["frameList"].array[0].object["content"];
    qc.object["angle"]=N(45);qc.object["zx"]=N(.5);
    auto shape=node("shape parent","shape/rect",20,20,A({
        node("shape child","src/images/face",-1,-1),
        node("nested shape","shape/circle",-2,-2,A({node("deep child","src/images/face",-1,-1)}))}));
    document.root.object["object"].object["actor"].object["motion"].object["idle"].object["layer"]=A({quad,shape});
    std::string error;auto model=std::make_shared<artc::EmoteModel>();
    Check(model->Load(document,error),error.c_str());artc::EmoteScene scene;
    Check(scene.Load(model,error),error.c_str());artc::Compositor c;c.Init(100,100);
    Check(scene.Render(c,"aff",0,{},error),error.c_str());
    auto layer=[&](const std::string& id)->const artc::Layer* {
        for(const auto& l:c.Layers())if(l.id==id)return &l;return nullptr;
    };
    const auto* q=layer("aff.000001.000000");
    expect(q!=nullptr,"affine quad exists");
    if(q) {
        const auto m=c.EffectiveTransform(*q);const double r=std::sqrt(.5);
        // Independent S(.5,1) R45 matrix, including the icon's (-1,-1) offset.
        for(const auto p:std::array<std::pair<double,double>,4>{{{0,0},{2,0},{0,2},{2,2}}}) {
            const auto world=m.Point(float(p.first),float(p.second));
            expect(near(world.first,20+.5*r*(p.first-p.second)) &&
                   near(world.second,20+r*(p.first+p.second-2)),"nonuniform 45-degree quad preserves all four corners");
        }
    }
    const auto* child=layer("aff.000002.000001.000000");
    expect(child && near(c.EffectiveTransform(*child).tx,18) && near(c.EffectiveTransform(*child).ty,18),
           "shape parent 20 plus child own absolute 19 is not accumulated to 39");
    const auto* deep=layer("aff.000002.000002.000001.000000");
    expect(deep && near(c.EffectiveTransform(*deep).tx,16) && near(c.EffectiveTransform(*deep).ty,16),
           "two shape levels do not reapply absolute transforms");
    expect(scene.HitTest(c,"aff","shape parent",20,20) && scene.HitTest(c,"aff","nested shape",18,18),
           "shape geometry stays centered at its absolute node origin");
    const auto* shape_leaf=layer("aff.000002.000000");
    expect(shape_leaf && !shape_leaf->texture,"shape is an independent textureless geometry leaf");
    c.SetProps("aff",{{"left","3"},{"top","4"},{"rotate","90"},{"alpha","128"}});
    // Render must not bake this owner: subsequent edits still move every leaf.
    Check(scene.Render(c,"aff",0,{},error),error.c_str());
    c.SetProps("aff",{{"left","50"},{"top","5"}});
    child=layer("aff.000002.000001.000000");
    expect(child && near(c.EffectiveTransform(*child).tx,32) && near(c.EffectiveTransform(*child).ty,23),
           "owner transform remains live after render rather than being baked");
    expect(scene.HitTest(c,"aff","shape parent",30,25),"shape leaf follows live external parent rotation");
    c.SetProps("aff",{{"visible","0"}});
    expect(!scene.HitTest(c,"aff","shape parent",30,25),"hidden live parent suppresses shape leaf");
    c.SetProps("aff",{{"visible","1"},{"alpha","0"}});
    expect(!scene.HitTest(c,"aff","shape parent",30,25),"zero-alpha live parent suppresses shape leaf");
    c.ReleaseGl();
    {
        auto d=emote_fixture::Scene();auto s=node("sheared shape","shape/rect",20,20);
        auto& content=s.object["frameList"].array[0].object["content"];
        content.object["zx"]=N(.5);content.object["angle"]=N(45);
        d.root.object["object"].object["actor"].object["motion"].object["idle"].object["layer"]=A({s});
        auto mod=std::make_shared<artc::EmoteModel>();Check(mod->Load(d,error),error.c_str());
        Check(scene.Load(mod,error),error.c_str());Check(scene.Render(c,"aff",0,{},error),error.c_str());
        const double r=std::sqrt(.5);
        expect(scene.HitTest(c,"aff","sheared shape",20+3.95*r,20+7.9*r) &&
               !scene.HitTest(c,"aff","sheared shape",20+5*r,20+10*r),
               "shape boundary inverts full nonuniform 45-degree affine, not reconstructed TRS");
        c.ReleaseGl();
    }
    // Negative determinant and singular first column must reach the compositor
    // exactly, without atan2/hypot decomposition or a fallback sy=1.
    for(double scale:{-0.5,0.0})for(double angle:{0.0,45.0}) {
        auto d=emote_fixture::Scene();
        auto n=node("matrix","src/images/face",20,20);
        auto& content=n.object["frameList"].array[0].object["content"];
        content.object["zx"]=N(scale);content.object["angle"]=N(angle);
        d.root.object["object"].object["actor"].object["motion"].object["idle"].object["layer"]=A({n});
        auto mod=std::make_shared<artc::EmoteModel>();Check(mod->Load(d,error),error.c_str());
        Check(scene.Load(mod,error),error.c_str());Check(scene.Render(c,"aff",0,{},error),error.c_str());
        q=layer("aff.000001.000000");const auto m=c.EffectiveTransform(*q);
        const double r=angle==0?1:std::sqrt(.5),s=angle==0?0:std::sqrt(.5);
        expect(near(m.a,scale*r)&&near(m.b,s)&&near(m.c,-scale*s)&&near(m.d,r),
               "negative and zero-scale affine columns are retained exactly");
        c.ReleaseGl();
    }
    // One stable node position switches shape -> picture -> shape -> layout;
    // its ordinary and nested-motion children must never inherit its geometry.
    auto d=emote_fixture::Scene();
    auto switching=node("switch","shape/rect",20,20,A({
        node("ordinary","src/images/face",-1,-1),node("motion entry","motion/actor/sub",-2,-2)}));
    switching.object["type"]=N(1);
    switching.object["frameList"]=A({
        Key(0,2,O({{"src",S("shape/rect")},{"coord",A({N(20),N(20)})},{"opa",N(128)}})),
        Key(1,2,O({{"src",S("src/images/body")},{"coord",A({N(20),N(20)})},{"opa",N(128)}})),
        Key(2,2,O({{"src",S("shape/circle")},{"coord",A({N(20),N(20)})},{"opa",N(128)}})),
        Key(3,2,O({{"src",S("layout")},{"coord",A({N(20),N(20)})},{"opa",N(128)}}))});
    auto& motions=d.root.object["object"].object["actor"].object["motion"];
    motions.object["idle"].object["layer"]=A({switching});
    motions.object["sub"]=O({{"lastTime",N(10)},{"loopTime",N(-1)},
        {"layer",A({node("nested art","src/images/face",-1,-1)})}});
    model=std::make_shared<artc::EmoteModel>();Check(model->Load(d,error),error.c_str());
    Check(scene.Load(model,error),error.c_str());
    for(int frame=0;frame<4;++frame) {
        Check(scene.Render(c,"aff",frame,{},error),error.c_str());
        const auto* holder=layer("aff.000001");q=layer("aff.000001.000000");
        expect(holder && holder->x==0 && holder->y==0 && holder->alpha==1 && !holder->affine,
               "every structure holder stays identity and opaque");
        expect((frame==3 && !q) || (frame!=3 && q && bool(q->texture)==(frame==1)),
               "shape/picture/layout switch removes stale content but retains the correct leaf kind");
        expect(scene.HitTest(c,"aff","switch",20,20)==(frame!=3),
               "shape/picture/layout switch replaces registered hit geometry");
        child=layer("aff.000001.000001.000000");
        expect(child && near(c.EffectiveTransform(*child).tx,18) && near(c.EffectiveTransform(*child).alpha,128.0/255),
               "shape opacity and absolute location are not doubled on ordinary child");
        const auto* nested=layer("aff.000001.000002.000000.000001.000000");
        expect(nested && near(c.EffectiveTransform(*nested).tx,16) && near(c.EffectiveTransform(*nested).alpha,128.0/255),
               "shape subtree nested motion inherits model context exactly once");
    }
    scene.Remove(c,"aff");
    expect(c.Layers().empty() && !scene.HitTest(c,"aff","",20,20),"remove clears geometry leaves and registered hits");
    c.ReleaseGl();
    Check(failures==0,"general affine and shape subtree regressions");
}

static void SceneZChannelTests() {
    using emote_fixture::Node;
    using emote_fixture::Key;
    using emote_fixture::Scene;
    auto content=[](const char* src,double x,double y,double z) {
        return O({{"src",S(src)},{"coord",A({N(x),N(y),N(z)})}});
    };
    auto node=[&](const char* label,double x,double y,double z,artc::PsbValue children=A({})) {
        return Node(2,label,A({Key(0,2,content("layout",x,y,z))}),std::move(children));
    };
    auto transparent=node("transparent",100,200,100,A({node("bypassed",2,3,4)}));
    transparent.object["inheritMask"]=N(0x24007fc);
    auto transparent_entry=Node(3,"transparent entry",A({Key(0,2,O({
        {"src",S("motion/actor/inner")},{"coord",A({N(1),N(2),N(20)})}}))}),
        A({node("ordinary entry child",0,0,2)}));
    transparent_entry.object["inheritMask"]=N(0x24007fc);
    auto legacy=node("legacy entry",0,0,5);
    legacy.object["frameList"].array[0].object["content"].object["src"]=S("motion/actor/inner");
    auto split=Node(3,"split entry",A({Key(0,2,O({{"src",S("actor")},{"icon",S("inner")},
        {"coord",A({N(0),N(0),N(-5)})}}))}));
    auto partial=node("partial",0,0,-2);partial.object["inheritMask"]=N(0);
    auto empty=Node(2,"no frame",A({}),A({node("below no frame",0,0,1)}));
    auto parent=node("parent",8,8,10,A({transparent,node("restored",1,2,3),legacy,split,partial,empty,transparent_entry}));
    parent.object["frameList"]=A({Key(0,3,content("layout",8,8,10)),
        Key(4,2,content("layout",8,8,18)),Key(6,0),Key(9,2,content("layout",8,8,-3))});
    // Node metadata has no Z contract and must not be read as one.
    parent.object["metadata"]=O({{"zorder",N(-5000)}});
    auto switched=Node(0,"switch",A({Key(0,2,content("src/images/body",8,8,7)),
        Key(3,2,content("src/images/face",8,8,-8)),Key(4,0),
        Key(9,2,content("src/images/wide",8,8,2))}));
    auto doc=Scene();auto& motions=doc.root.object["object"].object["actor"].object["motion"];
    motions.object["idle"]=O({{"layer",A({parent,node("outside sibling",0,0,-10),switched})},
        {"lastTime",N(20)},{"loopTime",N(-1)}});
    motions.object["inner"]=O({{"layer",A({node("nested leaf",0,0,-4)})},
        {"lastTime",N(20)},{"loopTime",N(-1)}});
    auto& icons=doc.root.object["source"].object["images"].object["icon"];
    icons.object["body"].object["metadata"]=O({{"zorder",N(-900)}});
    icons.object["face"].object["metadata"]=O({{"zorder",N(900)}});
    icons.object["wide"].object["metadata"]=O({{"zorder",N(500)}});
    auto model=std::make_shared<artc::EmoteModel>();std::string error;
    Check(model->Load(doc,error),error.c_str());artc::EmoteScene scene;
    Check(scene.Load(model,error),error.c_str());std::vector<artc::EmoteSceneLayer> layers;
    auto find=[&](const char* label)->const artc::EmoteSceneLayer& {
        for(const auto& l:layers)if(l.label==label)return l;
        Check(false,label);return layers.front();
    };
    for(const auto sample:std::vector<std::pair<double,double>>{{0,10},{2,14},{5,18},{6,18},{8,18},{9,-3},{25,-3}}) {
        Check(scene.Evaluate(sample.first,{},layers,error),error.c_str());
        const double z=sample.second;
        Check(find("parent").effective_z==z,"Z samples interpolation and retains last live state across HOLD");
        Check(find("transparent").effective_z==z+100 && find("bypassed").effective_z==z+4,
              "transparent node has own Z but ordinary descendants inherit the previous source Z");
        Check(find("bypassed").abs_tx==10 && find("bypassed").abs_ty==11,
              "transparent source location maps X/Y and Z through the same bypass contract");
        Check(find("restored").effective_z==z+3 && find("restored").abs_tx==9 && find("restored").abs_ty==10,
              "transparent branch restores parent source position/Z before its sibling");
        Check(find("outside sibling").effective_z==-10 && find("outside sibling").abs_tx==0,
              "parent scope restores before a top-level sibling");
        Check(find("partial").effective_z==z-2,"linear inheritMask channels do not drop source Z");
        Check(find("no frame").effective_z==z && find("below no frame").effective_z==z+1,
              "missing pose retains ancestor source Z for layout and descendants");
        std::vector<double> nested;
        for(const auto& l:layers)if(l.label=="nested leaf")nested.push_back(l.effective_z);
        Check(nested==std::vector<double>({z+1,z-9,z+16}),
              "legacy/split nested players retain attachment Z, including a transparent mount's own Z");
        Check(find("ordinary entry child").effective_z==z+2,
              "transparent mount's ordinary children bypass it without leaking nested-player source Z");
        Check(find("switch").effective_z==(sample.first<3?7:sample.first<9?-8:2),
              "icon changes and HOLD use runtime Z, never static icon metadata");
    }
    artc::Compositor compositor;compositor.Init(32,32);
    Check(scene.Render(compositor,"30",0,{},error),error.c_str());
    const auto key=std::string("30")+find("switch").key+".000000";
    // Explicit public paint remains compatible, but Render never invents or
    // resets it on behalf of labels/metadata.
    compositor.SetLayerPaint(key,7);
    const auto revision=compositor.Revision();
    Check(scene.Render(compositor,"30",4,{},error),error.c_str());
    bool published=false;
    for(const auto& l:compositor.Layers())if(l.id==key) {
        published=l.effect.emote_z==-8 && l.effect.paint==7;
    }
    Check(published && compositor.Revision()>revision,"Render publishes current Z on reused art and preserves explicit paint");
    // Finite numeric validation and unsupported coordinate modes are explicit,
    // not guessed projections or alternate metadata-derived ordering paths.
    for(bool stencil:{false,true}) {
        auto invalid=doc;
        auto& n=invalid.root.object["object"].object["actor"].object["motion"].object["idle"].object["layer"].array[0];
        n.object["coordinate"]=N(1);if(stencil)n.object["type"]=N(12);
        auto bad=std::make_shared<artc::EmoteModel>();Check(bad->Load(invalid,error),error.c_str());
        artc::EmoteScene rejected;Check(!rejected.Load(bad,error),"non-static coordinate mode rejects even a stencil parent");
    }
    for(bool stencil:{false,true}) {
        auto invalid=doc;
        auto& n=invalid.root.object["object"].object["actor"].object["motion"].object["idle"].object["layer"].array[0];
        n.object["frameList"].array[1].object["content"].object["coord"].array[2]=N(INFINITY);
        if(stencil)n.object["type"]=N(12);
        auto bad=std::make_shared<artc::EmoteModel>();Check(bad->Load(invalid,error),error.c_str());
        artc::EmoteScene rejected;Check(!rejected.Load(bad,error),
            "non-finite future Z is rejected before installation, including stencil source containers");
    }
}

static void SceneTransformInheritanceTests() {
    auto node=[](const char* label,artc::PsbValue content,artc::PsbValue children=A({})) {
        return emote_fixture::Node(0,label,A({emote_fixture::Key(0,2,std::move(content))}),std::move(children));
    };
    auto partial=node("partial",O({{"src",S("src/images/face")},{"coord",A({N(2),N(3)})}}));
    partial.object["inheritMask"]=N(0x2000010); // rotation only; not zoom/opacity
    auto transparent=node("transparent",O({{"src",S("layout")},{"coord",A({N(100),N(200)})},
        {"zx",N(7)},{"angle",N(90)},{"opa",N(64)}}),
        A({node("transparent child",O({{"src",S("src/images/face")},{"coord",A({N(2),N(3)})}}))}));
    transparent.object["inheritMask"]=N(0x24007fc);
    auto nested=node("entry",O({{"src",S("motion/actor/inner")},{"coord",A({N(1),N(2)})},
        {"zx",N(2)},{"zy",N(2)},{"opa",N(128)}}));
    auto parent=node("parent",O({{"src",S("layout")},{"coord",A({N(10),N(20)})},
        {"zx",N(2)},{"zy",N(3)},{"angle",N(90)},{"opa",N(128)}}),A({partial,transparent,nested}));
    auto d=emote_fixture::Scene();auto& motions=d.root.object["object"].object["actor"].object["motion"];
    motions.object["idle"].object["layer"]=A({parent});
    auto inner_parent=node("inner parent",O({{"src",S("layout")},{"zx",N(5)},{"zy",N(7)}}),
        A({node("inner partial",O({{"src",S("src/images/face")},{"coord",A({N(1),N(0)})}}))}));
    inner_parent.object["children"].array[0].object["inheritMask"]=N(0x2000000); // enclosing root remains
    motions.object["inner"]=O({{"layer",A({inner_parent})},{"lastTime",N(11)},{"loopTime",N(-1)}});
    std::string error;auto model=std::make_shared<artc::EmoteModel>();
    Check(model->Load(std::move(d),error),error.c_str());artc::EmoteScene scene;
    Check(scene.Load(model,error),error.c_str());std::vector<artc::EmoteSceneLayer> layers;
    Check(scene.Evaluate(0,{},layers,error),error.c_str());
    auto check=[&](const char* label,double a,double b,double c,double e,double x,double y,double opa) {
        for(const auto& l:layers)if(l.label==label) {
            Check(std::abs(l.abs_m11-a)<1e-8 && std::abs(l.abs_m12-b)<1e-8 &&
                  std::abs(l.abs_m21-c)<1e-8 && std::abs(l.abs_m22-e)<1e-8 &&
                  std::abs(l.abs_tx-x)<1e-8 && std::abs(l.abs_ty-y)<1e-8 &&
                  std::abs(l.abs_opa-opa)<1e-8,label);return;
        }
        Check(false,label);
    };
    const double alpha=128.0/255.0;
    // Default order builds S(2,3)*R(90), so the parent's matrix is
    // [0,-2;3,0]; anchors still use it even when zoom is not inherited.
    check("partial",0,-1,1,0,4,26,1);
    check("transparent child",0,-2,3,0,4,26,alpha);
    check("entry",0,-4,6,0,6,23,alpha*alpha);
    // Partial inheritance drops inner parent's zoom, but not entry/root zoom;
    // position still uses the inner parent's full linear: 5*6=30 in y.
    check("inner partial",0,-4,6,0,6,53,alpha*alpha);
}

// Shared-atlas decode caching: icons of one source share a single texture decode
// (the dominant load cost of real win/common models), results stay byte-identical
// to the uncached path, and the scratch is bounded and never counted as retained.
static artc::PsbDocument AtlasCacheDoc(const std::vector<std::string>& sources,int w,int h,const char* type,
                                       std::vector<uint8_t> bytes,
                                       const std::vector<std::pair<std::string,std::string>>& used,
                                       const std::vector<std::vector<std::pair<std::string,artc::PsbValue>>>& icon_lists) {
    namespace F=emote_fixture;
    artc::PsbDocument doc;doc.bytes=std::move(bytes);
    artc::PsbValue src_object=F::O({});
    for(size_t i=0;i<sources.size();++i) {
        artc::PsbValue pixel;pixel.type=artc::PsbValue::Resource;pixel.resource=uint32_t(i);
        doc.resources.push_back({0,doc.bytes.size()});
        artc::PsbValue icons=F::O({});
        for(const auto& icon:icon_lists[i])icons.object[icon.first]=icon.second;
        src_object.object[sources[i]]=F::O({{"texture",F::O({{"width",F::N(w)},{"height",F::N(h)},
            {"type",F::S(type)},{"pixel",pixel}})},{"icon",icons}});
    }
    artc::PsbValue frames_nodes=F::A({});
    int n=0;
    for(const auto& u:used) {
        const std::string label="n"+std::to_string(n++);
        auto node=F::Node(0,label.c_str(),F::A({F::Key(0,2,F::O({{"src",F::S(u.first.c_str())},{"icon",F::S(u.second.c_str())}})),
            F::Key(11,0)}));
        frames_nodes.array.push_back(std::move(node));
    }
    doc.root=F::O({{"spec",F::S("common")},{"source",src_object},
        {"object",F::O({{"body",F::O({{"motion",F::O({{"idle",F::O({{"layer",frames_nodes},
            {"lastTime",F::N(11)},{"loopTime",F::N(0)}})}})}})}})},
        {"metadata",F::O({{"base",F::O({{"chara",F::S("body")},{"motion",F::S("idle")}})}})}});
    return doc;
}
static artc::PsbValue AtlasIcon(int left,int top,int w,int h,bool mips=false) {
    namespace F=emote_fixture;
    auto v=F::O({{"left",F::N(left)},{"top",F::N(top)},{"width",F::N(w)},{"height",F::N(h)},
        {"originX",F::N(1)},{"originY",F::N(2)}});
    if(mips)v.object["mipMap"]=F::N(1);
    return v;
}
static void AtlasCacheTests() {
    namespace F=emote_fixture;
    std::string error;
    std::vector<uint8_t> rgba8(32);
    for(size_t i=0;i<rgba8.size();++i)rgba8[i]=uint8_t(10+i*7);
    const auto equal_images=[](const artc::EmoteImage& a,const artc::EmoteImage& b) {
        return a.width==b.width && a.height==b.height && a.origin_x==b.origin_x && a.origin_y==b.origin_y && a.rgba==b.rgba;
    };
    {   // Scene: four nodes over three icons of one atlas decode the atlas once.
        auto doc=AtlasCacheDoc({"tex"},4,2,"RGBA8",rgba8,{{"tex","a"},{"tex","b"},{"tex","a"},{"tex","c"}},
            {{{"a",AtlasIcon(0,0,2,2)},{"b",AtlasIcon(2,0,2,2)},{"c",AtlasIcon(0,0,4,2)}}});
        auto model=std::make_shared<artc::EmoteModel>();Check(model->Load(doc,error),error.c_str());
        artc::EmoteScene scene;Check(scene.Load(model,error),error.c_str());
        Check(scene.LoadAtlasDecodes()==1,"icons of one shared atlas decode that atlas exactly once");
        std::vector<artc::EmoteSceneLayer> layers;
        Check(scene.Evaluate(0,{},layers,error) && layers.size()==4,"cached scene evaluates all icon layers");
        // Cached and uncached Image agree byte for byte, including a full-texture icon.
        artc::EmoteAtlasCache cache;
        for(const char* name:{"a","b","a","c","b"}) {
            artc::EmoteImage plain,cached;std::string e1,e2;
            Check(model->Image("tex",name,plain,e1) && model->Image("tex",name,cached,e2,&cache) && equal_images(plain,cached),
                  "cached icon equals uncached icon");
        }
        Check(cache.Decodes()==1 && cache.Hits()==4 && cache.Bytes()==rgba8.size(),"cache counts one decode and four hits");
        // A full-texture icon is a copy: mutating it must not touch the cached atlas.
        artc::EmoteImage full;Check(model->Image("tex","c",full,error,&cache),error.c_str());
        full.rgba.assign(full.rgba.size(),0);artc::EmoteImage again;
        Check(model->Image("tex","c",again,error,&cache) && again.rgba[0]==rgba8[0] && cache.Decodes()==1,
              "full-texture icon does not alias the cached atlas");
    }
    {   // Eviction keeps the scratch bounded; an oversized source is never retained.
        auto doc=AtlasCacheDoc({"s1","s2"},4,2,"RGBA8",rgba8,{{"s1","a"},{"s2","a"}},
            {{{"a",AtlasIcon(0,0,2,2)}},{{"a",AtlasIcon(2,0,2,2)}}});
        artc::EmoteModel model;Check(model.Load(doc,error),error.c_str());
        artc::EmoteImage image;
        artc::EmoteAtlasCache tight(40);   // holds one 32-byte texture
        for(const char* source:{"s1","s2","s1","s1"})
            Check(model.Image(source,"a",image,error,&tight) && tight.Bytes()<=tight.Limit(),"bounded cache decodes");
        Check(tight.Decodes()==3 && tight.Hits()==1 && tight.Bytes()==32,"least recently used source is evicted");
        artc::EmoteAtlasCache none(16);    // smaller than any texture here
        for(int i=0;i<3;++i)Check(model.Image("s1","a",image,error,&none) && none.Bytes()==0,"oversized source");
        Check(none.Decodes()==3 && none.Hits()==0 && image.rgba.size()==16,"oversized source is decoded per icon, not retained");
        // A different model never sees another model's cached pixels.
        artc::EmoteModel other;auto doc2=doc;doc2.bytes.assign(32,200);
        Check(other.Load(doc2,error),error.c_str());artc::EmoteAtlasCache shared;
        artc::EmoteImage first,second;
        Check(model.Image("s1","a",first,error,&shared) && other.Image("s1","a",second,error,&shared) &&
              first.rgba!=second.rgba && second.rgba[0]==200,"cache is bound to one model");
    }
    {   // The per-icon mip descriptor still decides length validity per icon.
        std::vector<uint8_t> longer=rgba8;longer.resize(40,9);
        auto doc=AtlasCacheDoc({"tex"},4,2,"RGBA8",longer,{{"tex","a"},{"tex","b"}},
            {{{"a",AtlasIcon(0,0,2,2,true)},{"b",AtlasIcon(2,0,2,2)}}});
        artc::EmoteModel model;Check(model.Load(doc,error),error.c_str());
        for(int order=0;order<2;++order) {
            const char* names[2]={order?"b":"a",order?"a":"b"};
            artc::EmoteAtlasCache cache;
            for(const char* name:names) {
                artc::EmoteImage plain,cached;std::string e1,e2;
                const bool ok1=model.Image("tex",name,plain,e1),ok2=model.Image("tex",name,cached,e2,&cache);
                Check(ok1==ok2 && e1==e2 && (!ok1 || equal_images(plain,cached)),"mip length rule is per icon with a cache");
                Check(ok1==(std::string(name)=="a") && (ok1 || e1=="invalid E-mote raw texture length"),"mip length error text");
            }
        }
    }
    {   // Retained-image budget counts crops (128 MiB), never the decoded atlas scratch.
        const int w=4096,h=2048,band=512;    // DXT1 zero blocks: 4 MiB in, 32 MiB atlas, 8 MiB per icon
        std::vector<std::pair<std::string,artc::PsbValue>> icons;
        std::vector<std::pair<std::string,std::string>> used;
        const auto build=[&](int count) {
            icons.clear();used.clear();
            for(int i=0;i<count;++i) {
                const std::string name="i"+std::to_string(i);
                icons.push_back({name,AtlasIcon(0,(i%4)*band,w,band)});used.push_back({"big",name});
            }
            return AtlasCacheDoc({"big"},w,h,"DXT1",std::vector<uint8_t>(size_t(w/4)*(h/4)*8,0),used,{icons});
        };
        auto small=std::make_shared<artc::EmoteModel>();
        Check(small->Load(build(2),error),error.c_str());artc::EmoteScene scene;
        Check(scene.Load(small,error) && scene.LoadAtlasDecodes()==1,"small atlas scene loads");
        auto exact=std::make_shared<artc::EmoteModel>();Check(exact->Load(build(16),error),error.c_str());
        artc::EmoteScene full_scene;
        Check(full_scene.Load(exact,error) && full_scene.LoadAtlasDecodes()==1,
              "exactly 128 MiB of retained crops still loads (atlas scratch not counted)");
        auto over=std::make_shared<artc::EmoteModel>();Check(over->Load(build(17),error),error.c_str());
        Check(!scene.Load(over,error) && error.find("exceed memory limit")!=std::string::npos,
              "retained crops above 128 MiB are rejected");
        std::vector<artc::EmoteSceneLayer> layers;
        Check(scene.LoadAtlasDecodes()==1 && scene.Evaluate(0,{},layers,error) && layers.size()==2,
              "failed Load keeps the previous scene");
    }
}
int main(int argc,char** argv) {
#if defined(ARTC_HAS_GLES)
    // SetPixels uploads through real GL; OHOS builds the compositor with GLES
    // unconditionally, so give the binary a pbuffer context (as the desktop
    // ANGLE compositor suite does) before anything renders.
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(display==EGL_NO_DISPLAY || !eglInitialize(display,nullptr,nullptr)) {
        std::cerr<<"initialize EGL: error 0x"<<std::hex<<eglGetError()<<std::endl;
        std::exit(1);
    }
    EGLint cfg_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;
    Check(eglChooseConfig(display,cfg_attrs,&config,1,&count)&&count,"choose EGL config");
    EGLint pb_attrs[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,pb_attrs);
    EGLint ctx_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx_attrs);
    Check(eglMakeCurrent(display,surface,surface,context),"make EGL context current");
#endif
    if(argc==2 && std::string(argv[1])=="--geometry-only") {
        CompositorAffineApiTests();
        SceneAffineGeometryTests();
        SceneMeshSyncTests();
        SceneHitGeometryTests();
        SceneZChannelTests();
        SceneTransformInheritanceTests();
        std::cout<<"affine/shape/mesh/hit/Z geometry regressions passed\n";
        return 0;
    }
    ModelTests();
    AtlasCacheTests();
    PlayerTests();
    SceneEffectResetTests();
    SceneHitGeometryTests();
    SceneDiagnosticTests();
    CompositorAffineApiTests();
    SceneAffineGeometryTests();
    SceneMeshSyncTests();
    SceneZChannelTests();
    SceneTransformInheritanceTests();
    {
        auto data=emote_fixture::Scene();auto model=std::make_shared<artc::EmoteModel>();std::string error;
        Check(model->Load(data,error),error.c_str());artc::EmoteScene scene;Check(scene.Load(model,error),error.c_str());
        std::vector<artc::EmoteSceneLayer> layers;
        Check(scene.Evaluate(5,{{"expression",10}},layers,error) && layers.front().x==13 && layers.back().icon=="wide",
            "E-mote child motion, variable range binding and continuous parent frame");
        Check(scene.Evaluate(100,{},layers,error) && layers.front().x==9 && layers.back().icon=="face","scene time wraps the base motion loop");
        const auto size=layers.size();
        Check(!scene.Evaluate(-1,{},layers,error) && layers.size()==size,"invalid scene time preserves output");
        data.root.object["object"].object["actor"].object["motion"].object["idle"].object["layer"].array[0]
            .object["frameList"].array[1].object["content"].object["mesh"]=emote_fixture::A({});
        auto bad=std::make_shared<artc::EmoteModel>();Check(bad->Load(data,error),error.c_str());
        Check(!scene.Load(bad,error) && error.find("mesh")!=std::string::npos && scene.Evaluate(5,{},layers,error),
            "unsupported later deformation fails before replacing a working scene");
    }
    {
        // WP5: newer MotionEditor curve/color content must not reject the model
        // yet (evaluation keeps treating them as absent; a first-use log notes).
        auto curved=emote_fixture::Scene();
        std::string error;
        auto& content=curved.root.object["object"].object["actor"].object["motion"].object["idle"]
            .object["layer"].array[0].object["frameList"].array[0].object["content"];
        content.object["cc"]=emote_fixture::O({{"c",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0)})},
            {"x",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0)})},
            {"y",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0)})}});
        content.object["zcc"]=emote_fixture::O({{"c",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0)})},
            {"x",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0)})},
            {"y",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0)})}});
        content.object["ccc"]=emote_fixture::O({{"c",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0)})},
            {"x",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0)})},
            {"y",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0),emote_fixture::N(0)})}});
        content.object["color"]=emote_fixture::N(0xff808080);
        auto curved_model=std::make_shared<artc::EmoteModel>();
        Check(curved_model->Load(curved,error),error.c_str());
        artc::EmoteScene curved_scene;
        Check(curved_scene.Load(curved_model,error),error.c_str());
        std::vector<artc::EmoteSceneLayer> curved_layers;
        Check(curved_scene.Evaluate(5,{},curved_layers,error) && !curved_layers.empty(),
            "curve/color content loads without rejecting the model");
    }
    {
        // Win-split content (甜蜜女友3-style): src names the texture source and
        // icon the entry; type==3 nests (src=chara, icon=motion) and supports a
        // motion-level parameterize; stencil nodes are skipped, null bp/cc mesh
        // placeholders stay affine.
        std::string error;
        artc::PsbValue pixel; pixel.type=artc::PsbValue::Resource; pixel.resource=0;
        artc::PsbValue null_mesh; null_mesh.type=artc::PsbValue::Null;
        auto sub_node=emote_fixture::Node(0,"subpic",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("tex")},
                {"icon",emote_fixture::S("a")},{"coord",emote_fixture::A({emote_fixture::N(1),emote_fixture::N(1),emote_fixture::N(0)})}})),
            emote_fixture::Key(10,2,emote_fixture::O({{"src",emote_fixture::S("tex")},
                {"icon",emote_fixture::S("b")}})),
            emote_fixture::Key(11,0)}));
        auto sub_motion=emote_fixture::O({{"layer",emote_fixture::A({sub_node})},
            {"lastTime",emote_fixture::N(11)},{"loopTime",emote_fixture::N(-1)},
            {"parameterize",emote_fixture::N(0)},
            {"parameter",emote_fixture::A({emote_fixture::O({{"id",emote_fixture::S("expression")},
                {"rangeBegin",emote_fixture::N(0)},{"rangeEnd",emote_fixture::N(10)},
                {"division",emote_fixture::N(10)}})})}});
        auto pic_node=emote_fixture::Node(0,"pic",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("tex")},
                {"icon",emote_fixture::S("a")},
                {"mesh",emote_fixture::O({{"bp",null_mesh},{"cc",null_mesh}})}})),
            emote_fixture::Key(11,0)}));
        auto stencil_node=emote_fixture::Node(12,"stencil",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("tex")},
                {"icon",emote_fixture::S("a")},{"stencilType",emote_fixture::N(4)}})),
            emote_fixture::Key(11,0)}),
            emote_fixture::A({emote_fixture::Node(0,"stencil_child",emote_fixture::A({
                emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("tex")},
                    {"icon",emote_fixture::S("b")}})),
                emote_fixture::Key(11,0)}))}));
        auto sub_ref=emote_fixture::Node(3,"subref",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("body")},
                {"icon",emote_fixture::S("sub")},
                {"motion",emote_fixture::O({{"timeOffset",emote_fixture::N(0)}})}})),
            emote_fixture::Key(11,0)}));
        auto blank_node=emote_fixture::Node(2,"blanky",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("blank")},
                {"icon",emote_fixture::S("4:2:1:0.5")}})),
            emote_fixture::Key(11,0)}));
        artc::PsbDocument split;
        split.root=emote_fixture::O({{"spec",emote_fixture::S("common")},
            {"source",emote_fixture::O({{"tex",emote_fixture::O({
                {"texture",emote_fixture::O({{"width",emote_fixture::N(4)},{"height",emote_fixture::N(2)},
                    {"type",emote_fixture::S("RGBA8")},{"pixel",pixel}})},
                {"icon",emote_fixture::O({
                    {"a",emote_fixture::O({{"left",emote_fixture::N(0)},{"top",emote_fixture::N(0)},
                        {"width",emote_fixture::N(2)},{"height",emote_fixture::N(2)},
                        {"originX",emote_fixture::N(1)},{"originY",emote_fixture::N(1)}})},
                    {"b",emote_fixture::O({{"left",emote_fixture::N(2)},{"top",emote_fixture::N(0)},
                        {"width",emote_fixture::N(2)},{"height",emote_fixture::N(2)},
                        {"originX",emote_fixture::N(1)},{"originY",emote_fixture::N(1)}})}})}})}})},
            {"object",emote_fixture::O({{"body",emote_fixture::O({{"motion",emote_fixture::O({
                {"idle",emote_fixture::O({{"layer",emote_fixture::A({pic_node,stencil_node,sub_ref,blank_node})},
                    {"lastTime",emote_fixture::N(11)},{"loopTime",emote_fixture::N(0)}})},
                {"sub",sub_motion}})}})}})},
            {"metadata",emote_fixture::O({{"base",emote_fixture::O({{"chara",emote_fixture::S("body")},
                {"motion",emote_fixture::S("idle")}})},
                {"variableList",emote_fixture::A({emote_fixture::O({{"label",emote_fixture::S("expression")}})})}})}});
        for(int i=0;i<8;++i) {
            split.bytes.push_back(uint8_t(10+i*10));
            split.bytes.push_back(uint8_t(20+i*10));
            split.bytes.push_back(uint8_t(30+i*10));
            split.bytes.push_back(255);
        }
        split.resources={{0,32}};
        auto split_model=std::make_shared<artc::EmoteModel>();
        Check(split_model->Load(split,error),error.c_str());
        artc::EmoteScene split_scene;
        Check(split_scene.Load(split_model,error),error.c_str());
        std::vector<artc::EmoteSceneLayer> split_layers;
        Check(split_scene.Evaluate(0,{{"expression",0}},split_layers,error),"split model evaluates");
        std::string sub_icon;bool stencil_own_draw=false;bool stencil_child=false;bool pic_found=false;
        for(const auto& l:split_layers) {
            if(l.label=="subpic")sub_icon=l.icon;
            if(l.label=="stencil" && !l.source.empty())stencil_own_draw=true;
            if(l.label=="stencil_child" && l.source=="tex" && l.icon=="b")stencil_child=true;
            if(l.label=="pic" && l.source=="tex" && l.icon=="a")pic_found=true;
        }
        Check(pic_found && !stencil_own_draw && stencil_child,
              "split content draws by source+icon; stencil keeps its children");
        Check(sub_icon=="a","nested split motion evaluates at parameter 0");
        bool blank_found=false;
        for(const auto& l:split_layers)
            if(l.label=="blanky" && l.origin_x==1 && l.origin_y==0.5)blank_found=true;
        Check(blank_found,"blank descriptor sets the layout origin");
        Check(split_scene.Evaluate(0,{{"expression",10}},split_layers,error),"split model evaluates at 10");
        sub_icon.clear();
        for(const auto& l:split_layers) if(l.label=="subpic")sub_icon=l.icon;
        Check(sub_icon=="b","motion-level parameterize selects the nested tick");
    }
    {
        // E9: motion.priority reorders sibling emission (draw order). The
        // content list is reverse draw order (last entry draws first), so
        // [0,1] puts node 1 (pb) at the bottom.
        auto prio_doc=emote_fixture::Scene();
        auto node_a=emote_fixture::Node(0,"pa",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("src/images/face")}})),
            emote_fixture::Key(11,0)}));
        auto node_b=emote_fixture::Node(0,"pb",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("src/images/body")}})),
            emote_fixture::Key(11,0)}));
        auto& idle=prio_doc.root.object["object"].object["actor"].object["motion"].object["idle"];
        idle.object["layer"]=emote_fixture::A({node_a,node_b});
        idle.object["priority"]=emote_fixture::A({
            emote_fixture::O({{"time",emote_fixture::N(0)},
                {"content",emote_fixture::A({emote_fixture::N(0),emote_fixture::N(1)})}})});
        auto prio_model=std::make_shared<artc::EmoteModel>();
        std::string error;
        Check(prio_model->Load(prio_doc,error),error.c_str());
        artc::EmoteScene prio_scene;
        Check(prio_scene.Load(prio_model,error),error.c_str());
        std::vector<artc::EmoteSceneLayer> prio_layers;
        Check(prio_scene.Evaluate(0,{},prio_layers,error),"priority model evaluates");
        Check(prio_layers.size()>=2 && prio_layers[0].label=="pb" && prio_layers[1].label=="pa",
              "priority content reorders sibling emission");
    }
    {
        // E9 (device evidence): content `bm` selects the native blend mode and
        // non-default inheritMask masks load; both appear in real 立绘 models.
        auto blend_doc=emote_fixture::Scene();
        auto& blend_idle=blend_doc.root.object["object"].object["actor"].object["motion"].object["idle"];
        auto& group=blend_idle.object["layer"].array[0];
        group.object["children"].array[0].object["frameList"].array[0].object["content"]
            .object["bm"]=emote_fixture::N(0x13);
        auto masked=emote_fixture::Node(2,"masked",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("src/images/face")},
                {"bm",emote_fixture::N(2)}})),
            emote_fixture::Key(11,0)}));
        masked.object["inheritMask"]=emote_fixture::N(0x200020c);
        group.object["children"].array.push_back(masked);
        auto blend_model=std::make_shared<artc::EmoteModel>();
        std::string error;
        Check(blend_model->Load(blend_doc,error),error.c_str());
        artc::EmoteScene blend_scene;
        Check(blend_scene.Load(blend_model,error),error.c_str());
        std::vector<artc::EmoteSceneLayer> blend_layers;
        Check(blend_scene.Evaluate(0,{},blend_layers,error),"blend model evaluates");
        bool multiply=false,subtract=false;
        for(const auto& l:blend_layers) {
            if(l.blend=="multiply")multiply=true;
            if(l.blend=="subtract")subtract=true;
        }
        Check(multiply && subtract,"content bm maps to native blend modes");
    }
    {
        // E9: stencil composite mask labels propagate to a stencil node's
        // descendants (stencilType bit 0x4 replaces the inherited list); the
        // stencil node itself stays a non-drawing container.
        auto mask_doc=emote_fixture::Scene();
        auto& idle=mask_doc.root.object["object"].object["actor"].object["motion"].object["idle"];
        auto masked=emote_fixture::Node(0,"masked_part",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("src/images/face")}})),
            emote_fixture::Key(11,0)}));
        auto stencil=emote_fixture::Node(12,"mask_group",emote_fixture::A({
            emote_fixture::Key(0,2,emote_fixture::O({{"src",emote_fixture::S("src/images/body")}})),
            emote_fixture::Key(11,0)}),emote_fixture::A({masked}));
        stencil.object["stencilType"]=emote_fixture::N(5);
        stencil.object["stencilCompositeMaskLayerList"]=
            emote_fixture::A({emote_fixture::S("source_mask")});
        idle.object["layer"]=emote_fixture::A({stencil});
        auto mask_model=std::make_shared<artc::EmoteModel>();
        std::string error;
        Check(mask_model->Load(mask_doc,error),error.c_str());
        artc::EmoteScene mask_scene;
        Check(mask_scene.Load(mask_model,error),error.c_str());
        std::vector<artc::EmoteSceneLayer> mask_layers;
        Check(mask_scene.Evaluate(0,{},mask_layers,error),"mask model evaluates");
        bool masked_ok=false,container_ok=false;
        for(const auto& l:mask_layers) {
            if(l.label=="masked_part" && l.source=="images" && l.masks.size()==1 &&
               l.masks[0]=="source_mask")masked_ok=true;
            if(l.label=="mask_group" && l.source.empty())container_ok=true;
        }
        Check(masked_ok && container_ok,"stencil mask labels propagate to descendants");
    }
    const auto fixture=Fixture();artc::PsbDocument doc;std::string error;
    Check(artc::DecodePsb(fixture,doc,error),error.c_str());
    const auto& a=doc.root.At("a").array;
    Check(a.size()==4 && a[0].string=="hi" && a[2].number==-2 && a[3].number==1,"PSB dictionaries, arrays, signed numbers and strings");
    std::vector<uint8_t> pixels;
    Check(doc.ReadResource(a[1],pixels) && pixels==std::vector<uint8_t>({255,0,0,255}),"PSB resource ranges");
    for(size_t size=0;size<fixture.size();++size) {
        const std::vector<uint8_t> cut(fixture.begin(),fixture.begin()+size);
        Check(!artc::DecodePsb(cut,doc,error) && doc.root.At("a").array.size()==4,"truncated PSB leaves previous document intact");
    }
    auto broken=fixture;broken[6]=1;
    // A forged encryption flag with a plain body fails header validation (the
    // real encrypted-header path is covered by psb_encrypted_regressions).
    Check(!artc::DecodePsb(broken,doc,error) && !error.empty(),"forged encrypted PSB is rejected");
    // The root's sole value offset loops back to the root dictionary.
    broken=fixture;broken[36]=255;broken[37]=255;broken[38]=255;broken[39]=255;
    Check(!artc::DecodePsb(broken,doc,error),"PSB rejects overflowing offsets");
    std::vector<uint8_t> mdf={'m','d','f',0};for(int i=0;i<4;++i)mdf.push_back(uint8_t(fixture.size()>>(8*i)));
    uLongf len=compressBound(fixture.size());mdf.resize(8+len);
    Check(compress2(mdf.data()+8,&len,fixture.data(),fixture.size(),6)==Z_OK,"MDF compress fixture");mdf.resize(8+len);
    Check(artc::DecodePsb(mdf,doc,error),error.c_str());mdf.back()^=1;
    Check(!artc::DecodePsb(mdf,doc,error),"MDF checksum failure");
    Check(artc::DecodePsbRl({128,1,2,3,4,0,5,6,7,8},4,4,pixels) && pixels.size()==16 && pixels[12]==5,"RL repeat and literal complete pixels");
    Check(artc::DecodePsbRl({128,7,0,9},4,1,pixels) && pixels==std::vector<uint8_t>({7,7,7,9}),"RL palette indices");
    Check(!artc::DecodePsbRl({129,7},3,1,pixels) && pixels.back()==9,"RL rejects oversized packet transactionally");
    Check(!artc::DecodePsbRl({128,7},4,1,pixels),"RL rejects undersized stream");
    Check(!artc::DecodePsbRl({128,7,0,9},3,1,pixels),"RL rejects trailing packet");
    for(int i=1;i<argc;++i) {
        std::ifstream f(argv[i],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        Check(artc::DecodePsb(bytes,doc,error),error.c_str());
        std::cout<<"PSB v"<<doc.version<<" spec="<<doc.root.At("spec").string
                 <<" resources="<<doc.resources.size()<<" timelines="<<doc.root.At("metadata").At("timelineControl").array.size()<<'\n';
        artc::EmoteModel model;Check(model.Load(std::move(doc),error),error.c_str());size_t images=0;
        for(const auto& source:model.Document().root.At("source").object)
            for(const auto& icon:source.second.At("icon").object) {
                artc::EmoteImage image;Check(model.Image(source.first,icon.first,image,error),error.c_str());++images;
            }
        for(const auto& t:model.Timelines()) {
            std::map<std::string,double> values;Check(model.Sample(t.first,100.25,values),"sample real model track");
            for(const auto& v:values)Check(std::isfinite(v.second),"real timeline produces finite variables");
        }
        std::cout<<"decoded_images="<<images<<" variables="<<model.Variables().size()<<'\n';
        artc::EmoteScene scene;
        const bool supported=scene.Load(std::make_shared<artc::EmoteModel>(std::move(model)),error);
        std::cout<<"scene_renderer="<<(supported?"supported":error)<<'\n';
    }
}
