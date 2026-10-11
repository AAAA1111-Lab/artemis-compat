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

void Renderer::SetEglError(const char *operation) {
    const EGLint error = eglGetError();
    if (error == EGL_CONTEXT_LOST) context_lost_ = true;
    last_error_ = std::string(operation) + " failed: " + std::to_string(error);
    Log(kLogError, last_error_);
}

void Renderer::ApplySwapInterval() {
    // The interval belongs to the current draw surface. Reapply it when a
    // window is attached, including when the existing context is retained.
    const char *swap_env = std::getenv("ARTC_SWAP_INTERVAL");
    if (!(swap_env && std::strcmp(swap_env, "0") == 0)) {
        swap_paced_ = eglSwapInterval(display_, 1) == EGL_TRUE;
        Log(kLogInfo, std::string("renderer: eglSwapInterval(1) ") +
                          (swap_paced_ ? "applied" : "rejected; host-paced"));
    } else {
        swap_paced_ = false;
        Log(kLogInfo, "renderer: swap interval disabled (ARTC_SWAP_INTERVAL=0)");
    }
}

bool Renderer::Init(void *native_window) {
    Shutdown();
    if (!native_window) { last_error_ = "native window is null"; return false; }

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) { SetEglError("eglGetDisplay"); return false; }
    if (!eglInitialize(display_, nullptr, nullptr)) {
        SetEglError("eglInitialize");
        Shutdown();
        return false;
    }

    auto create = [&]() {
        const EGLint cfg_attrs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_NONE};
        EGLint num = 0;
        if (!eglChooseConfig(display_, cfg_attrs, &config_, 1, &num) || num < 1) {
            SetEglError("eglChooseConfig");
            return false;
        }
        const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctx_attrs);
        if (context_ == EGL_NO_CONTEXT) {
            SetEglError("eglCreateContext");
            return false;
        }
        const EGLint parking_attrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        parking_surface_ = eglCreatePbufferSurface(display_, config_, parking_attrs);
        if (parking_surface_ != EGL_NO_SURFACE) {
            if (AttachWindow(native_window)) return true;
        } else {
            SetEglError("eglCreatePbufferSurface");
        }
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (parking_surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, parking_surface_);
        eglDestroyContext(display_, context_);
        surface_ = EGL_NO_SURFACE;
        parking_surface_ = EGL_NO_SURFACE;
        context_ = EGL_NO_CONTEXT;
        window_ = nullptr;
        config_ = nullptr;
        context_lost_ = false;
        return false;
    };
    // A live session never replaces its context to recover a window surface.
    if (!create()) {
        Shutdown();
        return false;
    }
    return true;
}

bool Renderer::HasRetainedContext() const {
    return !context_lost_ && display_ != EGL_NO_DISPLAY &&
           context_ != EGL_NO_CONTEXT && parking_surface_ != EGL_NO_SURFACE;
}

bool Renderer::DetachSurface() {
    ready_ = false;
    swap_paced_ = false;
    if (!HasRetainedContext()) return false;
    // Move the context before destroying the window surface. No compositor,
    // Lua or script-runner state needs to be released for this transition.
    if (!eglMakeCurrent(display_, parking_surface_, parking_surface_, context_)) {
        SetEglError("eglMakeCurrent(parking)");
        return false;
    }
    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }
    window_ = nullptr;
    return true;
}

bool Renderer::AttachWindow(void *native_window) {
    if (!native_window || !HasRetainedContext()) {
        last_error_ = "cannot attach window without a retained EGL context";
        return false;
    }
    if (!DetachSurface()) return false;
    auto *window = static_cast<ANativeWindow *>(native_window);
    EGLSurface candidate = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (candidate == EGL_NO_SURFACE) {
        SetEglError("eglCreateWindowSurface");
        return false; // The context remains current on the parking surface.
    }
    if (!eglMakeCurrent(display_, candidate, candidate, context_)) {
        SetEglError("eglMakeCurrent(window)");
        eglDestroySurface(display_, candidate);
        // On ordinary surface failure EGL leaves the previous binding intact.
        // Do not invoke GL or claim retention after EGL_CONTEXT_LOST.
        return false;
    }
    surface_ = candidate;
    window_ = window;
    width_ = ANativeWindow_getWidth(window);
    height_ = ANativeWindow_getHeight(window);
    EGLint actual_w = 0, actual_h = 0;
    if (!eglQuerySurface(display_, surface_, EGL_WIDTH, &actual_w) ||
        !eglQuerySurface(display_, surface_, EGL_HEIGHT, &actual_h)) {
        SetEglError("eglQuerySurface");
        if (!context_lost_) DetachSurface();
        return false;
    }
    if (actual_w < 1 || actual_h < 1) {
        last_error_ = "window surface has no drawable area";
        DetachSurface();
        return false;
    }
    width_ = actual_w;
    height_ = actual_h;
    glViewport(0, 0, width_, height_);
    ready_ = true;
    ApplySwapInterval();
    last_error_.clear();
    Log(kLogInfo, "renderer: GLES2 ready " +
                      std::to_string(width_) + "x" + std::to_string(height_));
    return true;
}

