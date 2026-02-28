#include "../../types.h"

#ifndef FLECS_ENGINE_IMPL
#define FLECS_ENGINE_IMPL

typedef struct FlecsEngineSurface {
    WGPUTextureView view_texture;
    WGPUTexture surface_texture;
    bool owns_view_texture;
    WGPUSurfaceGetCurrentTextureStatus surface_status;
    WGPUBuffer readback_buffer;
    uint32_t readback_bytes_per_row;
    uint64_t readback_buffer_size;
} FlecsEngineSurface;

typedef struct FlecsEngineSurfaceInterface {
    int (*init_instance)(
        FlecsEngineImpl *impl,
        const void *config);
    int (*configure_target)(
        FlecsEngineImpl *impl);
    int (*prepare_frame)(
        ecs_world_t *world,
        FlecsEngineImpl *impl);
    int (*acquire_frame)(
        FlecsEngineImpl *impl,
        FlecsEngineSurface *target);
    int (*encode_frame)(
        FlecsEngineImpl *impl,
        WGPUCommandEncoder encoder,
        FlecsEngineSurface *target);
    int (*submit_frame)(
        ecs_world_t *world,
        FlecsEngineImpl *impl,
        const FlecsEngineSurface *target);
    void (*on_frame_failed)(
        ecs_world_t *world,
        FlecsEngineImpl *impl);
    void (*cleanup)(
        FlecsEngineImpl *impl,
        bool terminate_runtime);
} FlecsEngineSurfaceInterface;

typedef struct FlecsEngineOutputDesc {
    const FlecsEngineSurfaceInterface *ops;
    const void *config;
    int32_t width;
    int32_t height;
    flecs_rgba_t clear_color;
} FlecsEngineOutputDesc;

int flecsEngineInit(
    ecs_world_t *world,
    const FlecsEngineOutputDesc *output);

#endif
