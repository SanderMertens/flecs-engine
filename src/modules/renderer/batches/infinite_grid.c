#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_batch_t batch;
    FlecsWorldTransform3 transform;
    FlecsRgba color;
    flecs_vec3_t size;
} flecs_engine_infinite_grid_ctx_t;

static flecs_engine_infinite_grid_ctx_t* flecsEngine_infinite_grid_createCtx(
    ecs_world_t *world)
{
    flecs_engine_infinite_grid_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_infinite_grid_ctx_t);
    flecsEngine_batch_init(&result->batch, world, flecsEngine_quad_getAsset(world), 0, true, 0, NULL);

    glm_mat4_identity(result->transform.m);
    glm_rotate(result->transform.m, -glm_rad(90.0f), (vec3){1.0f, 0.0f, 0.0f});
    result->color = (flecs_rgba_t){96, 96, 112, 255};
    result->size = (flecs_vec3_t){2000.0f, 2000.0f, 1.0f};

    return result;
}

static void flecsEngine_infinite_grid_deleteCtx(
    void *arg)
{
    flecs_engine_infinite_grid_ctx_t *ctx = arg;
    flecsEngine_batch_fini(&ctx->batch);
    ecs_os_free(ctx);
}

static void flecsEngine_infinite_grid_extract(
    const FlecsEngineImpl *engine,
    flecs_engine_infinite_grid_ctx_t *ctx)
{
    if (ctx->batch.capacity < 1) {
        flecsEngine_batch_ensureCapacity(engine, &ctx->batch, 1);
    }

    flecsEngine_batch_transformInstance(
        &ctx->batch.cpu_transforms[0],
        &ctx->transform,
        ctx->size.x,
        ctx->size.y,
        ctx->size.z);

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->batch.instance_transform,
        0,
        ctx->batch.cpu_transforms,
        sizeof(FlecsInstanceTransform));

    ctx->batch.cpu_colors[0] = ctx->color;

    wgpuQueueWriteBuffer(
        engine->queue,
        ctx->batch.instance_color,
        0,
        ctx->batch.cpu_colors,
        sizeof(FlecsRgba));

    ctx->batch.count = 1;
}

static void flecsEngine_infinite_grid_extractCallback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecs_engine_infinite_grid_ctx_t *ctx = batch->ctx;
    flecsEngine_infinite_grid_extract(engine, ctx);
}

static void flecsEngine_infinite_grid_renderCallback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)engine;

    flecs_engine_infinite_grid_ctx_t *ctx = batch->ctx;
    flecsEngine_batch_draw(pass, &ctx->batch);
}

ecs_entity_t flecsEngine_createBatch_infiniteGrid(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_infiniteGrid(world);

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsRgba)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .extract_callback = flecsEngine_infinite_grid_extractCallback,
        .callback = flecsEngine_infinite_grid_renderCallback,
        .ctx = flecsEngine_infinite_grid_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_infinite_grid_deleteCtx
    });

    return batch;
}
