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
    flecs_vec3_t p;
    flecs_vec3_t n;
    flecs_vec2_t uv;
} FlecsLitVertexUv;

extern ECS_COMPONENT_DECLARE(FlecsLitVertexUv);

typedef struct {
    flecs_vec3_t c0;
    flecs_vec3_t c1;
    flecs_vec3_t c2;
    flecs_vec3_t c3;
} FlecsInstanceTransform;

extern ECS_COMPONENT_DECLARE(FlecsInstanceTransform);

#define FLECS_ENGINE_SHADOW_CASCADE_COUNT 4
#define FLECS_ENGINE_CLUSTER_X 16
#define FLECS_ENGINE_CLUSTER_Y 9
#define FLECS_ENGINE_CLUSTER_Z 24
#define FLECS_ENGINE_CLUSTER_TOTAL \
    (FLECS_ENGINE_CLUSTER_X * FLECS_ENGINE_CLUSTER_Y * FLECS_ENGINE_CLUSTER_Z)

typedef struct {
    float position[4];  /* xyz = position, w = range */
    float direction[4]; /* xyz = direction, w = outer_cos (-2 = point light) */
    float color[4];     /* rgb = color * intensity, w = inner_cos */
} FlecsGpuLight;

typedef struct {
    uint32_t grid_size[4];   /* x, y, z, total */
    float screen_info[4];    /* width, height, near, log(far/near) */
} FlecsClusterInfo;

typedef struct {
    uint32_t light_offset;
    uint32_t light_count;
    uint32_t _pad0;
    uint32_t _pad1;
} FlecsClusterEntry;

typedef struct {
    flecs_mat4_t mvp;
    flecs_mat4_t inv_vp;
    flecs_mat4_t light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    float cascade_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    float sky_color[4];
    float light_ray_dir[4];
    float light_color[4];
    float camera_pos[4];
    float shadow_info[4];
    float shadow_cascade_scales[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    float ambient_light[4];
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

ECS_STRUCT(flecs_engine_shadow_params_t, {
    ecs_bool_t enabled;
    int32_t map_size;
    float bias;
    float max_range;
});

ECS_STRUCT(flecs_engine_background_t, {
    flecs_rgba_t sky_color;
    flecs_rgba_t ground_color;
    flecs_rgba_t haze_color;
    flecs_rgba_t horizon_color;
    ecs_bool_t ibl;
});

ECS_STRUCT(flecs_render_view_effect_t, {
    ecs_bool_t enabled;
    ecs_entity_t effect;
});

ECS_STRUCT(FlecsRenderView, {
    ecs_entity_t camera;
    ecs_entity_t light;
    ecs_entity_t hdri;
    flecs_rgba_t ambient_light;
    flecs_engine_background_t background;
    flecs_engine_shadow_params_t shadow;
    ecs_vec_t effects;
});

ecs_entity_t flecsEngine_createHdri(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *file,
    uint32_t filterSampleCount,
    uint32_t lutSampleCount);

#endif
