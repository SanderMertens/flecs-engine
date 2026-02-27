#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "flecs_engine.h"

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    WGPUBuffer instance_size;
    mat4 *cpu_transforms;
    flecs_rgba_t *cpu_colors;
    FlecsInstanceSize *cpu_sizes;
    int32_t count;
    int32_t capacity;
} flecs_lit_colored_geometry_group_ctx_t;

static uint64_t flecsEngine_litColoredGeometry_groupByMesh(
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

static void* flecsEngine_litColoredGeometry_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr)
{
    (void)world;
    (void)ptr;
    return ecs_os_calloc_t(flecs_lit_colored_geometry_group_ctx_t);
}

static void flecsEngine_litColoredGeometry_onGroupDelete(
    ecs_world_t *world,
    uint64_t group_id,
    void *group_ptr,
    void *ptr)
{
    (void)ptr;

    flecs_lit_colored_geometry_group_ctx_t *ctx = group_ptr;

    if (ctx->instance_transform) {
        wgpuBufferRelease(ctx->instance_transform);
    }
    if (ctx->instance_color) {
        wgpuBufferRelease(ctx->instance_color);
    }
    if (ctx->instance_size) {
        wgpuBufferRelease(ctx->instance_size);
    }
    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
    }
    if (ctx->cpu_colors) {
        ecs_os_free(ctx->cpu_colors);
    }
    if (ctx->cpu_sizes) {
        ecs_os_free(ctx->cpu_sizes);
    }

    ecs_os_free(ctx);
}

static void flecsEngine_litColoredGeometry_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecs_lit_colored_geometry_group_ctx_t *ctx,
    int32_t count)
{
    if (count <= ctx->capacity) {
        return;
    }

    int32_t new_capacity = count;
    if (new_capacity < 64) {
        new_capacity = 64;
    }

    if (ctx->instance_transform) {
        wgpuBufferRelease(ctx->instance_transform);
    }
    if (ctx->instance_color) {
        wgpuBufferRelease(ctx->instance_color);
    }
    if (ctx->instance_size) {
        wgpuBufferRelease(ctx->instance_size);
    }

    ctx->instance_transform = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(mat4)
        });

    ctx->instance_color = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(flecs_rgba_t)
        });

    ctx->instance_size = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsInstanceSize)
        });

    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
    }
    if (ctx->cpu_colors) {
        ecs_os_free(ctx->cpu_colors);
    }
    if (ctx->cpu_sizes) {
        ecs_os_free(ctx->cpu_sizes);
    }

    ctx->cpu_transforms = ecs_os_malloc_n(mat4, new_capacity);
    ctx->cpu_colors = ecs_os_malloc_n(flecs_rgba_t, new_capacity);
    ctx->cpu_sizes = ecs_os_malloc_n(FlecsInstanceSize, new_capacity);
    ctx->capacity = new_capacity;
}

static flecs_rgba_t flecsEngine_litColoredGeometry_pickColor(
    const ecs_iter_t *it,
    int32_t row,
    int32_t self_color_field,
    int32_t asset_color_field)
{
    const FlecsRgba *self_color = ecs_field(it, FlecsRgba, self_color_field);
    if (self_color) {
        if (ecs_field_is_self(it, self_color_field)) {
            return self_color[row];
        }
        return self_color[0];
    }

    const FlecsRgba *asset_color = ecs_field(it, FlecsRgba, asset_color_field);
    if (asset_color) {
        if (ecs_field_is_self(it, asset_color_field)) {
            return asset_color[row];
        }
        return asset_color[0];
    }

    return (flecs_rgba_t){255, 255, 255, 255};
}

static void flecsEngine_litColoredGeometry_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    uint64_t group_id,
    flecs_lit_colored_geometry_group_ctx_t *ctx)
{
    ecs_query_t *q = batch->query;
    ctx->count = 0;

    ecs_iter_t count_it = ecs_query_iter(world, q);
    ecs_iter_set_group(&count_it, group_id);
    while (ecs_query_next(&count_it)) {
        ctx->count += count_it.count;
    }

    if (!ctx->count) {
        return;
    }

    flecsEngine_litColoredGeometry_ensureCapacity(engine, ctx, ctx->count);

    int32_t offset = 0;
    ecs_iter_t write_it = ecs_query_iter(world, q);
    ecs_iter_set_group(&write_it, group_id);
    while (ecs_query_next(&write_it)) {
        const FlecsWorldTransform3 *wt = ecs_field(&write_it, FlecsWorldTransform3, 1);

        for (int32_t i = 0; i < write_it.count; i ++) {
            glm_mat4_copy((vec4*)wt[i].m, ctx->cpu_transforms[offset]);
            ctx->cpu_colors[offset] = flecsEngine_litColoredGeometry_pickColor(
                &write_it, i, 2, 3);
            ctx->cpu_sizes[offset] = (FlecsInstanceSize){
                .size = {1.0f, 1.0f, 1.0f}
            };
            offset ++;
        }
    }

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->instance_transform,
        0,
        ctx->cpu_transforms,
        (uint64_t)ctx->count * sizeof(mat4));

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->instance_color,
        0,
        ctx->cpu_colors,
        (uint64_t)ctx->count * sizeof(flecs_rgba_t));

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->instance_size,
        0,
        ctx->cpu_sizes,
        (uint64_t)ctx->count * sizeof(FlecsInstanceSize));
}

static void flecsEngine_litColoredGeometry_renderGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch,
    uint64_t group_id)
{
    if (!group_id) {
        return;
    }

    flecs_lit_colored_geometry_group_ctx_t *ctx =
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

    WGPUBufferUsage vertex_usage = wgpuBufferGetUsage(mesh->vertex_buffer);
    if (!(vertex_usage & WGPUBufferUsage_Vertex)) {
        return;
    }

    WGPUBufferUsage index_usage = wgpuBufferGetUsage(mesh->index_buffer);
    if (!(index_usage & WGPUBufferUsage_Index)) {
        return;
    }

    flecsEngine_litColoredGeometry_prepareInstances(world, engine, batch, group_id, ctx);
    if (!ctx->count) {
        return;
    }

    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh->vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, ctx->instance_transform, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 2, ctx->instance_color, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 3, ctx->instance_size, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, mesh->index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, (uint32_t)mesh->index_count, (uint32_t)ctx->count, 0, 0, 0);
}

static void flecsEngine_litColoredGeometry_callback(
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
        flecsEngine_litColoredGeometry_renderGroup(world, engine, pass, batch, group);
    }
}

ecs_entity_t flecsEngine_createBatch_litColoredGeometry(
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
        .group_by_callback = flecsEngine_litColoredGeometry_groupByMesh,
        .on_group_create = flecsEngine_litColoredGeometry_onGroupCreate,
        .on_group_delete = flecsEngine_litColoredGeometry_onGroupDelete
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
        .callback = flecsEngine_litColoredGeometry_callback
    });

    return batch;
}
