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
    uint32_t mip_count;
    float scale_x;
    float scale_y;
} FlecsBloom;

extern ECS_COMPONENT_DECLARE(FlecsBloom);

ECS_STRUCT(FlecsExponentialHeightFog, {
    float density;
    float falloff;
    float base_height;
    float max_opacity;
    flecs_rgba_t color;
});

extern ECS_COMPONENT_DECLARE(FlecsExponentialHeightFog);

ECS_STRUCT(FlecsSSAO, {
    float radius;
    float bias;
    float intensity;
    int32_t blur;
});

extern ECS_COMPONENT_DECLARE(FlecsSSAO);

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

FlecsExponentialHeightFog flecsEngine_exponentialHeightFogSettingsDefault(void);

ecs_entity_t flecsEngine_createEffect_exponentialHeightFog(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsExponentialHeightFog *settings);

FlecsSSAO flecsEngine_ssaoSettingsDefault(void);

ecs_entity_t flecsEngine_createEffect_ssao(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsSSAO *settings);

ecs_entity_t flecsEngine_createEffect_gammaCorrect(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input);

#endif
