#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecs_engine_batch_ctx_t batch;
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
    flecsEngine_batchCtx_init(&result->batch, NULL);
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
    flecsEngine_batchCtx_fini(&ctx->batch);
    ecs_os_free(ctx);
}

static void flecsEngine_mesh_writeDefaultSizes(
    const FlecsEngineImpl *engine,
    WGPUBuffer buffer,
    int32_t count)
{
    enum { CHUNK_SIZE = 64 };
    FlecsInstanceSize chunk[CHUNK_SIZE];
    for (int32_t i = 0; i < CHUNK_SIZE; i ++) {
        chunk[i].size = (flecs_vec3_t){1.0f, 1.0f, 1.0f};
    }

    int32_t written = 0;
    while (written < count) {
        int32_t n = count - written;
        if (n > CHUNK_SIZE) {
            n = CHUNK_SIZE;
        }

        wgpuQueueWriteBuffer(
            engine->queue,
            buffer,
            (uint64_t)written * sizeof(FlecsInstanceSize),
            chunk,
            (uint64_t)n * sizeof(FlecsInstanceSize));

        written += n;
    }
}

static void flecsEngine_mesh_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecs_engine_mesh_group_ctx_t *ctx,
    int32_t count)
{
    int32_t prev_capacity = ctx->batch.capacity;
    flecsEngine_batchCtx_ensureCapacity(engine, &ctx->batch, count);

    if (ctx->batch.capacity != prev_capacity) {
        flecsEngine_mesh_writeDefaultSizes(
            engine,
            ctx->batch.instance_size,
            ctx->batch.capacity);
    }
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
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

            if ((ctx->batch.count + it.count) <= ctx->batch.capacity) {
                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->batch.instance_transform,
                    (uint64_t)ctx->batch.count * sizeof(mat4),
                    wt,
                    (uint64_t)it.count * sizeof(mat4));

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->batch.instance_color,
                    (uint64_t)ctx->batch.count * sizeof(flecs_rgba_t),
                    colors,
                    (uint64_t)it.count * sizeof(flecs_rgba_t));
            }

            ctx->batch.count += it.count;
        }

        if (ctx->batch.count > ctx->batch.capacity) {
            flecsEngine_mesh_ensureCapacity(engine, ctx, ctx->batch.count);
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
    flecsEngine_batchCtx_draw(pass, &ctx->batch);
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

ecs_entity_t flecsEngine_createBatch_mesh(
    ecs_world_t *world)
{
    ecs_entity_t batch = ecs_new(world);
    ecs_entity_t shader = flecsEngineShader_litColored(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
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
            ecs_id(FlecsInstanceColor),
            ecs_id(FlecsInstanceSize)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .callback = flecsEngine_mesh_callback
    });

    return batch;
}
