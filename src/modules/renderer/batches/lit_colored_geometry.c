#include "../renderer.h"
#include "flecs_engine.h"

static const char *kShaderSource =
    "struct Uniforms { mvp : mat4x4<f32> };\n"
    "@group(0) @binding(0) var<uniform> uniforms : Uniforms;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) color : vec3<f32>\n"
    "};\n"
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) color : vec3<f32>\n"
    "};\n"
    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  out.pos = uniforms.mvp * vec4<f32>(input.pos, 1.0);\n"
    "  out.color = input.color;\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(input.color, 1.0);\n"
    "}\n";

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    int32_t count;
    int32_t size;
} flecs_lit_colored_geometry_group_ctx_t;

// New group for prefab registered with query, create instance buffer
static void* flecsEngine_litColoredGeometry_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr)
{
    return ecs_os_calloc_t(flecs_lit_colored_geometry_group_ctx_t);
}

// Prefab group is deleted, delete instance buffer
static void flecsEngine_litColoredGeometry_onGroupDelete(
    ecs_world_t *world,
    uint64_t group_id,
    void *group_ptr,
    void *ptr)
{
    flecs_lit_colored_geometry_group_ctx_t *ctx = group_ptr;
    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    ecs_assert(engine != NULL, ECS_INVALID_OPERATION, NULL);

    if (ctx->size) {
        wgpuBufferRelease(ctx->instance_transform);
        wgpuBufferRelease(ctx->instance_color);
    }

    ecs_os_free(ctx);
}

static void flecsEngine_litColoredGeometry_resizeBuffers(
    const FlecsEngineImpl *engine,
    flecs_lit_colored_geometry_group_ctx_t *ctx)
{
    if (ctx->size) {
        wgpuBufferRelease(ctx->instance_transform);
        wgpuBufferRelease(ctx->instance_color);
    }

    ctx->instance_transform = wgpuDeviceCreateBuffer(engine->device, 
        &(WGPUBufferDescriptor) {
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = ctx->count * sizeof(mat4)
        });

    ctx->instance_color = wgpuDeviceCreateBuffer(engine->device, 
        &(WGPUBufferDescriptor) {
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = ctx->count * sizeof(flecs_rgba_t)
        });

    ctx->size = ctx->count;
}

static void flecsEngine_litColoredGeometry_renderGroup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch,
    uint64_t group_id)
{
    ecs_query_t *q = batch->query;
    flecs_lit_colored_geometry_group_ctx_t *ctx = 
        ecs_query_get_group_ctx(q, group_id);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    const FlecsMesh3 *m = ecs_get(world, group_id, FlecsMesh3);

redo: {
        ecs_iter_t it = ecs_query_iter(world, q);
        ecs_iter_set_group(&it, group_id);

        ctx->count = 0;

        while (ecs_query_next(&it)) {
            if ((ctx->count + it.count) < ctx->size) {
                ecs_assert(m == ecs_field(&it, FlecsMesh3, 0), ECS_INVALID_OPERATION,
                    "instance of prefab '%s' matched with a different mesh");
                FlecsWorldTransform3 *t = ecs_field(&it, FlecsWorldTransform3, 1);
                FlecsRgba *c = ecs_field(&it, FlecsRgba, 2);

                wgpuQueueWriteBuffer(engine->queue, ctx->instance_transform, 
                    ctx->count * sizeof(mat4), 
                    t, it.count * sizeof(mat4));

                wgpuQueueWriteBuffer(engine->queue, ctx->instance_color, 
                    ctx->count * sizeof(flecs_rgba_t), 
                    c, it.count * sizeof(flecs_rgba_t));
            }

            ctx->count += it.count;
        }

        if (ctx->count > ctx->size) {
            flecsEngine_litColoredGeometry_resizeBuffers(engine, ctx);
            goto redo;
        }
    }
}

static void flecsEngine_litColoredGeometry_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    const ecs_map_t *groups = ecs_query_get_groups(batch->query);
    ecs_assert(groups != NULL, ECS_INTERNAL_ERROR, NULL);

    // Render prefab groups
    ecs_map_iter_t git = ecs_map_iter(groups);
    while (ecs_map_next(&git)) {
        uint64_t group = ecs_map_key(&git); // Prefab id
        flecsEngine_litColoredGeometry_renderGroup(
            world, engine, pass, batch, group);
    }
}

ecs_entity_t flecsEngine_createBatch_litColoredGeometry(
    ecs_world_t *world)
{
    ecs_entity_t batch = ecs_new(world);

    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { ecs_id(FlecsWorldTransform3) },
            { ecs_id(FlecsRgba) }
        },

        .cache_kind = EcsQueryCacheAuto,

        // Instanced rendering batches by prefab
        .group_by = EcsIsA,
        .on_group_create = flecsEngine_litColoredGeometry_onGroupCreate,
        .on_group_delete = flecsEngine_litColoredGeometry_onGroupDelete
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
        .callback = flecsEngine_litColoredGeometry_callback
    });

    return batch;
}
