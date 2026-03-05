#ifndef FLECS_ENGINE_RENDER_EFFECTS_H
#define FLECS_ENGINE_RENDER_EFFECTS_H

typedef struct {
    float threshold;
    float threshold_softness;
} FlecsBloomPrefilter;

typedef struct {
    float intensity;
    float low_frequency_boost;
    float low_frequency_boost_curvature;
    float high_pass_frequency;
    FlecsBloomPrefilter prefilter;
    uint32_t max_mip_dimension;
    float scale_x;
    float scale_y;
} FlecsBloom;

extern ECS_COMPONENT_DECLARE(FlecsBloom);

ecs_entity_t flecsEngine_createEffect_tonyMcMapFace(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input);

ecs_entity_t flecsEngine_createEffect_invert(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input);

FlecsBloom flecsEngine_bloomSettingsDefault(void);

ecs_entity_t flecsEngine_createEffect_bloom(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsBloom *settings);

#endif
