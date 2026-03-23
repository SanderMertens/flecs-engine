#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

static uint64_t flecsEngine_textured_mesh_groupByMesh(
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

static void* flecsEngine_textured_mesh_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr)
{
    (void)world;
    (void)ptr;
    return flecsEngine_batch_create(world, NULL, group_id, false, 0, NULL);
}

static void flecsEngine_textured_mesh_onGroupDelete(
    ecs_world_t *world,
    uint64_t group_id,
    void *group_ptr,
    void *ptr)
{
    (void)world;
    (void)group_id;
    (void)ptr;
    flecsEngine_batch_delete(group_ptr);
}

static void flecsEngine_textured_mesh_extractGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    uint64_t group_id)
{
    if (!group_id) {
        return;
    }

    flecsEngine_batch_t *ctx =
        ecs_query_get_group_ctx(batch->query, group_id);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    const FlecsMesh3Impl *mesh = ecs_get(
        world, (ecs_entity_t)group_id, FlecsMesh3Impl);
    if (!mesh) {
        ctx->count = 0;
        ecs_os_zeromem(&ctx->mesh);
        return;
    }

    if (!mesh->vertex_uv_buffer || !mesh->index_buffer || !mesh->index_count) {
        ctx->count = 0;
        ecs_os_zeromem(&ctx->mesh);
        return;
    }

    ctx->mesh = *mesh;
    flecsEngine_batch_extractInstances(world, engine, batch, ctx);
}

static void flecsEngine_textured_mesh_renderGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    const WGPURenderPassEncoder pass,
    uint64_t group_id)
{
    if (!group_id) {
        return;
    }

    flecsEngine_batch_t *ctx =
        ecs_query_get_group_ctx(batch->query, group_id);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    if (!ctx->count) {
        return;
    }

    if (!ctx->mesh.index_buffer || !ctx->mesh.index_count) {
        return;
    }

    /* During shadow pass, use the non-UV vertex buffer so that
     * vertex attribute locations match the shadow depth shader. */
    const FlecsEngineImpl *eng = engine;
    if (eng->in_shadow_pass) {
        flecsEngine_batch_draw(pass, ctx);
        return;
    }

    /* Look up texture bind group from the prefab entity */
    const FlecsPbrTextures *pbr_tex = ecs_get(
        world, (ecs_entity_t)group_id, FlecsPbrTextures);
    if (!pbr_tex || !pbr_tex->_bind_group) {
        return;
    }
    wgpuRenderPassEncoderSetBindGroup(
        pass, 2, (WGPUBindGroup)pbr_tex->_bind_group, 0, NULL);

    /* Draw using the UV vertex buffer */
    if (!ctx->mesh.vertex_uv_buffer) {
        return;
    }

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, ctx->mesh.vertex_uv_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, ctx->instance_transform, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 2, ctx->instance_material_id, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, ctx->count, 0, 0, 0);
}

static void flecsEngine_textured_mesh_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    if (!groups) {
        return;
    }

    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git);
        flecsEngine_textured_mesh_extractGroup(world, engine, batch, group);
    }
}

static void flecsEngine_textured_mesh_render(
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
        flecsEngine_textured_mesh_renderGroup(world, engine, batch, pass, group);
    }
}

ecs_entity_t flecsEngine_createBatch_textured_mesh(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrTextured(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_textured_mesh_groupByMesh,
        .on_group_create = flecsEngine_textured_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_textured_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertexUv),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsMaterialId)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .extract_callback = flecsEngine_textured_mesh_extract,
        .callback = flecsEngine_textured_mesh_render
    });

    return batch;
}
