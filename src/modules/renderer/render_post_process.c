#include "renderer.h"
#include "flecs_engine.h"

void flecsEngineReleaseEffectTargets(
    FlecsEngineImpl *impl)
{
    if (!impl) {
        return;
    }

    for (int32_t i = 0; i < impl->effect_target_count; i ++) {
        if (impl->effect_target_views && impl->effect_target_views[i]) {
            wgpuTextureViewRelease(impl->effect_target_views[i]);
            impl->effect_target_views[i] = NULL;
        }
        if (impl->effect_target_textures && impl->effect_target_textures[i]) {
            wgpuTextureRelease(impl->effect_target_textures[i]);
            impl->effect_target_textures[i] = NULL;
        }
    }

    if (impl->effect_target_views) {
        ecs_os_free(impl->effect_target_views);
        impl->effect_target_views = NULL;
    }
    if (impl->effect_target_textures) {
        ecs_os_free(impl->effect_target_textures);
        impl->effect_target_textures = NULL;
    }

    impl->effect_target_count = 0;
    impl->effect_target_width = 0;
    impl->effect_target_height = 0;
    impl->effect_target_format = WGPUTextureFormat_Undefined;
}

static bool flecsEngineEnsureEffectTargets(
    FlecsEngineImpl *impl,
    int32_t effect_count)
{
    if (effect_count <= 0) {
        return true;
    }

    uint32_t width = (uint32_t)impl->width;
    uint32_t height = (uint32_t)impl->height;
    WGPUTextureFormat format = impl->surface_config.format;

    bool need_recreate = false;
    if (impl->effect_target_count < effect_count) {
        need_recreate = true;
    }
    if (!impl->effect_target_textures || !impl->effect_target_views) {
        need_recreate = true;
    }
    if (impl->effect_target_width != width ||
        impl->effect_target_height != height ||
        impl->effect_target_format != format)
    {
        need_recreate = true;
    }

    if (!need_recreate) {
        return true;
    }

    flecsEngineReleaseEffectTargets(impl);

    impl->effect_target_textures = ecs_os_calloc_n(WGPUTexture, effect_count);
    impl->effect_target_views = ecs_os_calloc_n(WGPUTextureView, effect_count);
    if (!impl->effect_target_textures || !impl->effect_target_views) {
        flecsEngineReleaseEffectTargets(impl);
        return false;
    }

    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    for (int32_t i = 0; i < effect_count; i ++) {
        impl->effect_target_textures[i] = wgpuDeviceCreateTexture(
            impl->device, &color_desc);
        if (!impl->effect_target_textures[i]) {
            flecsEngineReleaseEffectTargets(impl);
            return false;
        }

        impl->effect_target_views[i] = wgpuTextureCreateView(
            impl->effect_target_textures[i], NULL);
        if (!impl->effect_target_views[i]) {
            flecsEngineReleaseEffectTargets(impl);
            return false;
        }
    }

    impl->effect_target_count = effect_count;
    impl->effect_target_width = width;
    impl->effect_target_height = height;
    impl->effect_target_format = format;
    return true;
}

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

static WGPURenderPassEncoder flecsEngineBeginEffectPass(
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

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment
    };

    return wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
}

static void flecsEngineRenderViewWithEffects(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    const FlecsRenderView *view,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder,
    bool clear_output)
{
    int32_t effect_count = ecs_vec_count(&view->effects);

    if (!effect_count) {
        WGPURenderPassEncoder pass = flecsEngineBeginBatchPass(
            impl,
            encoder,
            view_texture,
            clear_output ? WGPULoadOp_Clear : WGPULoadOp_Load);

        flecsEngineRenderView(world, impl, pass, view);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        return;
    }

    if (!flecsEngineEnsureEffectTargets(impl, effect_count)) {
        ecs_err("failed to allocate effect render targets");
        return;
    }

    WGPURenderPassEncoder batch_pass = flecsEngineBeginBatchPass(
        impl,
        encoder,
        impl->effect_target_views[0],
        WGPULoadOp_Clear);

    flecsEngineRenderView(world, impl, batch_pass, view);
    wgpuRenderPassEncoderEnd(batch_pass);
    wgpuRenderPassEncoderRelease(batch_pass);

    ecs_entity_t *effect_entities = ecs_vec_first(&view->effects);
    for (int32_t i = 0; i < effect_count; i ++) {
        ecs_entity_t effect_entity = effect_entities[i];
        const FlecsRenderEffect *effect = ecs_get(
            world, effect_entity, FlecsRenderEffect);
        const FlecsRenderEffectImpl *effect_impl = ecs_get(
            world, effect_entity, FlecsRenderEffectImpl);

        ecs_assert(effect != NULL, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(effect_impl != NULL, ECS_INVALID_PARAMETER, NULL);

        ecs_assert(effect->input >= 0, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(effect->input <= i, ECS_INVALID_PARAMETER, NULL);

        bool is_last = (i + 1) == effect_count;
        WGPUTextureView output_view = is_last
            ? view_texture
            : impl->effect_target_views[i + 1];

        WGPUTextureView input_view = impl->effect_target_views[effect->input];

        WGPURenderPassEncoder effect_pass = flecsEngineBeginEffectPass(
            impl,
            encoder,
            output_view,
            is_last && !clear_output
                ? WGPULoadOp_Load
                : WGPULoadOp_Clear);

        flecsEngineRenderEffect(
            world,
            impl,
            effect_pass,
            effect,
            effect_impl,
            input_view);

        wgpuRenderPassEncoderEnd(effect_pass);
        wgpuRenderPassEncoderRelease(effect_pass);
    }
}

void flecsEngineRenderViewsWithEffects(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    bool clear_output = true;

    ecs_iter_t it = ecs_query_iter(world, impl->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngineRenderViewWithEffects(
                world,
                impl,
                &views[i],
                view_texture,
                encoder,
                clear_output);
            clear_output = false;
        }
    }
}
