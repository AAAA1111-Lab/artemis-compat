#include "render/renderer.h"
#include "log/logger.h"

#include <cstdlib>
#include <cstring>

#if defined(__ANDROID__) && defined(ARTC_HAS_GLES)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#endif

namespace artc {

#if defined(__ANDROID__) && defined(ARTC_HAS_GLES)

// Build display / config / context from scratch. Only needed when no context
// survives (first init, or the driver dropped it while backgrounded).
bool Renderer::CreateContext() {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) { last_error_ = "eglGetDisplay failed"; return false; }
    if (!eglInitialize(display_, nullptr, nullptr)) { last_error_ = "eglInitialize failed"; return false; }

    const EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE};
    EGLint num = 0;
    if (!eglChooseConfig(display_, cfg_attrs, &cfg_, 1, &num) || num < 1) {
        last_error_ = "eglChooseConfig failed";
        return false;
    }
    const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    context_ = eglCreateContext(display_, cfg_, EGL_NO_CONTEXT, ctx_attrs);
    if (context_ == EGL_NO_CONTEXT) { last_error_ = "eglCreateContext failed"; return false; }
    return true;
}

bool Renderer::CreateSurface(ANativeWindow *window) {
    surface_ = eglCreateWindowSurface(display_, cfg_, window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        last_error_ = "eglCreateWindowSurface failed";
        return false;
    }
    return true;
}

bool Renderer::Init(void *native_window) {
    auto *window = static_cast<ANativeWindow *>(native_window);
    context_reused_ = false;

    // A surviving context (window loss / background) is re-used as-is: its
    // textures and FBOs are exactly what lets the game resume in place rather
    // than re-running the boot chain.
    if (display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT) {
        Shutdown(); // clean slate
        if (!CreateContext()) { Shutdown(); return false; }
    } else {
        context_reused_ = true;
    }

    if (surface_ != EGL_NO_SURFACE) {   // rebinding: drop the old surface first
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_);
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }
    if (!CreateSurface(window)) { Shutdown(); return false; }
    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
        // The driver reclaimed the context while it had no surface
        // (EGL_CONTEXT_LOST). Rebuild from scratch; ContextReused()==false
        // tells the host that the previous GPU state is gone.
        Shutdown();
        if (!CreateContext() || !CreateSurface(window)) { Shutdown(); return false; }
        if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
            last_error_ = "eglMakeCurrent failed";
            Shutdown();
            return false;
        }
    }

    // Frame pacing (T3-3): swap blocks until the next vsync, replacing the old
    // fixed 16 ms sleep (which drifts against the display clock and wastes
    // wakeups). ARTC_SWAP_INTERVAL=0 is the documented device-level fallback
    // for drivers where the interval is unreliable; the host then paces itself
    // with a sleep. The interval is a surface attribute, so it is (re-)applied
    // on every surface bind here.
    const char *swap_env = std::getenv("ARTC_SWAP_INTERVAL");
    if (!(swap_env && std::strcmp(swap_env, "0") == 0)) {
        const EGLBoolean ok = eglSwapInterval(display_, 1);
        swap_paced_ = (ok == EGL_TRUE);
        Log(kLogInfo, std::string("renderer: eglSwapInterval(1) ") +
                          (swap_paced_ ? "applied" : "rejected; host-paced"));
    } else {
        swap_paced_ = false;
        Log(kLogInfo, "renderer: swap interval disabled (ARTC_SWAP_INTERVAL=0)");
    }

    window_ = window;
    width_ = ANativeWindow_getWidth(window);
    height_ = ANativeWindow_getHeight(window);
    ready_ = true;
    Log(kLogInfo, std::string("renderer: GLES2 ready ") + std::to_string(width_) +
                      "x" + std::to_string(height_) +
                      (context_reused_ ? " (context reused)" : ""));
    // Re-apply the stage mapping: the stage outlives the window (stage_set_ /
    // sidecut_ survive), and the engine only calls SetStage on (re)boot, not
    // on window/surface changes.
    ApplyViewport();
    return true;
}

