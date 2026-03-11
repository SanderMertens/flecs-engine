#include <stddef.h>
#include <string.h>
#include "batches.h"

void flecsEngine_batch_init(
    flecsEngine_batch_t* result,
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    bool owns_material_data,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback)
{
    result->instance_transform = NULL;
    result->instance_color = NULL;
    result->instance_pbr = NULL;
    result->instance_emissive = NULL;
    result->instance_material_id = NULL;
    result->cpu_transforms = NULL;
    result->cpu_colors = NULL;
    result->cpu_pbr_materials = NULL;
    result->cpu_emissives = NULL;
    result->cpu_material_ids = NULL;
    result->count = 0;
    result->capacity = 0;
    if (mesh) {
        result->mesh = *mesh;
    } else {
        ecs_os_zeromem(&result->mesh);
    }

    result->component = component;
    result->component_size = component ? flecsEngine_type_sizeof(world, component) : 0;
    result->scale_callback = scale_callback;
    result->group_id = group_id;
    result->owns_material_data = owns_material_data;
}

flecsEngine_batch_t* flecsEngine_batch_create(
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    bool owns_material_data,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback)
{
    flecsEngine_batch_t *result = ecs_os_calloc_t(flecsEngine_batch_t);
    flecsEngine_batch_init(result, world, mesh, group_id, owns_material_data, 
        component, scale_callback);
    return result;
}

static void flecsEngine_batch_releaseInstanceBuffers(
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
}

static void flecsEngine_batch_releaseCpuBuffers(
    flecsEngine_batch_t *ctx)
{
    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
        ctx->cpu_transforms = NULL;
    }
    if (ctx->cpu_colors) {
        ecs_os_free(ctx->cpu_colors);
        ctx->cpu_colors = NULL;
    }
    if (ctx->cpu_pbr_materials) {
        ecs_os_free(ctx->cpu_pbr_materials);
        ctx->cpu_pbr_materials = NULL;
    }
    if (ctx->cpu_emissives) {
        ecs_os_free(ctx->cpu_emissives);
        ctx->cpu_emissives = NULL;
    }
    if (ctx->cpu_material_ids) {
        ecs_os_free(ctx->cpu_material_ids);
        ctx->cpu_material_ids = NULL;
    }
}

void flecsEngine_batch_fini(
    flecsEngine_batch_t *ptr)
{
    flecsEngine_batch_t *ctx = ptr;
    flecsEngine_batch_releaseInstanceBuffers(ctx);
    flecsEngine_batch_releaseCpuBuffers(ctx);

    ctx->count = 0;
    ctx->capacity = 0;
}

static void flecsEngine_batch_resizeMaterialData(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t new_capacity)
{
    flecsEngine_batch_releaseInstanceBuffers(ctx);
    flecsEngine_batch_releaseCpuBuffers(ctx);

    ctx->instance_transform = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsInstanceTransform)
        });

    ctx->instance_color = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsRgba)
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

    ctx->cpu_transforms =
        ecs_os_malloc_n(FlecsInstanceTransform, new_capacity);
    ctx->cpu_colors = ecs_os_malloc_n(FlecsRgba, new_capacity);
    ctx->cpu_pbr_materials =
        ecs_os_malloc_n(FlecsPbrMaterial, new_capacity);
    ctx->cpu_emissives = ecs_os_malloc_n(FlecsEmissive, new_capacity);
    ctx->capacity = new_capacity;
}

static void flecsEngine_batch_resizeMaterialIds(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t new_capacity)
{
    flecsEngine_batch_releaseInstanceBuffers(ctx);
    flecsEngine_batch_releaseCpuBuffers(ctx);

    ctx->instance_transform = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsInstanceTransform)
        });

    ctx->instance_material_id = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsMaterialId)
        });

    ctx->cpu_transforms =
        ecs_os_malloc_n(FlecsInstanceTransform, new_capacity);
    ctx->cpu_material_ids = ecs_os_malloc_n(FlecsMaterialId, new_capacity);
    ctx->capacity = new_capacity;
}

void flecsEngine_batch_delete(
    void *ptr)
{
    flecsEngine_batch_fini(ptr);
    ecs_os_free(ptr);
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

    if (ctx->owns_material_data) {
        flecsEngine_batch_resizeMaterialData(engine, ctx, new_capacity);
    } else {
        flecsEngine_batch_resizeMaterialIds(engine, ctx, new_capacity);
    }
}

static void flecsEngine_batch_copyMaterialData(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t offset,
    int32_t count,
    const FlecsRgba *colors,
    const FlecsPbrMaterial *materials,
    const FlecsEmissive *emissives)
{
    FlecsRgba *cpu_colors = &ctx->cpu_colors[offset];
    FlecsPbrMaterial *cpu_materials = &ctx->cpu_pbr_materials[offset];
    FlecsEmissive *cpu_emissives = &ctx->cpu_emissives[offset];

    memcpy(
        cpu_colors,
        colors ? colors : flecsEngine_defaultAttrCache_getColor(engine, count),
        (size_t)count * sizeof(FlecsRgba));

    memcpy(
        cpu_materials,
        materials ? materials : flecsEngine_defaultAttrCache_getMaterial(engine, count),
        (size_t)count * sizeof(FlecsPbrMaterial));

    memcpy(
        cpu_emissives,
        emissives ? emissives : flecsEngine_defaultAttrCache_getEmissive(engine, count),
        (size_t)count * sizeof(FlecsEmissive));
}

