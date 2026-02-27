#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "flecs_engine.h"

enum {
    kBoxField_Box = 0,
    kBoxField_WorldTransform = 1,
    kBoxField_Color = 2
};

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    mat4 *cpu_transforms;
    flecs_rgba_t *cpu_colors;
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
    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
    }
    if (ctx->cpu_colors) {
        ecs_os_free(ctx->cpu_colors);
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

    if (ctx->cpu_transforms) {
        ecs_os_free(ctx->cpu_transforms);
    }
    if (ctx->cpu_colors) {
        ecs_os_free(ctx->cpu_colors);
    }

    ctx->cpu_transforms = ecs_os_malloc_n(mat4, new_capacity);
    ctx->cpu_colors = ecs_os_malloc_n(flecs_rgba_t, new_capacity);
    ctx->capacity = new_capacity;
}

static flecs_rgba_t flecsEngine_litColoredBoxes_pickColorSelfOnly(
    const ecs_iter_t *it,
    int32_t row,
    int32_t field_index)
{
    const FlecsRgba *self_color = ecs_field(it, FlecsRgba, field_index);
    if (self_color) {
        if (ecs_field_is_self(it, field_index)) {
            return self_color[row];
        }
        return self_color[0];
    }

    return (flecs_rgba_t){255, 255, 255, 255};
}

static void flecsEngine_litColoredBoxes_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecs_lit_colored_boxes_group_ctx_t *ctx)
{
    ecs_query_t *q = batch->query;
    ctx->count = 0;

    ecs_iter_t count_it = ecs_query_iter(world, q);
    while (ecs_query_next(&count_it)) {
        ctx->count += count_it.count;
    }

    if (!ctx->count) {
        return;
    }

    flecsEngine_litColoredBoxes_ensureCapacity(engine, ctx, ctx->count);

    int32_t offset = 0;
    ecs_iter_t write_it = ecs_query_iter(world, q);
    while (ecs_query_next(&write_it)) {
        const FlecsWorldTransform3 *wt = ecs_field(
            &write_it, FlecsWorldTransform3, kBoxField_WorldTransform);
        const FlecsBox *boxes = ecs_field(&write_it, FlecsBox, kBoxField_Box);

        for (int32_t i = 0; i < write_it.count; i ++) {
            glm_mat4_copy((vec4*)wt[i].m, ctx->cpu_transforms[offset]);

            vec3 size = {boxes[i].x, boxes[i].y, boxes[i].z};
            glm_scale(ctx->cpu_transforms[offset], size);

            ctx->cpu_colors[offset] =
                flecsEngine_litColoredBoxes_pickColorSelfOnly(&write_it, i, kBoxField_Color);
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