// Unbind the EGL surface but keep the context (and its GPU resources) alive so
// a backgrounded game resumes instead of re-booting. Returns false when the
// context could not be preserved (it was destroyed and must be rebuilt).
bool Renderer::ReleaseSurface() {
    ready_ = false;
    swap_paced_ = false;
    window_ = nullptr;
    if (display_ == EGL_NO_DISPLAY) { surface_ = EGL_NO_SURFACE; return false; }
    if (surface_ == EGL_NO_SURFACE) return context_ != EGL_NO_CONTEXT;
    // Detach (surfaceless) before destroying the surface: the context must
    // outlive the window for the GPU state to survive.
    const EGLBoolean detached =
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_);
    eglDestroySurface(display_, surface_);
    surface_ = EGL_NO_SURFACE;
    if (detached != EGL_TRUE) {   // surfaceless current unsupported on this driver
        Shutdown();
        return false;
    }
    return true;
}

bool Renderer::ContextAlive() const {
    return display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT;
}

void Renderer::SetStage(int stage_w, int stage_h, bool sidecut) {
    stage_w_ = stage_w;
    stage_h_ = stage_h;
    stage_set_ = stage_w > 0 && stage_h > 0;
    sidecut_ = sidecut;
    if (!ready_) return;
    ApplyViewport();
}

// Recompute the stage→surface viewport for the current width_/height_ and
// apply it. Requires a current GL context.
void Renderer::ApplyViewport() {
    if (stage_set_) {
        const float sw = (float)width_ / stage_w_, sh = (float)height_ / stage_h_;
        const float scale = sidecut_ ? (sw > sh ? sw : sh)   // crop to cover
                                     : (sw < sh ? sw : sh);  // letterbox to fit
        vp_w_ = stage_w_ * scale;
        vp_h_ = stage_h_ * scale;
        vp_x_ = (width_ - vp_w_) * 0.5f;
        vp_y_ = (height_ - vp_h_) * 0.5f;
    } else {
        vp_x_ = 0;
        vp_y_ = 0;
        vp_w_ = (float)width_;
        vp_h_ = (float)height_;
    }
    glViewport((int)vp_x_, (int)vp_y_, (int)vp_w_, (int)vp_h_);
    Log(kLogInfo, "viewport: " + std::to_string((int)vp_w_) + "x" +
                      std::to_string((int)vp_h_) + " at " +
                      std::to_string((int)vp_x_) + "," + std::to_string((int)vp_y_) +
                      " (surface " + std::to_string(width_) + "x" +
                      std::to_string(height_) + ")");
}

float Renderer::StageX(float win_x) const {
    return (win_x - vp_x_) / vp_w_ * stage_w_;
}
float Renderer::StageY(float win_y) const {
    return (win_y - vp_y_) / vp_h_ * stage_h_;
}

void Renderer::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_) return;
    Log(kLogInfo, "renderer: window resized " + std::to_string(width_) + "x" +
                      std::to_string(height_) + " -> " + std::to_string(width) + "x" +
                      std::to_string(height));
    width_ = width;
    height_ = height;
    if (ready_) ApplyViewport();
}

void Renderer::Clear() {
    if (!ready_) return;
    // stage background; composited layers draw on top of this
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::Present() {
    if (!ready_) return;
    if (!eglSwapBuffers(display_, surface_))
        Log(kLogError, "eglSwapBuffers failed");
}

void Renderer::DrawFrame() {
    Clear();
    Present();
}

void Renderer::Shutdown() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        eglTerminate(display_);
    }
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
    display_ = EGL_NO_DISPLAY;
    ready_ = false;
}

#else // host build: no GL

bool Renderer::Init(void *) { last_error_ = "renderer unavailable on host"; return false; }
bool Renderer::ReleaseSurface() { return false; }
bool Renderer::ContextAlive() const { return false; }
void Renderer::Resize(int, int) {}
void Renderer::SetStage(int, int, bool) {}
void Renderer::ApplyViewport() {}
float Renderer::StageX(float x) const { return x; }
float Renderer::StageY(float y) const { return y; }
void Renderer::Clear() {}
void Renderer::Present() {}
void Renderer::DrawFrame() {}
void Renderer::Shutdown() {}

#endif

} // namespace artc
