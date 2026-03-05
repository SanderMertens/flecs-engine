#include <stddef.h>

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

void flecsEngine_batch_init(
    flecsEngine_batch_t *ctx,
    const FlecsMesh3Impl *mesh)
{
    ctx->instance_transform = NULL;
    ctx->instance_color = NULL;
    ctx->instance_pbr = NULL;
    ctx->instance_emissive = NULL;
    ctx->instance_material_id = NULL;
    ctx->cpu_transforms = NULL;
    ctx->count = 0;
    ctx->capacity = 0;
    if (mesh) {
        ctx->mesh = *mesh;
    } else {
        ecs_os_zeromem(&ctx->mesh);
    }
}

void flecsEngine_batch_fini(
    flecsEngine_batch_t *ctx)
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
    if (ctx->instance_emissive) {
        wgpuBufferRelease(ctx->instance_emissive);
        ctx->instance_emissive = NULL;
    }
    if (ctx->instance_material_id) {
        wgpuBufferRelease(ctx->instance_material_id);
        ctx->instance_material_id = NULL;
    }
    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
        ctx->cpu_transforms = NULL;
    }

    ctx->count = 0;
    ctx->capacity = 0;
}

void flecsEngine_batch_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
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
    if (ctx->instance_emissive) {
        wgpuBufferRelease(ctx->instance_emissive);
    }
    if (ctx->instance_material_id) {
        wgpuBufferRelease(ctx->instance_material_id);
    }
    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
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
            .size = (uint64_t)new_capacity * sizeof(FlecsPbrMaterial)
        });

    ctx->instance_emissive = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsEmissive)
        });

    ctx->instance_material_id = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsMaterialId)
        });

    ctx->cpu_transforms =
        ecs_os_malloc_n(FlecsInstanceTransform, new_capacity);

    ctx->capacity = new_capacity;
}

static FlecsMaterialId* flecsEngine_batch_ensureMaterialIds(
    flecsEngine_batch_t *ctx,
    int32_t count)
{
    if (count > ctx->material_id_capacity) {
        ecs_os_free(ctx->cpu_material_ids);
        ctx->cpu_material_ids = ecs_os_malloc_n(FlecsMaterialId, count);
    }
    return ctx->cpu_material_ids;
}

void flecsEngine_batch_draw(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx)
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

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, ctx->mesh.vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, ctx->instance_transform, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 2, ctx->instance_color, 0, WGPU_WHOLE_SIZE);

    if (ctx->instance_pbr) {
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 3, ctx->instance_pbr, 0, WGPU_WHOLE_SIZE);
    }

    if (ctx->instance_emissive) {
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 4, ctx->instance_emissive, 0, WGPU_WHOLE_SIZE);
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, ctx->count, 0, 0, 0);
}

void flecsEngine_batch_drawMaterialIndex(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx)
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
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 2, ctx->instance_material_id, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, ctx->mesh.index_count, ctx->count, 0, 0, 0);
}

void flecsEngine_batch_uploadInstances(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t offset,
    const FlecsRgba *colors,
    const FlecsPbrMaterial *materials,
    const FlecsEmissive *emissives,
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

    if (colors) {
        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_color,
            (uint64_t)offset * sizeof(flecs_rgba_t),
            colors,
            (uint64_t)count * sizeof(flecs_rgba_t));
    } else {
        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_color,
            (uint64_t)offset * sizeof(flecs_rgba_t),
            flecsEngine_defaultAttrCache_getColor(engine, count),
            (uint64_t)count * sizeof(flecs_rgba_t));
    }

    if (materials) {
        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_pbr,
            (uint64_t)offset * sizeof(FlecsPbrMaterial),
            materials,
            (uint64_t)count * sizeof(FlecsPbrMaterial));
    } else {
        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_pbr,
            (uint64_t)offset * sizeof(FlecsPbrMaterial),
            flecsEngine_defaultAttrCache_getMaterial(engine, count),
            (uint64_t)count * sizeof(FlecsPbrMaterial));
    }

    if (emissives) {
        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_emissive,
            (uint64_t)offset * sizeof(FlecsEmissive),
            emissives,
            (uint64_t)count * sizeof(FlecsEmissive));
    } else {
        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_emissive,
            (uint64_t)offset * sizeof(FlecsEmissive),
            flecsEngine_defaultAttrCache_getEmissive(engine, count),
            (uint64_t)count * sizeof(FlecsEmissive));
    }
}

void flecsEngine_batch_uploadMaterialIds(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t offset,
    const FlecsMaterialId *material_id,
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

    FlecsMaterialId *matIds = flecsEngine_batch_ensureMaterialIds(ctx, count);
    for (int i = 0; i < count; i ++) {
        matIds[i] = material_id[0];
    }

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->instance_material_id,
        (uint64_t)offset * sizeof(FlecsMaterialId),
        matIds,
        (uint64_t)count * sizeof(FlecsMaterialId));
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
