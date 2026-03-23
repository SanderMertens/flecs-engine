#include "renderer.h"
#include "flecs_engine.h"

static WGPURenderPassEncoder flecsEngine_renderBatch_beginPass(
    const FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView color_view,
    WGPULoadOp color_load_op)
{
    WGPUColor sky_color = flecsEngine_getSkyColor(impl);

    WGPURenderPassColorAttachment color_attachment = {
        .view = color_view,
        WGPU_DEPTH_SLICE
        .loadOp = color_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = sky_color
    };

    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = impl->depth_texture_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 1.0f,
        .depthReadOnly = false,
        .stencilLoadOp = WGPULoadOp_Undefined,
        .stencilStoreOp = WGPUStoreOp_Undefined,
        .stencilClearValue = 0,
        .stencilReadOnly = true
    };

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment,
        .depthStencilAttachment = &depth_attachment
    };

    return wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
}

typedef void (*flecsEngine_batchSetVisitor_t)(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx);

static void flecsEngine_renderBatch_visitSet(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set,
    flecsEngine_batchSetVisitor_t visitor,
    void *ctx)
{
    int32_t i, count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested_batch_set = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested_batch_set) {
            flecsEngine_renderBatch_visitSet(
                world, engine, nested_batch_set, visitor, ctx);
            continue;
        }

        visitor(world, engine, batch_entity, ctx);
    }
}

typedef struct {
    const WGPURenderPassEncoder pass;
    const FlecsRenderView *view;
} flecsEngine_renderVisitorCtx_t;

static void flecsEngine_renderBatch_renderVisitor(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx)
{
    flecsEngine_renderVisitorCtx_t *render_ctx = ctx;
    flecsEngine_renderBatch_render(
        world, engine, render_ctx->pass, render_ctx->view, batch_entity);
}

static void flecsEngine_renderBatch_extractVisitor(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx)
{
    (void)ctx;
    flecsEngine_renderBatch_extract(world, engine, batch_entity);
}

static void flecsEngine_renderBatch_shadowVisitor(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity,
    void *ctx)
{
    flecsEngine_renderVisitorCtx_t *render_ctx = ctx;
    flecsEngine_renderBatch_renderShadow(
        world, engine, render_ctx->pass, batch_entity);
}

void flecsEngine_renderView_extractBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view)
{
    (void)view;

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_renderBatch_visitSet(
        world, engine, batch_set, flecsEngine_renderBatch_extractVisitor, NULL);
}

void flecsEngine_renderView_renderShadow(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    WGPUCommandEncoder encoder)
{
    if (!engine->shadow_texture_view || !view->light) {
        return;
    }

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    if (!batch_set) {
        return;
    }

    /* Compute all cascade light VP matrices and split distances */
    flecsEngine_shadow_computeCascades(
        world, view, engine->shadow_map_size,
        engine->shadow_cascade_sizes,
        engine->current_light_vp, engine->cascade_splits);

    /* Upload all cascade VP matrices to their own buffers upfront.
     * This must happen before encoding any render passes because
     * wgpuQueueWriteBuffer calls resolve before command buffer execution. */
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        wgpuQueueWriteBuffer(
            engine->queue,
            engine->shadow_vp_buffers[c],
            0,
            engine->current_light_vp[c],
            sizeof(mat4));
    }

    /* Render each cascade into its own texture array layer */
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        if (!engine->shadow_layer_views[c]) {
            continue;
        }

        engine->current_shadow_cascade = c;

        /* Begin shadow depth-only render pass for this cascade layer */
        WGPURenderPassDepthStencilAttachment depth_attachment = {
            .view = engine->shadow_layer_views[c],
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Store,
            .depthClearValue = 1.0f,
            .depthReadOnly = false,
            .stencilLoadOp = WGPULoadOp_Undefined,
            .stencilStoreOp = WGPUStoreOp_Undefined,
            .stencilClearValue = 0,
            .stencilReadOnly = true
        };

        WGPURenderPassDescriptor pass_desc = {
            .colorAttachmentCount = 0,
            .colorAttachments = NULL,
            .depthStencilAttachment = &depth_attachment
        };

        WGPURenderPassEncoder shadow_pass = wgpuCommandEncoderBeginRenderPass(
            encoder, &pass_desc);

        /* Restrict rendering to the cascade's effective resolution.
         * Distant cascades use smaller viewports to reduce rasterization
         * cost while the full-size texture layer stores their depth in
         * the top-left sub-region. */
        {
            uint32_t cs = engine->shadow_cascade_sizes[c];
            wgpuRenderPassEncoderSetViewport(
                shadow_pass,
                0.0f, 0.0f,
                (float)cs, (float)cs,
                0.0f, 1.0f);
        }

        engine->last_pipeline = NULL;

        flecsEngine_renderVisitorCtx_t shadow_ctx = {
            .pass = shadow_pass
        };

        flecsEngine_renderBatch_visitSet(
            world, engine, batch_set,
            flecsEngine_renderBatch_shadowVisitor, &shadow_ctx);

        wgpuRenderPassEncoderEnd(shadow_pass);
        wgpuRenderPassEncoderRelease(shadow_pass);
    }
}

void flecsEngine_renderView_renderBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    const FlecsRenderViewImpl *viewImpl,
    WGPUCommandEncoder encoder)
{
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    /* Batches always render into the first effect target. A passthrough
     * effect (or the user's effect chain) blits to the final view texture. */
    WGPUTextureView batch_target = viewImpl->effect_target_views[0];

    WGPURenderPassEncoder batch_pass = flecsEngine_renderBatch_beginPass(
        engine,
        encoder,
        batch_target,
        WGPULoadOp_Clear);

    /* Always set pipeline/uniforms for first batch in view */
    engine->last_pipeline = NULL;

    flecsEngine_renderVisitorCtx_t batch_ctx = {
        .pass = batch_pass,
        .view = view
    };

    flecsEngine_renderBatch_visitSet(
        world, engine, batch_set,
        flecsEngine_renderBatch_renderVisitor, &batch_ctx);

    wgpuRenderPassEncoderEnd(batch_pass);
    wgpuRenderPassEncoderRelease(batch_pass);
}
