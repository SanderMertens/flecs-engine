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
    flecs_mat4_t mvp;
} FlecsUniform;

extern ECS_COMPONENT_DECLARE(FlecsUniform);

// Render entities with FlecsMesh, FlecsWorldTransform with lighting
ecs_entity_t flecsEngine_createBatch_litColoredGeometry(
    ecs_world_t *world);

// Render a list of batches in order
ECS_STRUCT(FlecsRenderView, {
    ecs_entity_t camera;
    ecs_vec_t batches;
});

#endif
