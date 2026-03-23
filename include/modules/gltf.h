#ifndef FLECS_ENGINE_GLTF_H
#define FLECS_ENGINE_GLTF_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_GLTF_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsGltf, {
    const char *file;
});

extern ECS_COMPONENT_DECLARE(FlecsGltf);

#endif
