#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "flecs_engine.h"

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    WGPUBuffer instance_size;
    FlecsInstanceSize *cpu_sizes;
    int32_t count;
    int32_t capacity;
    const FlecsMesh3Impl *mesh;
} flecs_lit_colored_quads_group_ctx_t;

static flecs_lit_colored_quads_group_ctx_t* flecsEngine_quads_createCtx(
    ecs_world_t *world)
{
    flecs_lit_colored_quads_group_ctx_t *result =
        ecs_os_calloc_t(flecs_lit_colored_quads_group_ctx_t);
    result->mesh = flecsGeometry3_getQuadAsset(world);
    return result;
}

static void flecsEngine_quads_deleteCtx(
    void *arg)
{
    flecs_lit_colored_quads_group_ctx_t *ctx = arg;
    if (ctx->instance_transform) {
        wgpuBufferRelease(ctx->instance_transform);
    }
    if (ctx->instance_color) {
        wgpuBufferRelease(ctx->instance_color);
    }
    if (ctx->instance_size) {
        wgpuBufferRelease(ctx->instance_size);
    }
    if (ctx->cpu_sizes) {
        ecs_os_free(ctx->cpu_sizes);
    }

    ecs_os_free(ctx);
}

static void flecsEngine_quads_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecs_lit_colored_quads_group_ctx_t *ctx,
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

    if (ctx->cpu_sizes) {
        ecs_os_free(ctx->cpu_sizes);
    }
    ctx->cpu_sizes = ecs_os_malloc_n(FlecsInstanceSize, new_capacity);
    ctx->capacity = new_capacity;
}

static void flecsEngine_quads_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecs_lit_colored_quads_group_ctx_t *ctx)
{
redo: {
        ecs_iter_t it = ecs_query_iter(world, batch->query);
        ctx->count = 0;

        while (ecs_query_next(&it)) {
            const FlecsQuad *quads = ecs_field(&it, FlecsQuad, 0);
            const FlecsWorldTransform3 *wt = ecs_field(&it, FlecsWorldTransform3, 1);
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

            if ((ctx->count + it.count) <= ctx->capacity) {
                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_transform,
                    ctx->count * sizeof(mat4),
                    wt,
                    it.count * sizeof(mat4));

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_color,
                    ctx->count * sizeof(flecs_rgba_t),
                    colors,
                    it.count * sizeof(flecs_rgba_t));

                for (int32_t i = 0; i < it.count; i ++) {
                    int32_t index = ctx->count + i;
                    ctx->cpu_sizes[index].size.x = quads[i].x;
                    ctx->cpu_sizes[index].size.y = quads[i].y;
                    ctx->cpu_sizes[index].size.z = 1.0f;
                }

                wgpuQueueWriteBuffer(
                    engine->queue,
                    ctx->instance_size,
                    ctx->count * sizeof(FlecsInstanceSize),
                    &ctx->cpu_sizes[ctx->count],
                    it.count * sizeof(FlecsInstanceSize));
            }

            ctx->count += it.count;
        }

        if (ctx->count > ctx->capacity) {
            flecsEngine_quads_ensureCapacity(
                engine, ctx, ctx->count);
            ecs_assert(ctx->count <= ctx->capacity, ECS_INTERNAL_ERROR, NULL);
            goto redo;
        }
    }
}

static void flecsEngine_quads_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    flecs_lit_colored_quads_group_ctx_t *ctx = batch->ctx;

    flecsEngine_quads_prepareInstances(world, engine, batch, ctx);
    if (!ctx->count) {
        return;
    }

    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, ctx->mesh->vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, ctx->instance_transform, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 2, ctx->instance_color, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 3, ctx->instance_size, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, ctx->mesh->index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, ctx->mesh->index_count, ctx->count, 0, 0, 0);
}

ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world)
{
    ecs_entity_t batch = ecs_new(world);
    ecs_entity_t shader = flecsEngineShader_litColored(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(FlecsQuad), .src.id = EcsSelf },
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
        .callback = flecsEngine_quads_callback,
        .ctx = flecsEngine_quads_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_quads_deleteCtx
    });

    return batch;
}
