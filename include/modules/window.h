#ifndef FLECS_ENGINE_WINDOW_H
#define FLECS_ENGINE_WINDOW_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_WINDOW_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsWindow, {
    int32_t width;
    int32_t height;
    const char *title;
});

#endif
