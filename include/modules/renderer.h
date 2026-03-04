#ifndef FLECS_ENGINE_RENDERER_H
#define FLECS_ENGINE_RENDERER_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_RENDERER_IMPL
#define ECS_META_IMPL EXTERN
#endif

typedef struct {
    flecs_vec3_t p;
} FlecsVertex;

extern ECS_COMPONENT_DECLARE(FlecsVertex);

typedef struct {
    flecs_vec3_t p;
    flecs_vec3_t n;
} FlecsLitVertex;

extern ECS_COMPONENT_DECLARE(FlecsLitVertex);

typedef struct {
    flecs_vec3_t c0;
    flecs_vec3_t c1;
    flecs_vec3_t c2;
    flecs_vec3_t c3;
} FlecsInstanceTransform;

extern ECS_COMPONENT_DECLARE(FlecsInstanceTransform);

typedef struct {
    flecs_rgba_t color;
} FlecsInstanceColor;

extern ECS_COMPONENT_DECLARE(FlecsInstanceColor);

typedef struct {
    float metallic;
    float roughness;
} FlecsInstancePbrMaterial;

extern ECS_COMPONENT_DECLARE(FlecsInstancePbrMaterial);

typedef struct {
    uint32_t value;
} FlecsInstanceMaterialId;

extern ECS_COMPONENT_DECLARE(FlecsInstanceMaterialId);

typedef struct {
    flecs_mat4_t mvp;
    float clear_color[4];
    float light_ray_dir[4];
    float light_color[4];
    float camera_pos[4];
} FlecsUniform;

extern ECS_COMPONENT_DECLARE(FlecsUniform);

typedef struct {
    const char *source;
    const char *vertex_entry;
    const char *fragment_entry;
} FlecsShader;

extern ECS_COMPONENT_DECLARE(FlecsShader);

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

ECS_STRUCT(FlecsRenderBatchSet, {
    ecs_vec_t batches;
});

ECS_STRUCT(FlecsIbl, {
    const char *file;
});

extern ECS_COMPONENT_DECLARE(FlecsIbl);

ECS_STRUCT(FlecsRenderView, {
    ecs_entity_t camera;
    ecs_entity_t light;
    ecs_entity_t hdri;
    ecs_vec_t effects;
});

ecs_entity_t flecsEngine_createHdri(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *file);

ecs_entity_t flecsEngine_createBatch_infiniteGrid(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatchSet_primitiveShapes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatchSet_primitiveShapes_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

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

ecs_entity_t flecsEngine_createEffect_passthrough(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input);

#endif
