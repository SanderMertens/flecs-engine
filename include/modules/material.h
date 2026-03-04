#ifndef FLECS_ENGINE_MATERIAL_H
#define FLECS_ENGINE_MATERIAL_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_MATERIAL_IMPL
#define ECS_META_IMPL EXTERN
#endif

typedef flecs_rgba_t FlecsRgba;

extern ECS_COMPONENT_DECLARE(FlecsRgba);

ECS_STRUCT(FlecsPbrMaterial, {
    float metallic;
    float roughness;
});

extern ECS_COMPONENT_DECLARE(FlecsPbrMaterial);

ECS_STRUCT(FlecsEmissive, {
    flecs_rgba_t color;
    float strength;
});

typedef FlecsEmissive EcsEmissive;

extern ECS_COMPONENT_DECLARE(FlecsEmissive);

ECS_STRUCT(FlecsMaterialId, {
    uint32_t value;
});

extern ECS_COMPONENT_DECLARE(FlecsMaterialId);

#endif
