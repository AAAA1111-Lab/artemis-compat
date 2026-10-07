// Font style inheritance through production Lua tags and GLES glyph rendering.
// Uses the project's original rectangular font; no game assets or API stubs.
#include "config/ini.h"
#include "pack/pack_manager.h"
#include "render/compositor.h"
#include "script/lua_engine.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const char* why) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << why << '\n'; }
}
void Put32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (int i=0; i<4; ++i) bytes.push_back((value >> (8*i)) & 255);
}
const artc::Layer* Find(const artc::Compositor& c, const char* id) {
    for (const auto& layer : c.Layers()) if (layer.id == id) return &layer;
    return nullptr;
}
void Size(const artc::Compositor& c, const char* id, float expected, const char* why) {
    const auto* layer = Find(c, id);
    // rectangle.ttf's ink box is 600x700 units per 1000-unit em.
    Check(layer && layer->glyphs.size()==1 &&
          std::abs(layer->glyphs[0].w-std::ceil(expected*0.6f))<=1 &&
          std::abs(layer->glyphs[0].h-std::ceil(expected*0.7f))<=1, why);
}
int InkHeight(artc::Compositor& c, int width, int height) {
    glViewport(0,0,width,height);
    c.Draw();
    std::vector<uint8_t> pixels(static_cast<size_t>(width)*height*4);
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    int first=height, last=-1;
    for (int y=0; y<height; ++y)
        for (int x=0; x<width; ++x)
            if (pixels[(static_cast<size_t>(y)*width+x)*4]>128) {
                first=std::min(first,y); last=std::max(last,y);
            }
    Check(glGetError()==GL_NO_ERROR, "font rendering has no GLES error");
    return last<first ? 0 : last-first+1;
}
}

