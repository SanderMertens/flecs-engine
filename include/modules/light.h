#ifndef FLECS_ENGINE_LIGHT_H
#define FLECS_ENGINE_LIGHT_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_LIGHT_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsDirectionalLight, {
    float intensity;
});

ECS_STRUCT(FlecsPointLight, {
    float intensity;
    float range;
});

ECS_STRUCT(FlecsSpotLight, {
    float intensity;
    float range;
    float inner_angle; /* degrees, full-intensity cone half-angle */
    float outer_angle; /* degrees, falloff cone half-angle */
});

#endif
