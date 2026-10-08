// Exercise the production Android Renderer against synthetic EGL/GL calls.
// No device, display server, commercial assets or actual GL driver is used.
#include "render/renderer.h"
#include "log/logger.h"
#include <cstdlib>
#include <iostream>
#include <unordered_map>

namespace {
void Require(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
struct Surface { bool parking; int width; int height; };
struct EglMock {
    uintptr_t serial = 10;
    int created_contexts = 0, destroyed_contexts = 0;
    int created_pbuffers = 0, swap_intervals = 0;
    int selected_surface_bits = 0, viewport_calls = 0;
    int fail_create_version = 0;
    EGLint error = EGL_SUCCESS, next_window_error = EGL_SUCCESS;
    EGLint next_bind_error = EGL_SUCCESS, next_swap_error = EGL_SUCCESS;
    EGLint next_query_error = EGL_SUCCESS;
    EGLContext current = EGL_NO_CONTEXT;
    EGLSurface draw = EGL_NO_SURFACE;
    std::unordered_map<EGLSurface, Surface> surfaces;
    void *Id() { return reinterpret_cast<void *>(++serial); }
} mock;
EGLint Attr(const EGLint *attrs, EGLint key) {
    for (int i = 0; attrs && attrs[i] != EGL_NONE; i += 2)
        if (attrs[i] == key) return attrs[i + 1];
    return 0;
}
void Reset() { mock = EglMock{}; }
}
namespace artc { void Log(int, const std::string &) {} }
EGLDisplay eglGetDisplay(void *) { return reinterpret_cast<void *>(1); }
EGLBoolean eglInitialize(EGLDisplay, EGLint *, EGLint *) { return EGL_TRUE; }
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint *attrs, EGLConfig *cfg, EGLint, EGLint *num) {
    mock.selected_surface_bits = Attr(attrs, EGL_SURFACE_TYPE);
    *cfg = reinterpret_cast<void *>(2); *num = 1; return EGL_TRUE;
}
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint *attrs) {
    if (Attr(attrs, EGL_CONTEXT_CLIENT_VERSION) == mock.fail_create_version) {
        mock.error = EGL_BAD_MATCH; return EGL_NO_CONTEXT;
    }
    ++mock.created_contexts; return mock.Id();
}
EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const EGLint *attrs) {
    ++mock.created_pbuffers;
    auto id = mock.Id();
    mock.surfaces.emplace(id, Surface{true, Attr(attrs, EGL_WIDTH), Attr(attrs, EGL_HEIGHT)});
    return id;
}
EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType w, const EGLint *) {
    if (mock.next_window_error != EGL_SUCCESS) {
        mock.error = mock.next_window_error; mock.next_window_error = EGL_SUCCESS;
        return EGL_NO_SURFACE;
    }
    auto *window = static_cast<ANativeWindow *>(w);
    auto id = mock.Id(); mock.surfaces.emplace(id, Surface{false, window->width, window->height});
    return id;
}
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface draw, EGLSurface, EGLContext context) {
    if (mock.next_bind_error != EGL_SUCCESS) {
        mock.error = mock.next_bind_error; mock.next_bind_error = EGL_SUCCESS;
        return EGL_FALSE;
    }
    mock.current = context; mock.draw = draw; return EGL_TRUE;
}
EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface surface) {
    Require(mock.draw != surface, "a current draw surface was destroyed before parking");
    mock.surfaces.erase(surface); return EGL_TRUE;
}
EGLBoolean eglDestroyContext(EGLDisplay, EGLContext context) {
    Require(mock.current != context, "a current context was destroyed without unbinding");
    ++mock.destroyed_contexts; return EGL_TRUE;
}
EGLBoolean eglTerminate(EGLDisplay) { return EGL_TRUE; }
EGLBoolean eglSwapInterval(EGLDisplay, EGLint) { ++mock.swap_intervals; return EGL_TRUE; }
EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface surface) {
    Require(!mock.surfaces.at(surface).parking, "a parking pbuffer was presented");
    if (mock.next_swap_error != EGL_SUCCESS) {
        mock.error = mock.next_swap_error; mock.next_swap_error = EGL_SUCCESS;
        return EGL_FALSE;
    }
    return EGL_TRUE;
}
EGLBoolean eglQuerySurface(EGLDisplay, EGLSurface surface, EGLint key, EGLint *result) {
    if (mock.next_query_error != EGL_SUCCESS) {
        mock.error = mock.next_query_error; mock.next_query_error = EGL_SUCCESS;
        return EGL_FALSE;
    }
    auto entry = mock.surfaces.at(surface);
    *result = key == EGL_WIDTH ? entry.width : entry.height;
    return EGL_TRUE;
}
EGLContext eglGetCurrentContext() { return mock.current; }
EGLint eglGetError() { EGLint value = mock.error; mock.error = EGL_SUCCESS; return value; }
int ANativeWindow_getWidth(ANativeWindow *w) { return w->width; }
int ANativeWindow_getHeight(ANativeWindow *w) { return w->height; }
void glViewport(GLint, GLint, GLsizei, GLsizei) { ++mock.viewport_calls; }
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glClear(GLbitfield) {}

