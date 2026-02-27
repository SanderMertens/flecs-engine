#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecs_engine_batch_ctx_t batch;
    FlecsInstanceSize *cpu_sizes;
} flecs_engine_triangles_ctx_t;

static flecs_engine_triangles_ctx_t* flecsEngine_triangles_createCtx(
    ecs_world_t *world)
{
    flecs_engine_triangles_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_triangles_ctx_t);
    flecsEngine_batchCtx_init(&result->batch, flecsGeometry3_getTriangleAsset(world));
    return result;
}

static void flecsEngine_triangles_deleteCtx(
    void *arg)
{
    flecs_engine_triangles_ctx_t *ctx = arg;
    flecsEngine_batchCtx_fini(&ctx->batch);
    if (ctx->cpu_sizes) {
        ecs_os_free(ctx->cpu_sizes);
    }

    ecs_os_free(ctx);
}

static void flecsEngine_triangles_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecs_engine_triangles_ctx_t *ctx,
    int32_t count)
{
    int32_t prev_capacity = ctx->batch.capacity;
    flecsEngine_batchCtx_ensureCapacity(engine, &ctx->batch, count);

    if (ctx->batch.capacity != prev_capacity) {
        if (ctx->cpu_sizes) {
            ecs_os_free(ctx->cpu_sizes);
        }
        ctx->cpu_sizes = ecs_os_malloc_n(FlecsInstanceSize, ctx->batch.capacity);
    }
}

static void flecsEngine_triangles_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecs_engine_triangles_ctx_t *ctx)
{
redo: {
        ecs_iter_t it = ecs_query_iter(world, batch->query);
        ctx->batch.count = 0;

        while (ecs_query_next(&it)) {
            const FlecsTriangle *triangles = ecs_field(&it, FlecsTriangle, 0);
            const FlecsWorldTransform3 *wt = ecs_field(&it, FlecsWorldTransform3, 1);
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

            if ((ctx->batch.count + it.count) <= ctx->batch.capacity) {
                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->batch.instance_transform,
                    ctx->batch.count * sizeof(mat4),
                    wt,
                    it.count * sizeof(mat4));

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->batch.instance_color,
                    ctx->batch.count * sizeof(flecs_rgba_t),
                    colors,
                    it.count * sizeof(flecs_rgba_t));

                for (int32_t i = 0; i < it.count; i ++) {
                    int32_t index = ctx->batch.count + i;
                    ctx->cpu_sizes[index].size.x = triangles[i].x;
                    ctx->cpu_sizes[index].size.y = triangles[i].y;
                    ctx->cpu_sizes[index].size.z = 1.0f;
                }

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->batch.instance_size,
                    ctx->batch.count * sizeof(FlecsInstanceSize),
                    &ctx->cpu_sizes[ctx->batch.count],
                    it.count * sizeof(FlecsInstanceSize));
            }

            ctx->batch.count += it.count;
        }

        if (ctx->batch.count > ctx->batch.capacity) {
            flecsEngine_triangles_ensureCapacity(
                engine, ctx, ctx->batch.count);
            ecs_assert(ctx->batch.count <= ctx->batch.capacity, ECS_INTERNAL_ERROR, NULL);
            goto redo;
        }
    }
}

static void flecsEngine_triangles_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    flecs_engine_triangles_ctx_t *ctx = batch->ctx;
    flecsEngine_triangles_prepareInstances(world, engine, batch, ctx);
    flecsEngine_batchCtx_draw(pass, &ctx->batch);
}

ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world)
{
    ecs_entity_t batch = ecs_new(world);
    ecs_entity_t shader = flecsEngineShader_litColored(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(FlecsTriangle), .src.id = EcsSelf },
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
        .callback = flecsEngine_triangles_callback,
        .ctx = flecsEngine_triangles_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_triangles_deleteCtx
    });

    return batch;
}
