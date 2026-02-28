#include "renderer.h"

#define FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL_IMPL
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);
ECS_COMPONENT_DECLARE(FlecsRenderEffectImpl);
ECS_COMPONENT_DECLARE(FlecsRenderBatch);
ECS_COMPONENT_DECLARE(FlecsRenderEffect);
ECS_COMPONENT_DECLARE(FlecsRenderView);
ECS_COMPONENT_DECLARE(FlecsVertex);
ECS_COMPONENT_DECLARE(FlecsLitVertex);
ECS_COMPONENT_DECLARE(FlecsInstanceTransform);
ECS_COMPONENT_DECLARE(FlecsInstanceColor);
ECS_COMPONENT_DECLARE(FlecsInstanceSize);
ECS_COMPONENT_DECLARE(FlecsUniform);
ECS_COMPONENT_DECLARE(FlecsShader);
ECS_COMPONENT_DECLARE(FlecsShaderImpl);

typedef struct {
    WGPUTexture texture;
    WGPUTextureView view;
} flecs_engine_color_target_t;

ECS_DTOR(FlecsRenderBatch, ptr, {
    if (ptr->ctx && ptr->free_ctx) {
        ptr->free_ctx(ptr->ctx);
    }
})

ECS_CTOR(FlecsRenderView, ptr, {
    ecs_vec_init_t(NULL, &ptr->batches, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &ptr->effects, ecs_entity_t, 0);
    ptr->camera = 0;
})

ECS_MOVE(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    ecs_vec_fini_t(NULL, &dst->effects, ecs_entity_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    ecs_vec_fini_t(NULL, &dst->effects, ecs_entity_t);
    dst->camera = src->camera;
    dst->batches = ecs_vec_copy_t(NULL, &src->batches, ecs_entity_t);
    dst->effects = ecs_vec_copy_t(NULL, &src->effects, ecs_entity_t);
})

ECS_DTOR(FlecsRenderView, ptr, {
    ecs_vec_fini_t(NULL, &ptr->batches, ecs_entity_t);
    ecs_vec_fini_t(NULL, &ptr->effects, ecs_entity_t);
})

static bool flecsEngineAnyViewHasEffects(
    const ecs_world_t *world,
    ecs_query_t *view_query)
{
    ecs_iter_t it = ecs_query_iter(world, view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (ecs_vec_count(&views[i].effects)) {
                ecs_iter_fini(&it);
                return true;
            }
        }
    }

    return false;
}

static bool flecsEngineCreateColorTarget(
    const FlecsEngineImpl *impl,
    flecs_engine_color_target_t *target)
{
    target->texture = NULL;
    target->view = NULL;

    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = (uint32_t)impl->width,
            .height = (uint32_t)impl->height,
            .depthOrArrayLayers = 1
        },
        .format = impl->surface_config.format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    target->texture = wgpuDeviceCreateTexture(impl->device, &color_desc);
    if (!target->texture) {
        return false;
    }

    target->view = wgpuTextureCreateView(target->texture, NULL);
    if (!target->view) {
        wgpuTextureRelease(target->texture);
        target->texture = NULL;
        return false;
    }

    return true;
}

static void flecsEngineReleaseColorTarget(
    flecs_engine_color_target_t *target)
{
    if (target->view) {
        wgpuTextureViewRelease(target->view);
    }

    if (target->texture) {
        wgpuTextureRelease(target->texture);
    }

    target->view = NULL;
    target->texture = NULL;
}

static WGPURenderPassEncoder flecsEngineBeginBatchPass(
    const FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView color_view,
    WGPULoadOp color_load_op)
{
    WGPURenderPassColorAttachment color_attachment = {
        .view = color_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = color_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){ 0.05, 0.05, 0.08, 1.0 }
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
    WGPUCommandEncoder encoder,
    WGPUTextureView color_view,
    WGPULoadOp color_load_op)
{
    WGPURenderPassColorAttachment color_attachment = {
        .view = color_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = color_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){ 0.05, 0.05, 0.08, 1.0 }
    };

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment
    };

    return wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
}

static void flecsEngineRenderViewsWithoutEffects(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    // Color attachment: clear to background and render into swapchain.
    WGPURenderPassColorAttachment color_attachment = {
        .view = view_texture,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){ 0.05, 0.05, 0.08, 1.0 }
    };

    // Depth attachment: clear depth each frame.
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

    // Render pass: bind color and depth attachments.
    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment,
        .depthStencilAttachment = &depth_attachment
    };

    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);

    ecs_iter_t it = ecs_query_iter(world, impl->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngineRenderView(world, impl, pass, &views[i]);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
}

