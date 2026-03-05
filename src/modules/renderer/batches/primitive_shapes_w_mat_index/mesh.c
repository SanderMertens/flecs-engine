#include "../../renderer.h"
#include "../../shaders/shaders.h"
#include "../../../geometry3/geometry3.h"
#include "../batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_batch_t batch;
} flecs_engine_mesh_group_ctx_t;

static uint64_t flecsEngine_mesh_groupByMesh(
    ecs_world_t *world,
    ecs_table_t *table,
    ecs_id_t id,
    void *ctx)
{
    (void)id;
    (void)ctx;

    ecs_entity_t tgt = 0;
    if (ecs_search_relation(
        world,
        table,
        0,
        ecs_id(FlecsMesh3Impl),
        EcsIsA,
        EcsSelf | EcsUp,
        &tgt,
        NULL,
        NULL) == -1)
    {
        return 0;
    }

    return tgt;
}

static void* flecsEngine_mesh_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr)
{
    (void)world;
    (void)group_id;
    (void)ptr;
    flecs_engine_mesh_group_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_mesh_group_ctx_t);
    flecsEngine_batch_init(&result->batch, NULL);
    return result;
}

static void flecsEngine_mesh_onGroupDelete(
    ecs_world_t *world,
    uint64_t group_id,
    void *group_ptr,
    void *ptr)
{
    (void)world;
    (void)group_id;
    (void)ptr;

    flecs_engine_mesh_group_ctx_t *ctx = group_ptr;
    flecsEngine_batch_fini(&ctx->batch);
    ecs_os_free(ctx);
}

static void flecsEngine_mesh_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    uint64_t group_id,
    flecs_engine_mesh_group_ctx_t *ctx)
{
redo: {
        ecs_iter_t it = ecs_query_iter(world, batch->query);
        ecs_iter_set_group(&it, group_id);
        ctx->batch.count = 0;

        while (ecs_query_next(&it)) {
            const FlecsWorldTransform3 *wt = ecs_field(
                &it, FlecsWorldTransform3, 1);

            if ((ctx->batch.count + it.count) <= ctx->batch.capacity) {
                for (int32_t i = 0; i < it.count; i ++) {
                    int32_t index = ctx->batch.count + i;
                    flecsEngine_packInstanceTransform(
                        &ctx->batch.cpu_transforms[index],
                        &wt[i],
                        1.0f,
                        1.0f,
                        1.0f);
                }

                flecsEngine_batch_uploadMaterialIds(
                    engine,
                    &ctx->batch,
                    ctx->batch.count,
                    ecs_field(&it, FlecsMaterialId, 2),
                    it.count);
            }

            ctx->batch.count += it.count;
        }

        if (ctx->batch.count > ctx->batch.capacity) {
            flecsEngine_batch_ensureCapacity(
                engine, &ctx->batch, ctx->batch.count);
            ecs_assert(
                ctx->batch.count <= ctx->batch.capacity,
                ECS_INTERNAL_ERROR,
                NULL);
            goto redo;
        }
    }
}

static void flecsEngine_mesh_renderGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch,
    uint64_t group_id)
{
    if (!group_id) {
        return;
    }

    flecs_engine_mesh_group_ctx_t *ctx =
        ecs_query_get_group_ctx(batch->query, group_id);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    const FlecsMesh3Impl *mesh = ecs_get(
        world, (ecs_entity_t)group_id, FlecsMesh3Impl);
    if (!mesh) {
        char *path = ecs_get_path(world, group_id);
        ecs_err("missing Mesh3Impl component for geometry group '%s'", path);
        ecs_os_free(path);
        return;
    }

    if (!mesh->vertex_buffer || !mesh->index_buffer || !mesh->index_count) {
        char *path = ecs_get_path(world, group_id);
        ecs_err("missing GPU buffers for geometry group '%s'", path);
        ecs_os_free(path);
        return;
    }

    flecsEngine_mesh_prepareInstances(world, engine, batch, group_id, ctx);
    ctx->batch.mesh = *mesh;
    flecsEngine_batch_drawMaterialIndex(pass, &ctx->batch);
}

static void flecsEngine_mesh_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        flecsEngine_mesh_renderGroup(world, engine, pass, batch, group);
    }
}

ecs_entity_t flecsEngine_createBatch_mesh_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngineShader_pbrColoredMaterialIndex(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
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
        .callback = flecsEngine_mesh_callback
    });

    return batch;
}
