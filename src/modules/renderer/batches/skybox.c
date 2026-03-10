#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_batch_t batch;
    FlecsWorldTransform3 transform;
    FlecsRgba color;
} flecs_engine_skybox_ctx_t;

static flecs_engine_skybox_ctx_t* flecsEngine_skybox_createCtx(
    ecs_world_t *world)
{
    flecs_engine_skybox_ctx_t *result =
        ecs_os_calloc_t(flecs_engine_skybox_ctx_t);
    flecsEngine_batch_init(
        &result->batch, world, flecsEngine_quad_getAsset(world), 0, true, 0, NULL);
    glm_mat4_identity(result->transform.m);
    result->color = (FlecsRgba){255, 255, 255, 255};
    return result;
}

static void flecsEngine_skybox_deleteCtx(
    void *arg)
{
    flecs_engine_skybox_ctx_t *ctx = arg;
    flecsEngine_batch_fini(&ctx->batch);
    ecs_os_free(ctx);
}

static void flecsEngine_skybox_prepareInstance(
    const FlecsEngineImpl *engine,
    flecs_engine_skybox_ctx_t *ctx)
{
    if (ctx->batch.capacity < 1) {
        flecsEngine_batch_ensureCapacity(engine, &ctx->batch, 1);
    }

    flecsEngine_batch_transformInstance(
        &ctx->batch.cpu_transforms[0],
        &ctx->transform,
        2.0f,
        2.0f,
        1.0f);

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

static void flecsEngine_skybox_callback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecs_engine_skybox_ctx_t *ctx = batch->ctx;
    flecsEngine_skybox_prepareInstance(engine, ctx);
    flecsEngine_batch_draw(pass, &ctx->batch);
}

ecs_entity_t flecsEngine_createBatch_skybox(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_skybox(world);

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
        .callback = flecsEngine_skybox_callback,
        .ctx = flecsEngine_skybox_createCtx((ecs_world_t*)world),
        .free_ctx = flecsEngine_skybox_deleteCtx
    });

    ecs_add(world, batch, FlecsSkyboxBatch);

    return batch;
}
