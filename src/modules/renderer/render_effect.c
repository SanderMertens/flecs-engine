#include "renderer.h"
#include "flecs_engine.h"

static void flecsRenderEffectImplRelease(
    FlecsRenderEffectImpl *ptr)
{
    if (ptr->tony_lut_sampler) {
        wgpuSamplerRelease(ptr->tony_lut_sampler);
        ptr->tony_lut_sampler = NULL;
    }

    if (ptr->tony_lut_texture_view) {
        wgpuTextureViewRelease(ptr->tony_lut_texture_view);
        ptr->tony_lut_texture_view = NULL;
    }

    if (ptr->tony_lut_texture) {
        wgpuTextureRelease(ptr->tony_lut_texture);
        ptr->tony_lut_texture = NULL;
    }

    if (ptr->input_sampler) {
        wgpuSamplerRelease(ptr->input_sampler);
        ptr->input_sampler = NULL;
    }

    if (ptr->pipeline) {
        wgpuRenderPipelineRelease(ptr->pipeline);
        ptr->pipeline = NULL;
    }

    if (ptr->bind_layout) {
        wgpuBindGroupLayoutRelease(ptr->bind_layout);
        ptr->bind_layout = NULL;
    }
}

ECS_DTOR(FlecsRenderEffectImpl, ptr, {
    flecsRenderEffectImplRelease(ptr);
})

static bool flecsRenderEffectCreateInputSampler(
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

void FlecsRenderEffect_on_set(
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
            ecs_err("missing shader asset for render effect %s",
                effect_name ? effect_name : "<unnamed>");
            ecs_os_free(effect_name);
            continue;
        }

        if (!effects[i].setup_callback || !effects[i].bind_callback) {
            char *effect_name = ecs_get_path(world, e);
            ecs_err("missing setup/bind callback for render effect %s",
                effect_name ? effect_name : "<unnamed>");
            ecs_os_free(effect_name);
            continue;
        }

        const FlecsShader *shader = ecs_get(world, effects[i].shader, FlecsShader);
        if (!shader) {
            char *effect_name = ecs_get_path(world, e);
            char *shader_name = ecs_get_path(world, effects[i].shader);
            ecs_err("invalid shader asset '%s' for render effect %s",
                shader_name ? shader_name : "<unnamed>",
                effect_name ? effect_name : "<unnamed>");
            ecs_os_free(shader_name);
            ecs_os_free(effect_name);
            continue;
        }

        const FlecsShaderImpl *shader_impl = ecs_get(
            world, effects[i].shader, FlecsShaderImpl);
        if (!shader_impl || !shader_impl->shader_module) {
            char *effect_name = ecs_get_path(world, e);
            ecs_err("missing compiled shader for render effect %s",
                effect_name ? effect_name : "<unnamed>");
            ecs_os_free(effect_name);
            continue;
        }

        if (!flecsRenderEffectCreateInputSampler(engine, &impl)) {
            flecsRenderEffectImplRelease(&impl);
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

        if (!effects[i].setup_callback(
            world,
            engine,
            &effects[i],
            &impl,
            layout_entries,
            &layout_entry_count))
        {
            flecsRenderEffectImplRelease(&impl);
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
            flecsRenderEffectImplRelease(&impl);
            continue;
        }

        WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &impl.bind_layout
        };

        WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
            engine->device, &pipeline_layout_desc);
        if (!pipeline_layout) {
            flecsRenderEffectImplRelease(&impl);
            continue;
        }

        WGPUColorTargetState color_target = {
            .format = engine->surface_config.format,
            .writeMask = WGPUColorWriteMask_All
        };

        WGPUVertexState vertex_state = {
            .module = shader_impl->shader_module,
            .entryPoint = (WGPUStringView){
                .data = shader->vertex_entry ? shader->vertex_entry : "vs_main",
                .length = WGPU_STRLEN
            }
        };

        WGPUFragmentState fragment_state = {
            .module = shader_impl->shader_module,
            .entryPoint = (WGPUStringView){
                .data = shader->fragment_entry ? shader->fragment_entry : "fs_main",
                .length = WGPU_STRLEN
            },
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
            .multisample = {
                .count = 1
            }
        };

        impl.pipeline = wgpuDeviceCreateRenderPipeline(
            engine->device, &pipeline_desc);

        wgpuPipelineLayoutRelease(pipeline_layout);

        if (!impl.pipeline) {
            flecsRenderEffectImplRelease(&impl);
            continue;
        }

        ecs_set_ptr(world, e, FlecsRenderEffectImpl, &impl);
    }
}

void flecsEngineRenderEffect(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUTextureView input_view)
{
    ecs_assert(effect != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(impl != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(input_view != NULL, ECS_INVALID_PARAMETER, NULL);

    WGPUBindGroupEntry entries[8] = {
        { .binding = 0, .textureView = input_view },
        { .binding = 1, .sampler = impl->input_sampler }
    };

    uint32_t entry_count = 2;
    bool bind_ok = effect->bind_callback(
        world,
        engine,
        effect,
        impl,
        entries,
        &entry_count);
    ecs_assert(bind_ok, ECS_INTERNAL_ERROR, NULL);
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

    wgpuRenderPassEncoderSetPipeline(pass, impl->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    wgpuBindGroupRelease(bind_group);
}
