#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecs_engine_batch_ctx_t batch;
    FlecsInstanceTransform transform;
    FlecsInstanceColor color;
    FlecsInstanceSize size;
} flecs_engine_infinite_grid_ctx_t;

static flecs_engine_infinite_grid_ctx_t* flecsEngine_infinite_grid_createCtx(
    ecs_world_t *world)
{
    flecs_engine_infinite_grid_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_infinite_grid_ctx_t);
    flecsEngine_batchCtx_init(&result->batch, flecsGeometry3_getQuadAsset(world));

    glm_mat4_identity(result->transform.m);
    glm_rotate(result->transform.m, -glm_rad(90.0f), (vec3){1.0f, 0.0f, 0.0f});
    result->color.color = (flecs_rgba_t){96, 96, 112, 255};
    result->size.size = (flecs_vec3_t){2000.0f, 2000.0f, 1.0f};

    return result;
}

static void flecsEngine_infinite_grid_deleteCtx(
    void *arg)
{
    flecs_engine_infinite_grid_ctx_t *ctx = arg;
    flecsEngine_batchCtx_fini(&ctx->batch);
    ecs_os_free(ctx);
}

static void flecsEngine_infinite_grid_prepareInstance(
    const FlecsEngineImpl *engine,
    flecs_engine_infinite_grid_ctx_t *ctx)
{
    if (ctx->batch.capacity < 1) {
        flecsEngine_batchCtx_ensureCapacity(engine, &ctx->batch, 1);
    }

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->batch.instance_transform,
        0,
        &ctx->transform,
        sizeof(ctx->transform));

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->batch.instance_color,
        0,
        &ctx->color,
        sizeof(ctx->color));

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->batch.instance_size,
        0,
        &ctx->size,
        sizeof(ctx->size));

    ctx->batch.count = 1;
}

static void flecsEngine_infinite_grid_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecs_engine_infinite_grid_ctx_t *ctx = batch->ctx;
    flecsEngine_infinite_grid_prepareInstance(engine, ctx);
    flecsEngine_batchCtx_draw(pass, &ctx->batch);
}

ecs_entity_t flecsEngine_createBatch_infinite_grid(
    ecs_world_t *world)
{
    ecs_entity_t batch = ecs_new(world);
    ecs_entity_t shader = flecsEngineShader_infiniteGrid(world);

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsInstanceColor),
            ecs_id(FlecsInstanceSize)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .callback = flecsEngine_infinite_grid_callback,
        .ctx = flecsEngine_infinite_grid_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_infinite_grid_deleteCtx
    });

    return batch;
}
