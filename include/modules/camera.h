#ifndef FLECS_ENGINE_CAMERA_H
#define FLECS_ENGINE_CAMERA_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_CAMERA_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsCamera, {
    float fov;
    float near_;
    float far_;
    float aspect_ratio;
    bool orthographic;
});

#endif
