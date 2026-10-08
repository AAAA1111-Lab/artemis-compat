// Optional host presentation adapter. This header is also a versioned C ABI.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ARTC_PRESENTATION_ABI_V1 = 1,
    ARTC_PRESENTATION_PREFER_ES3 = 1u << 0,
    ARTC_PRESENTATION_OBSERVE = 1u << 1,
    ARTC_CONTEXT_CREATED = 1,
    ARTC_CONTEXT_DESTROYING = 2,
    ARTC_CONTEXT_LOST = 3,
    ARTC_SURFACE_RESIZED = 4,
    ARTC_RENDER_THREAD_STOPPING = 5
};

typedef struct ArtcPresentationFrameV1 {
    uint32_t struct_size;
    uint32_t source_texture; // Borrowed GL_TEXTURE_2D; never overwrite/delete it.
    int32_t source_width, source_height;
    uint32_t target_framebuffer;
    int32_t viewport_x, viewport_y, viewport_width, viewport_height;
    uint64_t revision, context_generation;
    uint32_t bottom_up; // v=0 is the bottom row for the retained scene.
} ArtcPresentationFrameV1;

typedef struct ArtcPresentationCallbacksV1 {
    uint32_t struct_size, abi_version, flags;
    void* userdata;
    // Called synchronously on the current render context. 0/source means pass
    // through; another texture is host-owned, covers the same full stage and
    // orientation (dimensions may differ for display enlargement), and
    // must remain valid through the final presentation draw. The engine never
    // deletes a returned texture. Restore all changed GL state before returning.
    uint32_t (*filter)(void*, const ArtcPresentationFrameV1*);
    // All events run on the render thread. DESTROYING has a current context;
    // LOST must only abandon old handles. Registration survives scene resets.
    void (*event)(void*, uint32_t event, int32_t gles_version,
                  uint64_t generation, int32_t surface_width, int32_t surface_height);
    // Only a successful actual swap counts as a presented frame.
    void (*presented)(void*, uint64_t generation);
} ArtcPresentationCallbacksV1;

// Optional Android host export. Register once before NativeActivity_onCreate;
// the callback structure is copied. No default engine behavior changes.
typedef int (*ArtcRegisterPresentationV1)(const ArtcPresentationCallbacksV1*);
int artc_register_presentation_v1(const ArtcPresentationCallbacksV1* callbacks);

#ifdef __cplusplus
}
#endif
