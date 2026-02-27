#include "batches.h"

void flecsEngine_batchCtx_init(
    flecs_engine_batch_ctx_t *ctx,
    const FlecsMesh3Impl *mesh)
{
    ctx->instance_transform = NULL;
    ctx->instance_color = NULL;
    ctx->instance_size = NULL;
    ctx->count = 0;
    ctx->capacity = 0;
    ctx->mesh = mesh;
}

void flecsEngine_batchCtx_fini(
    flecs_engine_batch_ctx_t *ctx)
{
    if (ctx->instance_transform) {
        wgpuBufferRelease(ctx->instance_transform);
        ctx->instance_transform = NULL;
    }
    if (ctx->instance_color) {
        wgpuBufferRelease(ctx->instance_color);
        ctx->instance_color = NULL;
    }
    if (ctx->instance_size) {
        wgpuBufferRelease(ctx->instance_size);
        ctx->instance_size = NULL;
    }

    ctx->count = 0;
    ctx->capacity = 0;
}

void flecsEngine_batchCtx_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecs_engine_batch_ctx_t *ctx,
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

    ctx->capacity = new_capacity;
}

void flecsEngine_batchCtx_draw(
    const WGPURenderPassEncoder pass,
    const flecs_engine_batch_ctx_t *ctx)
{
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