static void flecsEngineRenderViewWithEffects(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
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
        return;
    }

    flecs_engine_color_target_t *targets = ecs_os_calloc_n(
        flecs_engine_color_target_t, effect_count);
    ecs_assert(targets != NULL, ECS_OUT_OF_MEMORY, NULL);

    for (int32_t i = 0; i < effect_count; i ++) {
        bool ok = flecsEngineCreateColorTarget(impl, &targets[i]);
        ecs_assert(ok, ECS_INTERNAL_ERROR, NULL);
    }

    WGPURenderPassEncoder batch_pass = flecsEngineBeginBatchPass(
        impl,
        encoder,
        targets[0].view,
        WGPULoadOp_Clear);

    flecsEngineRenderView(world, impl, batch_pass, view);
    wgpuRenderPassEncoderEnd(batch_pass);

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
            : targets[i + 1].view;

        WGPUTextureView input_view = targets[effect->input].view;

        WGPURenderPassEncoder effect_pass = flecsEngineBeginEffectPass(
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
    }

    for (int32_t i = 0; i < effect_count; i ++) {
        flecsEngineReleaseColorTarget(&targets[i]);
    }

    ecs_os_free(targets);
}

static void flecsEngineRenderViewsWithEffects(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
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

void flecsEngineRenderViews(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    if (!flecsEngineAnyViewHasEffects(world, impl->view_query)) {
        flecsEngineRenderViewsWithoutEffects(
            world,
            impl,
            view_texture,
            encoder);
        return;
    }

    flecsEngineRenderViewsWithEffects(
        world,
        impl,
        view_texture,
        encoder);
}

void FlecsEngineRendererImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineRenderer);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsRenderBatch);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchImpl);
    ECS_COMPONENT_DEFINE(world, FlecsRenderEffect);
    ECS_COMPONENT_DEFINE(world, FlecsRenderEffectImpl);
    ECS_COMPONENT_DEFINE(world, FlecsRenderView);
    ECS_COMPONENT_DEFINE(world, FlecsVertex);
    ECS_COMPONENT_DEFINE(world, FlecsLitVertex);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceTransform);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceColor);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceSize);
    ECS_COMPONENT_DEFINE(world, FlecsUniform);
    ECS_COMPONENT_DEFINE(world, FlecsShader);
    ECS_COMPONENT_DEFINE(world, FlecsShaderImpl);

    ecs_set_hooks(world, FlecsRenderBatch, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(FlecsRenderBatch),
        .on_set = FlecsRenderBatch_on_set
    });

    ecs_set_hooks(world, FlecsRenderBatchImpl, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(FlecsRenderBatchImpl)
    });

    ecs_set_hooks(world, FlecsRenderEffect, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsRenderEffect_on_set
    });

    ecs_set_hooks(world, FlecsRenderEffectImpl, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(FlecsRenderEffectImpl)
    });

    ecs_set_hooks(world, FlecsShader, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsShader_on_set
    });

    ecs_set_hooks(world, FlecsShaderImpl, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(FlecsShaderImpl)
    });

    ecs_set_hooks(world, FlecsRenderView, {
        .ctor = ecs_ctor(FlecsRenderView),
        .move = ecs_move(FlecsRenderView),
        .copy = ecs_copy(FlecsRenderView),
        .dtor = ecs_dtor(FlecsRenderView)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsVertex),
        .members = {
            { .name = "p", .type = ecs_id(flecs_vec3_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsLitVertex),
        .members = {
            { .name = "p", .type = ecs_id(flecs_vec3_t) },
            { .name = "n", .type = ecs_id(flecs_vec3_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsInstanceTransform),
        .members = {
            { .name = "m", .type = ecs_id(flecs_mat4_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsInstanceColor),
        .members = {
            { .name = "c", .type = ecs_id(flecs_rgba_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsInstanceSize),
        .members = {
            { .name = "size", .type = ecs_id(flecs_vec3_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsUniform),
        .members = {
            { .name = "vp", .type = ecs_id(flecs_mat4_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsShader),
        .members = {
            { .name = "source", .type = ecs_id(ecs_string_t) },
            { .name = "vertex_entry", .type = ecs_id(ecs_string_t) },
            { .name = "fragment_entry", .type = ecs_id(ecs_string_t) }
        }
    });
}
