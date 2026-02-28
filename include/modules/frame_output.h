#ifndef FLECS_ENGINE_FRAME_OUTPUT_H
#define FLECS_ENGINE_FRAME_OUTPUT_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_FRAME_OUTPUT_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsFrameOutput, {
    int32_t width;
    int32_t height;
    const char *path;
    flecs_rgba_t clear_color;
});

#endif
