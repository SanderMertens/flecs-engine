#include "../../renderer.h"
#include "../../shaders/shaders.h"
#include "../../../geometry3/geometry3.h"
#include "../batches.h"
#include "flecs_engine.h"

typedef struct {
    flecs_engine_batch_ctx_t batch;
} flecs_engine_right_triangles_ctx_t;

static flecs_engine_right_triangles_ctx_t* flecsEngine_right_triangles_createCtx(
    ecs_world_t *world)
{
    flecs_engine_right_triangles_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_right_triangles_ctx_t);
    flecsEngine_batchCtx_init(
        &result->batch, flecsGeometry3_getRightTriangleAsset(world));
    return result;
}

static void flecsEngine_right_triangles_deleteCtx(
    void *arg)
{
    flecs_engine_right_triangles_ctx_t *ctx = arg;
    flecsEngine_batchCtx_fini(&ctx->batch);
    ecs_os_free(ctx);
}

static void flecsEngine_right_triangles_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecs_engine_right_triangles_ctx_t *ctx)
{
redo: {
        ecs_iter_t it = ecs_query_iter(world, batch->query);
        ctx->batch.count = 0;

        while (ecs_query_next(&it)) {
            const FlecsRightTriangle *right_triangles =
                ecs_field(&it, FlecsRightTriangle, 0);
            const FlecsWorldTransform3 *wt = ecs_field(&it, FlecsWorldTransform3, 1);
            const FlecsMaterialId *material_ids =
                ecs_field(&it, FlecsMaterialId, 2);

            if ((ctx->batch.count + it.count) <= ctx->batch.capacity) {
                for (int32_t i = 0; i < it.count; i ++) {
                    int32_t index = ctx->batch.count + i;
                    flecsEngine_packInstanceTransform(
                        &ctx->batch.cpu_transforms[index],
                        &wt[i],
                        right_triangles[i].x,
                        right_triangles[i].y,
                        1.0f);

                }

                flecsEngine_batchCtx_uploadMaterialIds(
                    engine,
                    &ctx->batch,
                    ctx->batch.count,
                    material_ids,
                    it.count);
            }

            ctx->batch.count += it.count;
        }

        if (ctx->batch.count > ctx->batch.capacity) {
            flecsEngine_batchCtx_ensureCapacity(
                engine, &ctx->batch, ctx->batch.count);
            ecs_assert(ctx->batch.count <= ctx->batch.capacity, ECS_INTERNAL_ERROR, NULL);
            goto redo;
        }
    }
}

static void flecsEngine_right_triangles_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    flecs_engine_right_triangles_ctx_t *ctx = batch->ctx;
    flecsEngine_right_triangles_prepareInstances(world, engine, batch, ctx);
    flecsEngine_batchCtx_drawMaterialIndex(pass, &ctx->batch);
}

ecs_entity_t flecsEngine_createBatch_right_triangles_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngineShader_pbrColoredMaterialIndex(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsRightTriangle), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .callback = flecsEngine_right_triangles_callback,
        .ctx = flecsEngine_right_triangles_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_right_triangles_deleteCtx
    });

    return batch;
}
