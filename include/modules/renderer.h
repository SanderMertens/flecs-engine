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
    mat4 m;
} FlecsInstanceTransform;

extern ECS_COMPONENT_DECLARE(FlecsInstanceTransform);

typedef struct {
    flecs_rgba_t color;
} FlecsInstanceColor;

extern ECS_COMPONENT_DECLARE(FlecsInstanceColor);

typedef struct {
    flecs_vec3_t size;
} FlecsInstanceSize;

extern ECS_COMPONENT_DECLARE(FlecsInstanceSize);

typedef struct {
    flecs_mat4_t mvp;
} FlecsUniform;

extern ECS_COMPONENT_DECLARE(FlecsUniform);

typedef struct {
    const char *source;
    const char *vertex_entry;
    const char *fragment_entry;
} FlecsShader;

extern ECS_COMPONENT_DECLARE(FlecsShader);

// Render entities with FlecsMesh, FlecsWorldTransform with lighting
ecs_entity_t flecsEngine_createBatch_litColoredGeometry(
    ecs_world_t *world);

// Render scalable box primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world);

// Render scalable pyramid primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_pyramids(
    ecs_world_t *world);

// Render scalable quad primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world);

// Render scalable triangle primitives without IsA grouping
ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world);

// Render a list of batches in order
ECS_STRUCT(FlecsRenderView, {
    ecs_entity_t camera;
    ecs_vec_t batches;
});

#endif
