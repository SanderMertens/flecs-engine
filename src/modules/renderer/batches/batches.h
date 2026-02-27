#ifndef FLECS_ENGINE_BATCHES_H
#define FLECS_ENGINE_BATCHES_H

#include "../renderer.h"

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    WGPUBuffer instance_size;
    int32_t count;
    int32_t capacity;
    FlecsMesh3Impl mesh;
} flecs_engine_batch_ctx_t;

void flecsEngine_batchCtx_init(
    flecs_engine_batch_ctx_t *ctx,
    const FlecsMesh3Impl *mesh);

void flecsEngine_batchCtx_fini(
    flecs_engine_batch_ctx_t *ctx);

void flecsEngine_batchCtx_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecs_engine_batch_ctx_t *ctx,
    int32_t count);

void flecsEngine_batchCtx_draw(
    const WGPURenderPassEncoder pass,
    const flecs_engine_batch_ctx_t *ctx);

#endif