int main() {
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    Check(eglInitialize(display,nullptr,nullptr), "initialize EGL");
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config=nullptr; EGLint count=0;
    Check(eglChooseConfig(display,attrs,&config,1,&count) && count, "choose EGL config");
    const EGLint surface_attrs[]={EGL_WIDTH,512,EGL_HEIGHT,384,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attrs);
    const EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
    Check(eglMakeCurrent(display,surface,surface,context), "make EGL context current");
    if (failures) return 1;
    std::ifstream font(std::string(ARTC_TEST_DATA)+"/rectangle.ttf",std::ios::binary);
    Check(bool(font), "open synthetic font");
    std::vector<uint8_t> font_data{std::istreambuf_iterator<char>(font),{}};
    if (failures) return 1;
    const std::string name="font.ttf";
    const uint32_t index_size=4+16+name.size();
    std::vector<uint8_t> pack={'p','f','8'};
    Put32(pack,index_size); Put32(pack,1); Put32(pack,name.size());
    pack.insert(pack.end(),name.begin(),name.end());
    Put32(pack,0); Put32(pack,7+index_size); Put32(pack,font_data.size());
    pack.insert(pack.end(),font_data.begin(),font_data.end());
    const auto path=std::filesystem::temp_directory_path()/
        ("artc_font_style_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".pfs");
    { std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(pack.data()),pack.size()); }
    artc::PackManager packs;
    Check(packs.OpenChain(path.string(),{0}), "open synthetic font pack");
    {
        artc::Compositor c; c.Init(256,192); c.SetPackManager(&packs);
        Check(c.LoadFont(name), "load synthetic font through compositor");
        artc::LuaEngine script;
        Check(script.Init(&packs,artc::Ini{},"android",256,192,&c), "initialize font style script");
        Check(script.DoString(R"(
            e:tag{'fontdefault',face='font.ttf',size=20,width=200}
            e:tag{'chgmsg',id='title'}
            e:tag{'font',size=60,width=850}
            e:tag{'print',data='A'}
            e:tag{'/chgmsg'}
            e:tag{'chgmsg',id='body'}
            e:tag{'print',data='A'}
        )", "title and default body"), "execute title and body tags");
        Size(c,"title",60,"explicit title size overrides game defaults");
        Size(c,"body",20,"new body uses defaults rather than the most recent title font");
        Check(script.DoString(R"(
            e:tag{'fontinit'}
            e:tag{'font',width=180}
            e:tag{'rp'}; e:tag{'print',data='A'}
        )", "reset current font"), "execute current font reset");
        Size(c,"body",20,"fontinit retains declared defaults when later font omits size");
        Size(c,"title",60,"resetting body does not erase another layer's explicit style");
        Check(script.DoString(R"(
            e:tag{'font',size=30}
            e:tag{'rp'}; e:tag{'print',data='A'}
            e:tag{'chgmsg',id='icon_name'}
            e:tag{'font',size=12,width=40,show='none'}
            e:tag{'print',data='A'}
            e:tag{'/chgmsg'}
            assert(e:var('s.current_message_layer')=='body')
            e:tag{'rp'}; e:tag{'print',data='A'}
        )", "nested explicit fonts"), "execute nested style tags");
        Size(c,"body",30,"nested font selection restores the body's explicit size");
        Size(c,"icon_name",12,"hidden font slot retains its own size without becoming the main font");
        Check(script.DoString(R"(
            e:tag{'fontdefault',face='font.ttf',size=18,width=200}
            e:tag{'chgmsg',id='new_name'}; e:tag{'print',data='A'}
            e:tag{'/chgmsg'}
            e:tag{'rp'}; e:tag{'print',data='A'}
        )", "change declared defaults"), "execute updated defaults");
        Size(c,"new_name",18,"unconfigured layer uses new defaults without inheriting another slot");
        Size(c,"body",30,"declaring defaults does not replace an existing explicit layer size");
        Check(script.DoString("e:tag{'fontinit'}; e:tag{'rp'}; e:tag{'print',data='A'}",
                              "reset without another font tag"), "reset current overrides before printing");
        Size(c,"body",18,"fontinit resets only current overrides and print still resolves defaults");
        c.Shutdown();
    }
    {
        artc::Compositor c; c.Init(256,192); c.SetPackManager(&packs);
        Check(c.LoadFont(name), "load partial-default synthetic font");
        artc::LuaEngine script;
        Check(script.Init(&packs,artc::Ini{},"android",256,192,&c), "initialize partial defaults script");
        Check(script.DoString(R"(
            e:tag{'fontdefault',face='font.ttf'}
            e:tag{'font',size=24,width=850}
            e:tag{'chgmsg',id='body'}; e:tag{'print',data='A'}; e:tag{'/chgmsg'}
        )", "face-only defaults"), "execute face-only default and legacy visible font");
        Size(c,"body",24,"face-only defaults retain the legacy visible-slot size");
        Check(script.DoString(R"(
            e:tag{'fontdefault',color='FF0000'}
            e:tag{'chgmsg',id='color_only'}; e:tag{'print',data='A'}; e:tag{'/chgmsg'}
        )", "color-only defaults"), "execute color-only defaults");
        Size(c,"color_only",24,"color-only defaults retain an unspecified legacy size");
        c.SetProps("color_only",{{"left","30"},{"top","0"}});
        glViewport(0,0,256,192); c.Draw();
        uint8_t pixel[4]{};
        glReadPixels(33,192-1-8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        Check(pixel[0]>200 && pixel[1]<10 && pixel[2]<10,
              "declared default color overrides the legacy slot in actual pixels");
        Check(script.DoString(R"(
            e:tag{'fontdefault',face='font.ttf',size=20,width=200}
            e:tag{'chgmsg',id='declared_size'}; e:tag{'print',data='A'}; e:tag{'/chgmsg'}
            e:tag{'chgmsg',id='explicit_size'}; e:tag{'font',size=32}
            e:tag{'print',data='A'}; e:tag{'/chgmsg'}
        )", "size precedence with legacy slot"), "execute declared and explicit size precedence");
        Size(c,"declared_size",20,"a declared default size overrides the previous visible-slot size");
        Size(c,"explicit_size",32,"a layer's explicit size overrides both declared and legacy sizes");
        c.Shutdown();
    }
    {
        artc::Compositor c; c.Init(256,192); c.SetPackManager(&packs);
        Check(c.LoadFont(name), "load legacy synthetic font");
        artc::LuaEngine script;
        Check(script.Init(&packs,artc::Ini{},"android",256,192,&c), "initialize legacy text script");
        Check(script.DoString("e:tag{'chgmsg',id='body'}; e:tag{'print',data='A'}", "legacy size"),
              "execute text without any font defaults");
        Size(c,"body",40,"unconfigured legacy text keeps the original 40-unit default");
        c.Shutdown();
    }
    {
        artc::Compositor c; c.Init(256,192); c.SetPackManager(&packs);
        Check(c.LoadFont(name), "load font for stage scaling");
        Check(c.SetText("1.body","A",40,0xffffff,200), "rasterize original stage text");
        Size(c,"1.body",40,"raster size stays in stage units");
        const int at_stage=InkHeight(c,256,192);
        const int at_surface=InkHeight(c,512,384);
        Check(at_stage>=27 && at_stage<=29 && std::abs(at_surface-2*at_stage)<=2,
              "doubling the surface scales the whole stage once without changing font size");
        c.SetProps("1",{{"xscale","150"},{"yscale","150"}});
        const auto* layer=Find(c,"1.body");
        const auto transform=c.EffectiveTransform(*layer);
        Check(std::abs(transform.a-1.5f)<0.001f && std::abs(transform.d-1.5f)<0.001f,
              "parent scale is applied once by the layer transform");
        const int scaled=InkHeight(c,512,384);
        Check(std::abs(scaled-3*at_stage)<=3,
              "parent and surface scaling compose once each in actual pixels");
        Size(c,"1.body",40,"surface and parent changes do not enlarge glyph raster dimensions");
        c.Shutdown();
    }
    std::filesystem::remove(path);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context); eglDestroySurface(display,surface); eglTerminate(display);
    if (!failures) std::cout << "font style regressions passed\n";
    return failures ? 1 : 0;
}