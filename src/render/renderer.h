// renderer.h — EGL/GLES2 render surface for the compat engine.
//
// M2.0 scope: context lifecycle (init per native window, resize, shutdown)
// and frame presentation (clear + swap). Layer compositing and texture
// drawing land in M2.1 on top of this.
//
// Host builds get a no-op implementation (no EGL) so the CLI keeps working.
#pragma once

#include <string>

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#endif

namespace artc {

class Renderer {
public:
    // Create EGL/GLES context bound to the native window.
    bool Init(void *native_window);
    // Window loss (background / surface recreation): unbind the current EGL
    // surface but keep the EGL context, so the engine's GPU resources (layer
    // textures, glyph pages, FBOs) survive and the game resumes in place
    // instead of re-booting. Returns true when the context was preserved;
    // false when it had to be destroyed (the caller must then rebuild its GL
    // state via the boot chain).
    bool ReleaseSurface();
    // True while a live EGL context exists that can still host previously
    // created GPU resources.
    bool ContextAlive() const;
    // The most recent Init re-bound a surviving context (true) instead of
    // building a fresh one (false — the previous GPU state is gone).
    bool ContextReused() const { return context_reused_; }
    // Called when the window geometry changes (in-place relayout of the same
    // surface, e.g. a buffer-geometry change the activity never reports):
    // re-applies the stage viewport so the letterbox follows the new size.
    void Resize(int width, int height);
    // Clear the backbuffer (stage background).
    void Clear();
    // Swap the backbuffer to the display.
    void Present();
    // True when eglSwapBuffers paces the frame loop to the display refresh
    // (eglSwapInterval(1)); false when the host must pace itself (fallback
    // build/device: ARTC_SWAP_INTERVAL=0).
    bool SwapPaced() const { return swap_paced_; }
    // Present one frame (clear + swap).
    void DrawFrame();
    // Release the context/surface.
    void Shutdown();

    bool Ready() const { return ready_; }
    // Stage→surface mapping: letterbox/crop viewport + touch conversion.
    void SetStage(int stage_w, int stage_h, bool sidecut);
    float StageX(float win_x) const;   // window px → stage coords
    float StageY(float win_y) const;
    int Width() const { return width_; }
    int Height() const { return height_; }
    const std::string &LastError() const { return last_error_; }

private:
#if defined(__ANDROID__)
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig cfg_ = nullptr;   // kept so a surface can be rebuilt on the context
    void *window_ = nullptr;
    bool CreateContext();                       // display + config + context
    bool CreateSurface(ANativeWindow *window);  // EGLSurface on the context
#endif
    bool ready_ = false;
    bool swap_paced_ = false;   // eglSwapInterval(1) applied successfully
    bool context_reused_ = false; // last Init re-bound a surviving context
    int width_ = 0, height_ = 0;
    float vp_x_ = 0, vp_y_ = 0, vp_w_ = 1, vp_h_ = 1;   // viewport in surface px
    int stage_w_ = 1280, stage_h_ = 720;
    bool stage_set_ = false;    // SetStage ran; survives window loss/re-init
    bool sidecut_ = false;      // crop-to-cover instead of letterbox-to-fit
    void ApplyViewport();       // glViewport from width_/height_ + stage state
    std::string last_error_;
};

} // namespace artc
