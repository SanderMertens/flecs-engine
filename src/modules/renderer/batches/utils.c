#include "batches.h"

static float flecsEngine_clampf(
    float value,
    float min_value,
    float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

void flecsEngine_batchCtx_init(
    flecs_engine_batch_ctx_t *ctx,
    const FlecsMesh3Impl *mesh)
{
    ctx->instance_transform = NULL;
    ctx->instance_color = NULL;
    ctx->instance_pbr = NULL;
    ctx->cpu_transforms = NULL;
    ctx->cpu_pbr = NULL;
    ctx->count = 0;
    ctx->capacity = 0;
    if (mesh) {
        ctx->mesh = *mesh;
    } else {
        ecs_os_zeromem(&ctx->mesh);
    }
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
    if (ctx->instance_pbr) {
        wgpuBufferRelease(ctx->instance_pbr);
        ctx->instance_pbr = NULL;
    }
    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
        ctx->cpu_transforms = NULL;
    }
    if (ctx->cpu_pbr) {
        ecs_os_free(ctx->cpu_pbr);
        ctx->cpu_pbr = NULL;
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
    if (ctx->instance_pbr) {
        wgpuBufferRelease(ctx->instance_pbr);
    }
    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
    }
    if (ctx->cpu_pbr) {
        ecs_os_free(ctx->cpu_pbr);
    }

    ctx->instance_transform = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsInstanceTransform)
        });

    ctx->instance_color = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(flecs_rgba_t)
        });

    ctx->instance_pbr = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsInstancePbrMaterial)
        });

    ctx->cpu_transforms =
        ecs_os_malloc_n(FlecsInstanceTransform, new_capacity);
    ctx->cpu_pbr =
        ecs_os_malloc_n(FlecsInstancePbrMaterial, new_capacity);

    ctx->capacity = new_capacity;
}

void flecsEngine_batchCtx_draw(
    const WGPURenderPassEncoder pass,
    const flecs_engine_batch_ctx_t *ctx)
{
    if (!ctx->count) {
        return;
    }

    if (!ctx->mesh.vertex_buffer || !ctx->mesh.index_buffer || !ctx->mesh.index_count) {
        return;
    }

    WGPUBufferUsage vertex_usage = wgpuBufferGetUsage(ctx->mesh.vertex_buffer);
    if (!(vertex_usage & WGPUBufferUsage_Vertex)) {
        return;
    }

    WGPUBufferUsage index_usage = wgpuBufferGetUsage(ctx->mesh.index_buffer);
    if (!(index_usage & WGPUBufferUsage_Index)) {
        return;
    }

    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, ctx->mesh.vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, ctx->instance_transform, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 2, ctx->instance_color, 0, WGPU_WHOLE_SIZE);
    if (ctx->instance_pbr) {
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 3, ctx->instance_pbr, 0, WGPU_WHOLE_SIZE);
    }
    wgpuRenderPassEncoderSetIndexBuffer(pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, ctx->mesh.index_count, ctx->count, 0, 0, 0);
}

void flecsEngine_batchCtx_uploadInstances(
    const FlecsEngineImpl *engine,
    const flecs_engine_batch_ctx_t *ctx,
    int32_t offset,
    const FlecsRgba *colors,
    const FlecsPbrMaterial *materials,
    int32_t count)
{
    if (!count) {
        return;
    }

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->instance_transform,
        (uint64_t)offset * sizeof(FlecsInstanceTransform),
        &ctx->cpu_transforms[offset],
        (uint64_t)count * sizeof(FlecsInstanceTransform));

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->instance_color,
        (uint64_t)offset * sizeof(flecs_rgba_t),
        colors,
        (uint64_t)count * sizeof(flecs_rgba_t));

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->instance_pbr,
        (uint64_t)offset * sizeof(FlecsInstancePbrMaterial),
        materials,
        (uint64_t)count * sizeof(FlecsInstancePbrMaterial));
}

void flecsEngine_packInstanceTransform(
    FlecsInstanceTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z)
{
    out->c0.x = wt->m[0][0] * scale_x;
    out->c0.y = wt->m[0][1] * scale_x;
    out->c0.z = wt->m[0][2] * scale_x;

    out->c1.x = wt->m[1][0] * scale_y;
    out->c1.y = wt->m[1][1] * scale_y;
    out->c1.z = wt->m[1][2] * scale_y;

    out->c2.x = wt->m[2][0] * scale_z;
    out->c2.y = wt->m[2][1] * scale_z;
    out->c2.z = wt->m[2][2] * scale_z;

    out->c3.x = wt->m[3][0];
    out->c3.y = wt->m[3][1];
    out->c3.z = wt->m[3][2];
}
