#include "../../renderer.h"
#include "../../shaders/shaders.h"
#include "../../../geometry3/geometry3.h"
#include "../batches.h"
#include "flecs_engine.h"

static flecsEngine_batch_t* flecsEngine_boxes_createCtx(
    ecs_world_t *world) 
{
    flecsEngine_batch_t *result =
        ecs_os_calloc_t(flecsEngine_batch_t);
    flecsEngine_batch_init(result, flecsGeometry3_getBoxAsset(world));
    return result;
}

static void flecsEngine_boxes_deleteCtx(
    void *arg)
{
    flecsEngine_batch_t *ctx = arg;
    flecsEngine_batch_fini(ctx);
    ecs_os_free(ctx);
}

static void flecsEngine_boxes_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx)
{
redo: {
        ecs_iter_t it = ecs_query_iter(world, batch->query);
        ctx->count = 0;

        while (ecs_query_next(&it)) {
            const FlecsBox *boxes = ecs_field(&it, FlecsBox, 0);
            const FlecsWorldTransform3 *wt = ecs_field(&it, FlecsWorldTransform3, 1);

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

                flecsEngine_batch_uploadMaterialIds(
                    engine,
                    ctx,
                    ctx->count,
                    ecs_field(&it, FlecsMaterialId, 2),
                    it.count);
            }

            ctx->count += it.count;
        }

        if (ctx->count > ctx->capacity) {
            flecsEngine_batch_ensureCapacity(engine, ctx, ctx->count);
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
    flecsEngine_batch_t *ctx = batch->ctx;
    flecsEngine_boxes_prepareInstances(world, engine, batch, ctx);
    flecsEngine_batch_drawMaterialIndex(pass, ctx);
}

ecs_entity_t flecsEngine_createBatch_boxes_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngineShader_pbrColoredMaterialIndex(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
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
        .callback = flecsEngine_boxes_callback,
        .ctx = flecsEngine_boxes_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_boxes_deleteCtx
    });

    return batch;
}
