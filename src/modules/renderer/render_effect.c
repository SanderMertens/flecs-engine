#include "renderer.h"
#include "flecs_engine.h"

static WGPURenderPipeline flecsEngine_renderEffect_createPipeline(
    const FlecsEngineImpl *engine,
    const FlecsShader *shader,
    const FlecsShaderImpl *shader_impl,
    WGPUBindGroupLayout bind_layout,
    WGPUTextureFormat color_format);

ECS_COMPONENT_DECLARE(FlecsRenderEffect);
ECS_COMPONENT_DECLARE(FlecsRenderEffectImpl);

ECS_DTOR(FlecsRenderEffect, ptr, {
    if (ptr->ctx && ptr->free_ctx) {
        ptr->free_ctx(ptr->ctx);
    }
})

ECS_MOVE(FlecsRenderEffect, dst, src, {
    if (dst->ctx && dst->free_ctx) {
        dst->free_ctx(dst->ctx);
    }
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_renderEffect_release(
    FlecsRenderEffectImpl *ptr)
{
    if (ptr->input_sampler) {
        wgpuSamplerRelease(ptr->input_sampler);
        ptr->input_sampler = NULL;
    }

    if (ptr->pipeline_surface) {
        wgpuRenderPipelineRelease(ptr->pipeline_surface);
        ptr->pipeline_surface = NULL;
    }

    if (ptr->pipeline_hdr) {
        wgpuRenderPipelineRelease(ptr->pipeline_hdr);
        ptr->pipeline_hdr = NULL;
    }

    if (ptr->bind_layout) {
        wgpuBindGroupLayoutRelease(ptr->bind_layout);
        ptr->bind_layout = NULL;
    }
}

ECS_DTOR(FlecsRenderEffectImpl, ptr, {
    flecsEngine_renderEffect_release(ptr);
})

ECS_MOVE(FlecsRenderEffectImpl, dst, src, {
    flecsEngine_renderEffect_release(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static bool flecsEngine_renderEffect_createInputSampler(
    const FlecsEngineImpl *engine,
    FlecsRenderEffectImpl *impl)
{
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    };

    impl->input_sampler = wgpuDeviceCreateSampler(engine->device, &sampler_desc);
    return impl->input_sampler != NULL;
}

static WGPURenderPassEncoder flecsEngine_renderEffect_beginPass(
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

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment
    };

    return wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
}

void flecsEngine_renderEffect_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUTextureView input_view,
    WGPUTextureFormat output_format)
{
    ecs_assert(effect != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(impl != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(input_view != NULL, ECS_INVALID_PARAMETER, NULL);

    WGPUBindGroupEntry entries[8] = {
        { .binding = 0, .textureView = input_view },
        { .binding = 1, .sampler = impl->input_sampler }
    };

    uint32_t entry_count = 2;
    if (effect->bind_callback) {
        bool bind_ok = effect->bind_callback(
            world,
            engine,
            effect_entity,
            effect,
            impl,
            entries,
            &entry_count);
        ecs_assert(bind_ok, ECS_INTERNAL_ERROR, NULL);
    }
    ecs_assert(entry_count > 0, ECS_INTERNAL_ERROR, NULL);
    ecs_assert(entry_count <= 8, ECS_INTERNAL_ERROR, NULL);

    WGPUBindGroupDescriptor bind_group_desc = {
        .layout = impl->bind_layout,
        .entryCount = entry_count,
        .entries = entries
    };

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &bind_group_desc);
    ecs_assert(bind_group != NULL, ECS_INTERNAL_ERROR, NULL);

    WGPURenderPipeline pipeline = output_format == engine->surface_config.format
        ? impl->pipeline_surface
        : impl->pipeline_hdr;
    ecs_assert(pipeline != NULL, ECS_INTERNAL_ERROR, NULL);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    wgpuBindGroupRelease(bind_group);
}

/* Resolve the input index for an effect, scanning back past any disabled
 * effects to find the first enabled effect's output. Returns 0 (the batch
 * framebuffer) if no enabled effect is found before the batch output. */
static int32_t flecsEngine_resolveEffectInput(
    const flecs_render_view_effect_t *effects,
    int32_t input)
{
    while (input > 0) {
        if (effects[input - 1].enabled) {
            return input;
        }
        input--;
    }
    return 0;
}

void flecsEngine_renderView_renderEffects(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *viewImpl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    int32_t effect_count = ecs_vec_count(&view->effects);
    const flecs_render_view_effect_t *effects = NULL;

    /* Find the last enabled effect. */
    int32_t last_enabled = -1;
    if (effect_count > 0) {
        effects = ecs_vec_first(&view->effects);
        for (int32_t i = effect_count - 1; i >= 0; i --) {
            if (effects[i].enabled) {
                last_enabled = i;
                break;
            }
        }
    }

    /* No effects enabled — blit batch output to screen via passthrough. */
    if (last_enabled < 0) {
        WGPUBindGroupEntry entries[2] = {
            { .binding = 0, .textureView = viewImpl->effect_target_views[0] },
            { .binding = 1, .sampler = engine->passthrough_sampler }
        };
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = engine->passthrough_bind_layout,
                .entryCount = 2,
                .entries = entries
            });

        WGPURenderPassEncoder pass = flecsEngine_renderEffect_beginPass(
            engine, encoder, view_texture, WGPULoadOp_Load);
        wgpuRenderPassEncoderSetPipeline(pass, engine->passthrough_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(bg);
        return;
    }

    for (int32_t i = 0; i < effect_count; i ++) {
        if (!effects[i].enabled) {
            continue;
        }

        ecs_entity_t entity = effects[i].effect;
        const FlecsRenderEffect *effect = ecs_get(
            world, entity, FlecsRenderEffect);
        FlecsRenderEffectImpl *effect_impl = ecs_get_mut(
            world, entity, FlecsRenderEffectImpl);

        ecs_assert(effect != NULL, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(effect_impl != NULL, ECS_INVALID_PARAMETER, NULL);

        ecs_assert(effect->input >= 0, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(effect->input <= i, ECS_INVALID_PARAMETER, NULL);

        bool is_last = (i == last_enabled);
        WGPUTextureView output_view = is_last
            ? view_texture
            : viewImpl->effect_target_views[i + 1];
        WGPUTextureFormat output_format = is_last
            ? engine->surface_config.format
            : viewImpl->effect_target_format;

        int32_t resolved_input = flecsEngine_resolveEffectInput(
            effects, effect->input);
        WGPUTextureView input_view =
            viewImpl->effect_target_views[resolved_input];
        WGPULoadOp load_op = is_last ? WGPULoadOp_Load : WGPULoadOp_Clear;

        if (effect->render_callback) {
            bool render_ok = effect->render_callback(
                world,
                engine,
                encoder,
                entity,
                effect,
                effect_impl,
                input_view,
                viewImpl->effect_target_format,
                output_view,
                output_format,
                load_op);
            if (!render_ok) {
                ecs_err("failed to render effect");
                return;
            }
            continue;
        }

        WGPURenderPassEncoder effect_pass = flecsEngine_renderEffect_beginPass(
            engine,
            encoder,
            output_view,
            load_op);

        flecsEngine_renderEffect_render(
            world,
            engine,
            effect_pass,
            entity,
            effect,
            effect_impl,
            input_view,
            output_format);

        wgpuRenderPassEncoderEnd(effect_pass);
        wgpuRenderPassEncoderRelease(effect_pass);
    }
}

static WGPURenderPipeline flecsEngine_renderEffect_createPipeline(
    const FlecsEngineImpl *engine,
    const FlecsShader *shader,
    const FlecsShaderImpl *shader_impl,
    WGPUBindGroupLayout bind_layout,
    WGPUTextureFormat color_format)
{
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &bind_layout
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &pipeline_layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUColorTargetState color_target = {
        .format = color_format,
        .writeMask = WGPUColorWriteMask_All
    };

    WGPUVertexState vertex_state = {
        .module = shader_impl->shader_module,
        .entryPoint = WGPU_STR(shader->vertex_entry ? shader->vertex_entry : "vs_main")
    };

    WGPUFragmentState fragment_state = {
        .module = shader_impl->shader_module,
        .entryPoint = WGPU_STR(shader->fragment_entry ? shader->fragment_entry : "fs_main"),
        .targetCount = 1,
        .targets = &color_target
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = vertex_state,
        .fragment = &fragment_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = WGPU_MULTISAMPLE_DEFAULT
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);

    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

static void FlecsRenderEffect_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsRenderEffect *effects = ecs_field(it, FlecsRenderEffect, 0);
    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    if (!engine) {
        ecs_err("cannot build render effects: engine is not initialized");
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        FlecsRenderEffectImpl impl = {};

        if (!effects[i].shader) {
            char *effect_name = ecs_get_path(world, e);
            ecs_err("missing shader asset for render effect %s", effect_name);
            ecs_os_free(effect_name);
            continue;
        }

        const FlecsShader *shader = ecs_get(world, effects[i].shader, FlecsShader);
        if (!shader) {
            char *effect_name = ecs_get_path(world, e);
            char *shader_name = ecs_get_path(world, effects[i].shader);
            ecs_err("invalid shader asset '%s' for render effect %s",
                shader_name, effect_name);
            ecs_os_free(shader_name);
            ecs_os_free(effect_name);
            continue;
        }

        const FlecsShaderImpl *shader_impl = ecs_get(
            world, effects[i].shader, FlecsShaderImpl);
        if (!shader_impl || !shader_impl->shader_module) {
            char *effect_name = ecs_get_path(world, e);
            ecs_err("missing compiled shader for render effect %s", effect_name);
            ecs_os_free(effect_name);
            continue;
        }

        if (!flecsEngine_renderEffect_createInputSampler(engine, &impl)) {
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        WGPUBindGroupLayoutEntry layout_entries[8] = {0};
        uint32_t layout_entry_count = 2;

        layout_entries[0] = (WGPUBindGroupLayoutEntry){
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        };

        layout_entries[1] = (WGPUBindGroupLayoutEntry){
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        };

        if (effects[i].setup_callback) {
            if (!effects[i].setup_callback(
                world,
                engine,
                e,
                &effects[i],
                &impl,
                layout_entries,
                &layout_entry_count))
            {
                flecsEngine_renderEffect_release(&impl);
                continue;
            }
        }

        if (layout_entry_count > 2 && !effects[i].bind_callback) {
            char *effect_name = ecs_get_path(world, e);
            ecs_err(
                "render effect %s has custom setup bindings but no bind callback",
                effect_name);
            ecs_os_free(effect_name);
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        ecs_assert(layout_entry_count > 0, ECS_INTERNAL_ERROR, NULL);
        ecs_assert(layout_entry_count <= 8, ECS_INTERNAL_ERROR, NULL);

        WGPUBindGroupLayoutDescriptor bind_layout_desc = {
            .entries = layout_entries,
            .entryCount = layout_entry_count
        };

        impl.bind_layout = wgpuDeviceCreateBindGroupLayout(
            engine->device, &bind_layout_desc);
        if (!impl.bind_layout) {
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        impl.pipeline_surface = flecsEngine_renderEffect_createPipeline(
            engine,
            shader,
            shader_impl,
            impl.bind_layout,
            engine->surface_config.format);
        if (!impl.pipeline_surface) {
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);

        impl.pipeline_hdr = flecsEngine_renderEffect_createPipeline(
            engine,
            shader,
            shader_impl,
            impl.bind_layout,
            hdr_format);
        if (!impl.pipeline_hdr) {
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        ecs_set_ptr(world, e, FlecsRenderEffectImpl, &impl);
    }
}

void flecsEngine_renderEffect_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsRenderEffect);
    ECS_COMPONENT_DEFINE(world, FlecsRenderEffectImpl);

    ecs_set_hooks(world, FlecsRenderEffect, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderEffect),
        .dtor = ecs_dtor(FlecsRenderEffect),
        .on_set = FlecsRenderEffect_on_set
    });

    ecs_set_hooks(world, FlecsRenderEffectImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderEffectImpl),
        .dtor = ecs_dtor(FlecsRenderEffectImpl)
    });
}
