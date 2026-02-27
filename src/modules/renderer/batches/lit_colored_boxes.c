#include "../renderer.h"
#include "../../geometry3/geometry3.h"
#include "flecs_engine.h"

static const int32_t kLitBoxLogFrameLimit = 180;
static int32_t g_lit_box_log_frames = 0;
static const uint64_t kBoxGroupId = 1;
enum {
    kBoxField_Box = 0,
    kBoxField_WorldTransform = 1,
    kBoxField_Color = 2
};

static bool flecsEngineLitBoxLogEnabled(void) {
    return g_lit_box_log_frames < kLitBoxLogFrameLimit;
}

static const char *kShaderSource =
    "struct Uniforms { vp : mat4x4<f32> }\n"
    "@group(0) @binding(0) var<uniform> uniforms : Uniforms;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) nrm : vec3<f32>,\n"
    "  @location(2) m0 : vec4<f32>,\n"
    "  @location(3) m1 : vec4<f32>,\n"
    "  @location(4) m2 : vec4<f32>,\n"
    "  @location(5) m3 : vec4<f32>,\n"
    "  @location(6) color : vec4<f32>\n"
    "};\n"
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) color : vec4<f32>,\n"
    "  @location(1) normal : vec3<f32>\n"
    "};\n"
    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  let model = mat4x4<f32>(input.m0, input.m1, input.m2, input.m3);\n"
    "  let world_pos = model * vec4<f32>(input.pos, 1.0);\n"
    "  out.pos = uniforms.vp * world_pos;\n"
    "  out.normal = normalize((model * vec4<f32>(input.nrm, 0.0)).xyz);\n"
    "  out.color = input.color;\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let light = normalize(vec3<f32>(0.4, 0.8, 0.2));\n"
    "  let diffuse = max(dot(normalize(input.normal), light), 0.0);\n"
    "  return vec4<f32>(input.color.rgb * diffuse, input.color.a);\n"
    "}\n";

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    mat4 *cpu_transforms;
    flecs_rgba_t *cpu_colors;
    int32_t count;
    int32_t capacity;
} flecs_lit_colored_boxes_group_ctx_t;

static void* flecsEngine_litColoredBoxes_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr)
{
    (void)world;
    (void)ptr;

    if (flecsEngineLitBoxLogEnabled()) {
        ecs_dbg("[lit-box] create group=%llu", (unsigned long long)group_id);
    }

    return ecs_os_calloc_t(flecs_lit_colored_boxes_group_ctx_t);
}

static void flecsEngine_litColoredBoxes_onGroupDelete(
    ecs_world_t *world,
    uint64_t group_id,
    void *group_ptr,
    void *ptr)
{
    (void)world;
    (void)group_id;
    (void)ptr;

    flecs_lit_colored_boxes_group_ctx_t *ctx = group_ptr;
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

    if (flecsEngineLitBoxLogEnabled()) {
        ecs_dbg("[lit-box] resize instance buffers: new_capacity=%d", new_capacity);
    }
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

static uint64_t flecsEngine_litColoredBoxes_groupBy(
    ecs_world_t *world,
    ecs_table_t *table,
    ecs_id_t group_id,
    void *ctx)
{
    (void)world;
    (void)table;
    (void)group_id;
    (void)ctx;
    return kBoxGroupId;
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
    ecs_iter_set_group(&count_it, kBoxGroupId);
    while (ecs_query_next(&count_it)) {
        ctx->count += count_it.count;
    }

    if (!ctx->count) {
        return;
    }

    flecsEngine_litColoredBoxes_ensureCapacity(engine, ctx, ctx->count);

    int32_t offset = 0;
    ecs_iter_t write_it = ecs_query_iter(world, q);
    ecs_iter_set_group(&write_it, kBoxGroupId);
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
    flecs_lit_colored_boxes_group_ctx_t *ctx =
        ecs_query_get_group_ctx(batch->query, kBoxGroupId);
    if (!ctx) {
        return;
    }

    flecsEngine_litColoredBoxes_prepareInstances(world, engine, batch, ctx);
    if (!ctx->count) {
        if (flecsEngineLitBoxLogEnabled()) {
            ecs_dbg("[lit-box] no box instances matched");
            g_lit_box_log_frames ++;
        }
        return;
    }

    const FlecsMesh3Impl *mesh = flecsEngineGeometry3EnsureUnitBoxMesh((ecs_world_t*)world);
    if (!mesh || !mesh->vertex_buffer || !mesh->index_buffer || !mesh->index_count) {
        if (flecsEngineLitBoxLogEnabled()) {
            ecs_dbg("[lit-box] unit box mesh missing");
            g_lit_box_log_frames ++;
        }
        return;
    }

    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh->vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, ctx->instance_transform, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 2, ctx->instance_color, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, mesh->index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, (uint32_t)mesh->index_count, (uint32_t)ctx->count, 0, 0, 0);

    if (flecsEngineLitBoxLogEnabled()) {
        ecs_dbg("[lit-box] draw instances=%d indices=%d", ctx->count, mesh->index_count);
        g_lit_box_log_frames ++;
    }
}

ecs_entity_t flecsEngine_createBatch_litColoredBoxes(
    ecs_world_t *world)
{
    ecs_entity_t batch = ecs_new(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsGeometryConflict3), .src.id = EcsSelf, .oper = EcsNot }
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = ecs_id(FlecsBox),
        .group_by_callback = flecsEngine_litColoredBoxes_groupBy,
        .on_group_create = flecsEngine_litColoredBoxes_onGroupCreate,
        .on_group_delete = flecsEngine_litColoredBoxes_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = kShaderSource,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsInstanceColor)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .callback = flecsEngine_litColoredBoxes_callback
    });

    return batch;
}
