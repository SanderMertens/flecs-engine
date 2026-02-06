#include "../../types.h"

#ifndef FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_RENDERER_IMPL_IMPL
#define ECS_META_IMPL EXTERN
#endif

struct FlecsRenderBatch;

typedef void (*flecs_render_batch_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const struct FlecsRenderBatch *batch);

// Render entities matching a query with specified shader
ECS_STRUCT(FlecsRenderBatch, {
    const char *shader;
    ecs_query_t *query;
    ecs_entity_t vertex_type;
    ecs_entity_t instance_types[FLECS_ENGINE_INSTANCE_TYPES_MAX];
    ecs_entity_t uniforms[FLECS_ENGINE_UNIFORMS_MAX];
ECS_PRIVATE
    flecs_render_batch_callback callback;
});

void FlecsRenderBatch_on_set(
    ecs_iter_t *it);

void flecsEngineRenderViews(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder);

void flecsEngineRenderView(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view);

void flecsEngineRenderBatch(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    const FlecsRenderBatch *batch,
    const FlecsRenderBatchImpl *batch_impl);

void FlecsEngineRendererImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

#endif
