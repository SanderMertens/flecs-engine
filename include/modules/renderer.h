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

typedef enum {
    FlecsBloomCompositeMode_EnergyConserving = 0,
    FlecsBloomCompositeMode_Additive = 1
} FlecsBloomCompositeMode;

typedef struct {
    float intensity;
    float low_frequency_boost;
    float low_frequency_boost_curvature;
    float high_pass_frequency;
    FlecsBloomPrefilter prefilter;
    FlecsBloomCompositeMode composite_mode;
    uint32_t max_mip_dimension;
    float scale_x;
    float scale_y;
} FlecsBloomSettings;

// Render entities with FlecsMesh, FlecsWorldTransform with lighting
ecs_entity_t flecsEngine_createBatch_mesh(
    ecs_world_t *world);

// Render scalable box primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world);

// Render scalable cone primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_cones(
    ecs_world_t *world);

// Render scalable quad primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world);

// Render scalable triangle primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world);

// Render scalable right triangle primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_right_triangles(
    ecs_world_t *world);

// Render scalable triangle prism primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_triangle_prisms(
    ecs_world_t *world);

// Render scalable right triangle prism primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_right_triangle_prisms(
    ecs_world_t *world);

// Render an infinite-style world grid on the XZ plane through the origin
ecs_entity_t flecsEngine_createBatch_infiniteGrid(
    ecs_world_t *world);

// Render all primitive 3D shapes through a nested batch set
ecs_entity_t flecsEngine_createBatchSet_primitiveShapes(
    ecs_world_t *world);

// Create Tony McMapface post-process effect that reads from chain input index
ecs_entity_t flecsEngine_createEffect_tonyMcMapFace(
    ecs_world_t *world,
    int32_t input);

// Bloom presets
FlecsBloomSettings flecsEngine_bloomSettingsDefault(void);
FlecsBloomSettings flecsEngine_bloomSettingsAnamorphic(void);
FlecsBloomSettings flecsEngine_bloomSettingsOldSchool(void);
FlecsBloomSettings flecsEngine_bloomSettingsScreenBlur(void);

// Create bloom post-process effect that reads from chain input index
ecs_entity_t flecsEngine_createEffect_bloom(
    ecs_world_t *world,
    int32_t input,
    const FlecsBloomSettings *settings);

// Create invert-color post-process effect that reads from chain input index
ecs_entity_t flecsEngine_createEffect_invert(
    ecs_world_t *world,
    int32_t input);

// Render a list of batches and/or nested batch sets in order
ECS_STRUCT(FlecsRenderBatchSet, {
    ecs_vec_t batches;
});

// Render settings for a single camera/light view
ECS_STRUCT(FlecsRenderView, {
    ecs_entity_t camera;
    ecs_entity_t light;
    ecs_vec_t effects;
});

#endif
