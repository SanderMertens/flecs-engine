#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_batch_t batch;
    flecsEngine_batch_buffers_t buffers;
    FlecsWorldTransform3 transform;
    FlecsRgba color;
    ecs_entity_t view_entity;
} flecs_engine_infinite_plane_ctx_t;

static flecs_engine_infinite_plane_ctx_t* flecsEngine_infinite_plane_createCtx(
    ecs_world_t *world,
    ecs_entity_t view_entity)
{
    flecs_engine_infinite_plane_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_infinite_plane_ctx_t);
    flecsEngine_batch_buffers_init(&result->buffers, true);
    flecsEngine_batch_init(&result->batch, world,
        flecsEngine_quad_getAsset(world), 0, true, 0, NULL);
    result->batch.buffers = &result->buffers;
    glm_mat4_identity(result->transform.m);
    result->color = (FlecsRgba){140, 135, 125, 255};
    result->view_entity = view_entity;
    return result;
}

static void flecsEngine_infinite_plane_deleteCtx(
    void *arg)
{
    flecs_engine_infinite_plane_ctx_t *ctx = arg;
    flecsEngine_batch_fini(&ctx->batch);
    flecsEngine_batch_buffers_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static void flecsEngine_infinite_plane_extractCallback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    flecs_engine_infinite_plane_ctx_t *ctx = batch->ctx;
    const FlecsRenderView *view = ecs_get(world, ctx->view_entity, FlecsRenderView);
    if (view) {
        ctx->color = view->background.ground_color;
        if (!ctx->color.a) {
            ctx->color.a = 255;
        }
    }
    flecsEngine_batch_extractSingleInstance(
        engine, &ctx->batch, &ctx->transform, &ctx->color,
        2.0f, 2.0f, 1.0f);
}

static void flecsEngine_infinite_plane_renderCallback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)engine;
    flecs_engine_infinite_plane_ctx_t *ctx = batch->ctx;
    flecsEngine_batch_draw(pass, &ctx->batch);
}

ecs_entity_t flecsEngine_createBatch_infinitePlane(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_infinitePlane(world);

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsRgba)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .extract_callback = flecsEngine_infinite_plane_extractCallback,
        .callback = flecsEngine_infinite_plane_renderCallback,
        .ctx = flecsEngine_infinite_plane_createCtx((ecs_world_t*)world, parent),
        .free_ctx = flecsEngine_infinite_plane_deleteCtx
    });

    ecs_add(world, batch, FlecsGroundPlaneBatch);

    return batch;
}
