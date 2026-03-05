#include "../../renderer.h"
#include "../../shaders/shaders.h"
#include "../../../geometry3/geometry3.h"
#include "../batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_batch_t batch;
} flecs_engine_right_triangle_prisms_ctx_t;

static flecs_engine_right_triangle_prisms_ctx_t* flecsEngine_right_triangle_prisms_createCtx(
    ecs_world_t *world)
{
    flecs_engine_right_triangle_prisms_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_right_triangle_prisms_ctx_t);
    flecsEngine_batch_init(
        &result->batch, flecsGeometry3_getRightTrianglePrismAsset(world));
    return result;
}

static void flecsEngine_right_triangle_prisms_deleteCtx(
    void *arg)
{
    flecs_engine_right_triangle_prisms_ctx_t *ctx = arg;
    flecsEngine_batch_fini(&ctx->batch);
    ecs_os_free(ctx);
}

static void flecsEngine_right_triangle_prisms_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecs_engine_right_triangle_prisms_ctx_t *ctx)
{
redo: {
        ecs_iter_t it = ecs_query_iter(world, batch->query);
        ctx->batch.count = 0;

        while (ecs_query_next(&it)) {
            const FlecsRightTrianglePrism *right_triangle_prisms =
                ecs_field(&it, FlecsRightTrianglePrism, 0);
            const FlecsWorldTransform3 *wt = ecs_field(&it, FlecsWorldTransform3, 1);

            if ((ctx->batch.count + it.count) <= ctx->batch.capacity) {
                for (int32_t i = 0; i < it.count; i ++) {
                    int32_t index = ctx->batch.count + i;
                    flecsEngine_packInstanceTransform(
                        &ctx->batch.cpu_transforms[index],
                        &wt[i],
                        right_triangle_prisms[i].x,
                        right_triangle_prisms[i].y,
                        right_triangle_prisms[i].z);

                }

                flecsEngine_batch_uploadInstances(
                    engine,
                    &ctx->batch,
                    ctx->batch.count,
                    ecs_field(&it, FlecsRgba, 2),
                    ecs_field(&it, FlecsPbrMaterial, 3),
                    ecs_field(&it, FlecsEmissive, 4),
                    it.count);
            }

            ctx->batch.count += it.count;
        }

        if (ctx->batch.count > ctx->batch.capacity) {
            flecsEngine_batch_ensureCapacity(
                engine, &ctx->batch, ctx->batch.count);
            ecs_assert(ctx->batch.count <= ctx->batch.capacity, ECS_INTERNAL_ERROR, NULL);
            goto redo;
        }
    }
}

static void flecsEngine_right_triangle_prisms_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    flecs_engine_right_triangle_prisms_ctx_t *ctx = batch->ctx;
    flecsEngine_right_triangle_prisms_prepareInstances(world, engine, batch, ctx);
    flecsEngine_batch_draw(pass, &ctx->batch);
}

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngineShader_pbrColored(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsRightTrianglePrism), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsRgba),
            ecs_id(FlecsPbrMaterial),
            ecs_id(FlecsEmissive)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .callback = flecsEngine_right_triangle_prisms_callback,
        .ctx = flecsEngine_right_triangle_prisms_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_right_triangle_prisms_deleteCtx
    });

    return batch;
}