int main() {
    ANativeWindow first{1280, 720}, second{2133, 1200};
    {
        Reset(); artc::Renderer r;
        Require(r.Init(&first, 3), "initial ES3 window must initialize");
        auto context = mock.current;
        Require(r.GlesVersion() == 3 && r.Ready(), "preferred ES3 was not retained");
        Require(mock.selected_surface_bits == (EGL_WINDOW_BIT | EGL_PBUFFER_BIT), "config must support parking and windows");
        Require(r.DetachSurface(), "normal window loss must park the context");
        Require(!r.Ready() && r.HasCurrentContext() && r.HasRetainedContext(), "parking must keep context usable, presentation unavailable");
        Require(mock.surfaces.size() == 1 && mock.surfaces.at(mock.draw).parking, "window surface must be destroyed after parking");
        Require(r.AttachWindow(&second), "resume must reattach another window");
        Require(mock.current == context && mock.created_contexts == 1 && mock.destroyed_contexts == 0, "window replacement must not recreate the GL context");
        Require(r.Width() == 2133 && r.Height() == 1200, "reattach must refresh surface geometry");
        Require(mock.swap_intervals == 2, "new draw surface must reapply its swap interval");
        r.SetStage(1280, 720, false);
        Require(r.Present(), "reattached window should present");
        r.Shutdown();
        Require(mock.surfaces.empty() && mock.destroyed_contexts == 1, "shutdown must release window, parking surface and context");
        Require(!r.HasRetainedContext() && !r.Ready(), "shutdown must invalidate retention");
    }
    {
        Reset(); artc::Renderer r;
        Require(r.Init(&first), "default ES2 window must initialize");
        Require(r.GlesVersion() == 2, "default host GL version must stay ES2");
        auto context = mock.current;
        Require(r.DetachSurface(), "detach before failed attach");
        mock.next_window_error = EGL_BAD_NATIVE_WINDOW;
        Require(!r.AttachWindow(&second), "unavailable window must reject attachment");
        Require(r.HasRetainedContext() && r.HasCurrentContext() && !r.ContextLost(), "window failure must retain parking context for retry");
        Require(r.AttachWindow(&second) && mock.current == context && mock.created_contexts == 1, "window retry must preserve context");
        mock.next_swap_error = EGL_BAD_NATIVE_WINDOW;
        Require(!r.Present() && !r.Ready() && !r.ContextLost(), "surface swap failure must require reattachment only");
        Require(r.AttachWindow(&first) && mock.current == context, "same-window recovery must preserve context");
        r.Shutdown();
    }
    {
        Reset(); mock.fail_create_version = 3; artc::Renderer r;
        Require(r.Init(&first, 3) && r.GlesVersion() == 2, "initial ES3 failure must retain the ES2 fallback");
        r.Shutdown();
    }
    {
        Reset(); artc::Renderer r;
        Require(r.Init(&first), "context-loss fixture init");
        mock.next_swap_error = EGL_CONTEXT_LOST;
        Require(!r.Present() && r.ContextLost(), "real context loss must be reported");
        Require(!r.HasRetainedContext() && !r.HasCurrentContext(), "lost context cannot be used as valid retained state");
        Require(!r.AttachWindow(&second) && mock.created_contexts == 1, "attachment cannot silently rebuild a lost context");
        r.Shutdown();
    }
    {
        Reset(); artc::Renderer r;
        Require(r.Init(&first), "parking-loss fixture init");
        mock.next_bind_error = EGL_CONTEXT_LOST;
        Require(!r.DetachSurface() && r.ContextLost(), "loss during parking bind must propagate");
        Require(!r.HasRetainedContext(), "failed parking after loss cannot claim retention");
        r.Shutdown();
    }
    {
        Reset(); artc::Renderer r;
        Require(r.Init(&first), "query-loss fixture init");
        Require(r.DetachSurface(), "query-loss fixture detach");
        int before = mock.viewport_calls;
        mock.next_query_error = EGL_CONTEXT_LOST;
        Require(!r.AttachWindow(&second) && r.ContextLost(), "surface query context loss must propagate");
        Require(mock.viewport_calls == before, "no GL work is allowed after query reports context loss");
        r.Shutdown();
    }
    {
        Reset(); artc::Renderer r;
        Require(r.Init(&first), "resize-loss fixture init");
        int before = mock.viewport_calls;
        mock.next_query_error = EGL_CONTEXT_LOST;
        Require(!r.RefreshSize() && !r.Ready() && r.ContextLost(), "size refresh must expose lost context and disable rendering");
        Require(mock.viewport_calls == before, "failed size query cannot resize through a lost context");
        r.Shutdown();
    }
    std::cout << "android renderer lifecycle regressions passed\n";
}
