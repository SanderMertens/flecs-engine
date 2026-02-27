#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "flecs_engine.h"

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    int32_t count;
    int32_t capacity;
} flecs_lit_colored_boxes_group_ctx_t;

static flecs_lit_colored_boxes_group_ctx_t* flecsEngine_litColoredBoxes_createCtx(void) {
    return ecs_os_calloc_t(flecs_lit_colored_boxes_group_ctx_t);
}

static void flecsEngine_litColoredBoxes_deleteCtx(
    void *arg)
{
    flecs_lit_colored_boxes_group_ctx_t *ctx = arg;
    if (ctx->instance_transform) {
        wgpuBufferRelease(ctx->instance_transform);
    }
    if (ctx->instance_color) {
        wgpuBufferRelease(ctx->instance_color);
    }

    ecs_os_free(ctx);
}

static void flecsEngine_litColoredBoxes_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecs_lit_colored_boxes_group_ctx_t *ctx,
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

    ctx->capacity = new_capacity;
}

static void flecsEngine_litColoredBoxes_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecs_lit_colored_boxes_group_ctx_t *ctx)
{
    ctx->count = 0;

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    while (ecs_query_next(&it)) {
        const FlecsBox *boxes = ecs_field(&it, FlecsBox, 0);
        const FlecsWorldTransform3 *wt = ecs_field(&it, FlecsWorldTransform3, 1);
        const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

        flecsEngine_litColoredBoxes_ensureCapacity(
            engine, ctx, ctx->count + it.count);

        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_transform,
            ctx->count * sizeof(mat4),
            wt,
            it.count * sizeof(mat4));

        if (colors) {
            wgpuQueueWriteBuffer(
                engine->queue,
                ctx->instance_color,
                ctx->count  * sizeof(flecs_rgba_t),
                colors,
                it.count * sizeof(flecs_rgba_t));
        }

        ctx->count += it.count;
    }
}

static void flecsEngine_litColoredBoxes_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    flecs_lit_colored_boxes_group_ctx_t *ctx = batch->ctx;

    flecsEngine_litColoredBoxes_prepareInstances(world, engine, batch, ctx);
    if (!ctx->count) {
        return;
    }

    const FlecsMesh3Impl *mesh = flecsEngineGeometry3EnsureUnitBox((ecs_world_t*)world);
    if (!mesh || !mesh->vertex_buffer || !mesh->index_buffer || !mesh->index_count) {
        return;
    }

    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh->vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, ctx->instance_transform, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 2, ctx->instance_color, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, mesh->index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, (uint32_t)mesh->index_count, (uint32_t)ctx->count, 0, 0, 0);
}

ecs_entity_t flecsEngine_createBatch_litColoredBoxes(
    ecs_world_t *world)
{
    ecs_entity_t batch = ecs_new(world);
    ecs_entity_t shader = flecsEngineShader_litColored(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsGeometryConflict3), .src.id = EcsSelf, .oper = EcsNot }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsInstanceColor)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .callback = flecsEngine_litColoredBoxes_callback,
        .ctx = flecsEngine_litColoredBoxes_createCtx(),
        .free_ctx = flecsEngine_litColoredBoxes_deleteCtx
    });

    return batch;
}