static void flecsEngine_batch_copyMaterialIds(
    flecsEngine_batch_t *ctx,
    int32_t offset,
    int32_t count,
    const FlecsMaterialId *material_id)
{
    FlecsMaterialId *mat_ids = &ctx->cpu_material_ids[offset];
    for (int32_t i = 0; i < count; i ++) {
        mat_ids[i] = material_id[0];
    }
}

static void flecsEngine_batch_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *ctx)
{
    int32_t count = ctx->count;
    if (!count) {
        return;
    }

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->instance_transform,
        0,
        ctx->cpu_transforms,
        (uint64_t)count * sizeof(FlecsInstanceTransform));

    if (ctx->owns_material_data) {
        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_color,
            0,
            ctx->cpu_colors,
            (uint64_t)count * sizeof(FlecsRgba));

        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_pbr,
            0,
            ctx->cpu_pbr_materials,
            (uint64_t)count * sizeof(FlecsPbrMaterial));

        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_emissive,
            0,
            ctx->cpu_emissives,
            (uint64_t)count * sizeof(FlecsEmissive));
    } else {
        wgpuQueueWriteBuffer(
            engine->queue,
            ctx->instance_material_id,
            0,
            ctx->cpu_material_ids,
            (uint64_t)count * sizeof(FlecsMaterialId));
    }
}

void flecsEngine_batch_extractInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx)
{
redo: {
        ctx->count = 0;

        ecs_iter_t it = ecs_query_iter(world, batch->query);
        ecs_iter_set_group(&it, ctx->group_id);
        while (ecs_query_next(&it)) {
            const FlecsWorldTransform3 *wt = ecs_field(
                &it, FlecsWorldTransform3, 1);

            if ((ctx->count + it.count) <= ctx->capacity) {
                if (ctx->scale_callback) {
                    const void *scale_data = ecs_field_w_size(
                        &it, ctx->component_size, 0);
                    for (int32_t i = 0; i < it.count; i ++) {
                        void *ptr = ECS_ELEM(scale_data, ctx->component_size, i);
                        int32_t index = ctx->count + i;
                        vec3 scale;
                        ctx->scale_callback(ptr, scale);
                        flecsEngine_batch_transformInstance(
                            &ctx->cpu_transforms[index],
                            &wt[i], scale[0], scale[1], scale[2]);
                    }
                } else {
                    for (int32_t i = 0; i < it.count; i ++) {
                        int32_t index = ctx->count + i;

                        flecsEngine_batch_transformInstance(
                            &ctx->cpu_transforms[index],
                            &wt[i], 1.0f, 1.0f, 1.0f);
                    }
                }

                if (ctx->owns_material_data) {
                    flecsEngine_batch_copyMaterialData(
                        engine,
                        ctx,
                        ctx->count,
                        it.count,
                        ecs_field(&it, FlecsRgba, 2),
                        ecs_field(&it, FlecsPbrMaterial, 3),
                        ecs_field(&it, FlecsEmissive, 4));
                } else {
                    flecsEngine_batch_copyMaterialIds(
                        ctx,
                        ctx->count,
                        it.count,
                        ecs_field(&it, FlecsMaterialId, 2));
                }
            }

            ctx->count += it.count;
        }

        if (ctx->count > ctx->capacity) {
            flecsEngine_batch_ensureCapacity(engine, ctx, ctx->count);
            ecs_assert(ctx->count <= ctx->capacity, ECS_INTERNAL_ERROR, NULL);
            goto redo;
        }

        flecsEngine_batch_upload(engine, ctx);
    }
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

    if (ctx->owns_material_data) {
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, ctx->instance_color, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 3, ctx->instance_pbr, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 4, ctx->instance_emissive, 0, WGPU_WHOLE_SIZE);
    } else {
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, ctx->instance_material_id, 0, WGPU_WHOLE_SIZE);
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(
        pass, ctx->mesh.index_count, ctx->count, 0, 0, 0);
}

void flecsEngine_batch_transformInstance(
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

void flecsEngine_box_scale(
    const void *ptr, 
    float *scale) 
{
    const FlecsBox *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

void flecsEngine_quad_scale(
    const void *ptr, 
    float *scale) 
{
    const FlecsQuad *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

void flecsEngine_triangle_scale(
    const void *ptr, 
    float *scale) 
{
    const FlecsTriangle *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

void flecsEngine_right_triangle_scale(
    const void *ptr, 
    float *scale) 
{
    const FlecsRightTriangle *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

void flecsEngine_triangle_prism_scale(
    const void *ptr, 
    float *scale) 
{
    const FlecsTrianglePrism *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

void flecsEngine_right_triangle_prism_scale(
    const void *ptr,
    float *scale)
{
    const FlecsRightTrianglePrism *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}