void Renderer::SetStage(int stage_w, int stage_h, bool sidecut) {
    stage_w_ = stage_w;
    stage_h_ = stage_h;
    if (!ready_ || stage_w <= 0 || stage_h <= 0) return;
    const float sw = (float)width_ / stage_w, sh = (float)height_ / stage_h;
    const float scale = sidecut ? (sw > sh ? sw : sh)   // crop to cover
                                : (sw < sh ? sw : sh);  // letterbox to fit
    vp_w_ = stage_w * scale;
    vp_h_ = stage_h * scale;
    vp_x_ = (width_ - vp_w_) * 0.5f;
    vp_y_ = (height_ - vp_h_) * 0.5f;
    glViewport((int)vp_x_, (int)vp_y_, (int)vp_w_, (int)vp_h_);
    Log(kLogInfo, "viewport: " + std::to_string((int)vp_w_) + "x" +
                      std::to_string((int)vp_h_) + " at " +
                      std::to_string((int)vp_x_) + "," + std::to_string((int)vp_y_));
}

float Renderer::StageX(float win_x) const {
    return (win_x - vp_x_) / vp_w_ * stage_w_;
}
float Renderer::StageY(float win_y) const {
    return (win_y - vp_y_) / vp_h_ * stage_h_;
}

bool Renderer::RefreshSize() {
    if (!ready_) return false;
    EGLint width = 0, height = 0;
    if (!eglQuerySurface(display_, surface_, EGL_WIDTH, &width) ||
        !eglQuerySurface(display_, surface_, EGL_HEIGHT, &height)) {
        SetEglError("eglQuerySurface(resize)");
        ready_ = false;
        return false;
    }
    if (width < 1 || height < 1) {
        last_error_ = "window surface has no drawable area";
        ready_ = false;
        return false;
    }
    if (width == width_ && height == height_) return false;
    Resize(width, height);
    return true;
}

void Renderer::Resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (ready_) glViewport(0, 0, width, height);
}

void Renderer::Clear() {
    if (!ready_) return;
    // stage background; composited layers draw on top of this
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

bool Renderer::Present() {
    if (!ready_) return false;
    if (eglSwapBuffers(display_, surface_)) return true;
    const EGLint error = eglGetError();
    if (error == EGL_CONTEXT_LOST) context_lost_ = true;
    ready_ = false; // Reattach the surface; do not replace a healthy context.
    Log(kLogError, "eglSwapBuffers failed: " + std::to_string(error));
    return false;
}

void Renderer::DrawFrame() {
    Clear();
    Present();
}

bool Renderer::HasCurrentContext() const {
    return !context_lost_ && context_ != EGL_NO_CONTEXT && eglGetCurrentContext() == context_;
}

void Renderer::Shutdown() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (parking_surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, parking_surface_);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        eglTerminate(display_);
    }
    surface_ = EGL_NO_SURFACE;
    parking_surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
    display_ = EGL_NO_DISPLAY;
    config_ = nullptr;
    window_ = nullptr;
    ready_ = false;
    context_lost_ = false;
    swap_paced_ = false;
}

#else // host build: no GL

bool Renderer::Init(void *) { last_error_ = "renderer unavailable on host"; return false; }
bool Renderer::DetachSurface() { return false; }
bool Renderer::AttachWindow(void *) { return false; }
bool Renderer::HasRetainedContext() const { return false; }
void Renderer::Resize(int, int) {}
void Renderer::SetStage(int, int, bool) {}
float Renderer::StageX(float x) const { return x; }
float Renderer::StageY(float y) const { return y; }
void Renderer::Clear() {}
bool Renderer::Present() { return false; }
bool Renderer::RefreshSize() { return false; }
bool Renderer::HasCurrentContext() const { return false; }
void Renderer::DrawFrame() {}
void Renderer::Shutdown() {}

#endif

} // namespace artc
