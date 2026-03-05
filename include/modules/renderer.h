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
    flecs_mat4_t mvp;
    flecs_mat4_t inv_vp;
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

ECS_STRUCT(FlecsRenderBatchSet, {
    ecs_vec_t batches;
});

ECS_STRUCT(FlecsHdri, {
    const char *file;
    uint32_t filter_sample_count;
    uint32_t lut_sample_count;
});

extern ECS_COMPONENT_DECLARE(FlecsHdri);

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
    const char *file,
    uint32_t filterSampleCount,
    uint32_t lutSampleCount);

ecs_entity_t flecsEngine_createEffect_passthrough(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input);

#endif
