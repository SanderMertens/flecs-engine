#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

static flecs_engine_batch_ctx_t* flecsEngine_boxes_createCtx(
    ecs_world_t *world) 
{
    flecs_engine_batch_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_batch_ctx_t);
    flecsEngine_batchCtx_init(result, flecsGeometry3_getBoxAsset(world));
    return result;
}

static void flecsEngine_boxes_deleteCtx(
    void *arg)
{
    flecs_engine_batch_ctx_t *ctx = arg;
    flecsEngine_batchCtx_fini(ctx);
    ecs_os_free(ctx);
}

static void flecsEngine_boxes_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecs_engine_batch_ctx_t *ctx)
{
redo: {
        ecs_iter_t it = ecs_query_iter(world, batch->query);
        ctx->count = 0;

        while (ecs_query_next(&it)) {
            const FlecsBox *boxes = ecs_field(&it, FlecsBox, 0);
            const FlecsWorldTransform3 *wt = ecs_field(&it, FlecsWorldTransform3, 1);
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

            if ((ctx->count + it.count) <= ctx->capacity) {
                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_transform,
                    ctx->count * sizeof(mat4),
                    wt,
                    it.count * sizeof(mat4));

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_color,
                    ctx->count * sizeof(flecs_rgba_t),
                    colors,
                    it.count * sizeof(flecs_rgba_t));

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_size,
                    ctx->count * sizeof(FlecsInstanceSize),
                    boxes,
                    it.count * sizeof(FlecsBox));
            }

            ctx->count += it.count;
        }

        if (ctx->count > ctx->capacity) {
            flecsEngine_batchCtx_ensureCapacity(engine, ctx, ctx->count);
            ecs_assert(ctx->count <= ctx->capacity, ECS_INTERNAL_ERROR, NULL);
            goto redo;
        }
    }
}

static void flecsEngine_boxes_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    flecs_engine_batch_ctx_t *ctx = batch->ctx;
    flecsEngine_boxes_prepareInstances(world, engine, batch, ctx);
    flecsEngine_batchCtx_draw(pass, ctx);
}

ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world)
{
    ecs_entity_t batch = ecs_new(world);
    ecs_entity_t shader = flecsEngineShader_litColored(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsInstanceColor),
            ecs_id(FlecsInstanceSize)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .callback = flecsEngine_boxes_callback,
        .ctx = flecsEngine_boxes_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_boxes_deleteCtx
    });

    return batch;
}
