#include "renderer.h"
#include "flecs_engine.h"

static WGPURenderPassEncoder flecsEngine_renderBatch_beginPass(
    const FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView color_view,
    WGPULoadOp color_load_op)
{
    WGPUColor clear_color = flecsEngine_getClearColor(impl);

    WGPURenderPassColorAttachment color_attachment = {
        .view = color_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = color_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = clear_color
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

static void flecsEngine_renderBatch_extractSet(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set)
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
            flecsEngine_renderBatch_extractSet(
                world,
                engine,
                nested_batch_set);
            continue;
        }

        flecsEngine_renderBatch_extract(world, engine, batch_entity);
    }
}

static void flecsEngine_renderBatch_renderSet(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    const FlecsRenderBatchSet *batch_set)
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
            flecsEngine_renderBatch_renderSet(
                world,
                engine,
                pass,
                view,
                nested_batch_set);
            continue;
        }

        flecsEngine_renderBatch_render(world, engine, pass, view, batch_entity);
    }
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

    flecsEngine_renderBatch_extractSet(world, engine, batch_set);
}

void flecsEngine_renderView_renderBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    const FlecsRenderViewImpl *viewImpl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    WGPURenderPassEncoder batch_pass = flecsEngine_renderBatch_beginPass(
        engine,
        encoder,
        viewImpl->effect_target_views[0],
        WGPULoadOp_Clear);

    /* Always set pipeline/uniforms for first batch in view */
    engine->last_pipeline = NULL;

    flecsEngine_renderBatch_renderSet(
        world,
        engine,
        batch_pass,
        view,
        batch_set);
        
    wgpuRenderPassEncoderEnd(batch_pass);
    wgpuRenderPassEncoderRelease(batch_pass);
}
