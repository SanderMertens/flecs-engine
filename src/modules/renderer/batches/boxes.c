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
            const FlecsPbrMaterial *materials =
                ecs_field(&it, FlecsPbrMaterial, 3);

            if ((ctx->count + it.count) <= ctx->capacity) {
                for (int32_t i = 0; i < it.count; i ++) {
                    int32_t index = ctx->count + i;
                    flecsEngine_packInstanceTransform(
                        &ctx->cpu_transforms[index],
                        &wt[i],
                        boxes[i].x,
                        boxes[i].y,
                        boxes[i].z);

                }

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_transform,
                    (uint64_t)ctx->count * sizeof(FlecsInstanceTransform),
                    &ctx->cpu_transforms[ctx->count],
                    (uint64_t)it.count * sizeof(FlecsInstanceTransform));

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_color,
                    ctx->count * sizeof(flecs_rgba_t),
                    colors,
                    it.count * sizeof(flecs_rgba_t));

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_pbr,
                    (uint64_t)ctx->count * sizeof(FlecsInstancePbrMaterial),
                    materials,
                    (uint64_t)it.count * sizeof(FlecsInstancePbrMaterial));
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
    ecs_entity_t shader = flecsEngineShader_pbrColored(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf }
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
            ecs_id(FlecsInstancePbrMaterial)
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
