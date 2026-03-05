#include "renderer.h"
#include "flecs_engine.h"

static WGPURenderPassEncoder flecsEngineBeginBatchPass(
    const FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView color_view,
    WGPULoadOp color_load_op)
{
    WGPUColor clear_color = flecsEngineGetClearColor(impl);

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

static void flecsEngineRenderBatchSet(
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
            flecsEngineRenderBatchSet(
                world,
                engine,
                pass,
                view,
                nested_batch_set);
            continue;
        }

        flecsEngineRenderBatch(world, engine, pass, view, batch_entity);
    }
}

void flecsEngineRenderView_renderBatches(
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

    WGPURenderPassEncoder batch_pass = flecsEngineBeginBatchPass(
        engine,
        encoder,
        viewImpl->effect_target_views[0],
        WGPULoadOp_Clear);

    /* Always set pipeline/uniforms for first batch in view */
    engine->last_pipeline = NULL;

    flecsEngineRenderBatchSet(
        world,
        engine,
        batch_pass,
        view,
        batch_set);
        
    wgpuRenderPassEncoderEnd(batch_pass);
    wgpuRenderPassEncoderRelease(batch_pass);
}
