#include "../renderer.h"
#include "ibl_internal.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsHdri);
ECS_COMPONENT_DECLARE(FlecsHdriImpl);

ECS_CTOR(FlecsHdri, ptr, {
    ptr->file = NULL;
    ptr->filter_sample_count = 0;
    ptr->lut_sample_count = 0;
})

ECS_MOVE(FlecsHdri, dst, src, {
    if (dst->file) {
        ecs_os_free((char*)dst->file);
    }

    *dst = *src;
    src->file = NULL;
})

ECS_COPY(FlecsHdri, dst, src, {
    if (dst->file) {
        ecs_os_free((char*)dst->file);
    }

    dst->file = src->file ? ecs_os_strdup(src->file) : NULL;
    dst->filter_sample_count = src->filter_sample_count;
    dst->lut_sample_count = src->lut_sample_count;
})

ECS_DTOR(FlecsHdri, ptr, {
    if (ptr->file) {
        ecs_os_free((char*)ptr->file);
        ptr->file = NULL;
    }
})

FLECS_ENGINE_IMPL_HOOKS(FlecsHdriImpl, flecsEngine_ibl_releaseRuntimeResources)

WGPUBindGroupLayout flecsEngine_ibl_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->ibl_shadow_bind_layout) {
        return impl->ibl_shadow_bind_layout;
    }

    /* Combined IBL + Shadow + Cluster bind group layout (group 1):
     *   binding 0: IBL prefiltered env cubemap
     *   binding 1: IBL sampler
     *   binding 2: IBL BRDF LUT
     *   binding 3: Shadow depth texture array
     *   binding 4: Shadow comparison sampler
     *   binding 5: Cluster info uniform
     *   binding 6: Cluster grid storage
     *   binding 7: Light indices storage
     *   binding 8: Lights storage (unified point + spot) */
    WGPUBindGroupLayoutEntry layout_entries[9] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube,
                .multisampled = false
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Comparison
            }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsClusterInfo)
            }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsClusterEntry)
            }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(uint32_t)
            }
        },
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsGpuLight)
            }
        }
    };

    impl->ibl_shadow_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 9,
            .entries = layout_entries
        });

    return impl->ibl_shadow_bind_layout;
}

bool flecsEngine_ibl_createRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl)
{
    if (ibl->ibl_shadow_bind_group) {
        wgpuBindGroupRelease(ibl->ibl_shadow_bind_group);
        ibl->ibl_shadow_bind_group = NULL;
    }

    WGPUBindGroupLayout bind_layout = engine->ibl_shadow_bind_layout;
    if (!bind_layout) {
        return false;
    }

    if (!engine->shadow.texture_view || !engine->shadow.sampler) {
        return false;
    }

    if (!engine->lighting.cluster_info_buffer || !engine->lighting.cluster_grid_buffer ||
        !engine->lighting.cluster_index_buffer || !engine->lighting.light_buffer)
    {
        return false;
    }

    ibl->ibl_shadow_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = bind_layout,
            .entryCount = 9,
            .entries = (WGPUBindGroupEntry[9]){
                {
                    .binding = 0,
                    .textureView = ibl->ibl_prefiltered_cubemap_view
                },
                {
                    .binding = 1,
                    .sampler = ibl->ibl_sampler
                },
                {
                    .binding = 2,
                    .textureView = ibl->ibl_brdf_lut_texture_view
                },
                {
                    .binding = 3,
                    .textureView = engine->shadow.texture_view
                },
                {
                    .binding = 4,
                    .sampler = engine->shadow.sampler
                },
                {
                    .binding = 5,
                    .buffer = engine->lighting.cluster_info_buffer,
                    .size = sizeof(FlecsClusterInfo)
                },
                {
                    .binding = 6,
                    .buffer = engine->lighting.cluster_grid_buffer,
                    .size = (uint64_t)FLECS_ENGINE_CLUSTER_TOTAL *
                        sizeof(FlecsClusterEntry)
                },
                {
                    .binding = 7,
                    .buffer = engine->lighting.cluster_index_buffer,
                    .size = (uint64_t)engine->lighting.cluster_index_capacity *
                        sizeof(uint32_t)
                },
                {
                    .binding = 8,
                    .buffer = engine->lighting.light_buffer,
                    .size = (uint64_t)engine->lighting.light_capacity *
                        sizeof(FlecsGpuLight)
                }
            }
        });

    ibl->scene_bind_version = engine->scene_bind_version;

    return ibl->ibl_shadow_bind_group != NULL;
}

void flecsEngine_ibl_releaseRuntimeResources(
    FlecsHdriImpl *ibl)
{
    if (!ibl) {
        return;
    }

    if (ibl->ibl_shadow_bind_group) {
        wgpuBindGroupRelease(ibl->ibl_shadow_bind_group);
        ibl->ibl_shadow_bind_group = NULL;
    }

    if (ibl->ibl_sampler) {
        wgpuSamplerRelease(ibl->ibl_sampler);
        ibl->ibl_sampler = NULL;
    }

    if (ibl->ibl_brdf_lut_texture_view) {
        wgpuTextureViewRelease(ibl->ibl_brdf_lut_texture_view);
        ibl->ibl_brdf_lut_texture_view = NULL;
    }

    if (ibl->ibl_brdf_lut_texture) {
        wgpuTextureRelease(ibl->ibl_brdf_lut_texture);
        ibl->ibl_brdf_lut_texture = NULL;
    }

    if (ibl->ibl_prefiltered_cubemap_view) {
        wgpuTextureViewRelease(ibl->ibl_prefiltered_cubemap_view);
        ibl->ibl_prefiltered_cubemap_view = NULL;
    }

    if (ibl->ibl_prefiltered_cubemap) {
        wgpuTextureRelease(ibl->ibl_prefiltered_cubemap);
        ibl->ibl_prefiltered_cubemap = NULL;
    }

    if (ibl->ibl_equirect_texture_view) {
        wgpuTextureViewRelease(ibl->ibl_equirect_texture_view);
        ibl->ibl_equirect_texture_view = NULL;
    }

    if (ibl->ibl_equirect_texture) {
        wgpuTextureRelease(ibl->ibl_equirect_texture);
        ibl->ibl_equirect_texture = NULL;
    }

    ibl->ibl_prefilter_mip_count = 0;
}

void flecsEngine_ibl_releaseResources(
    FlecsEngineImpl *impl)
{
    if (impl && impl->ibl_shadow_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->ibl_shadow_bind_layout);
        impl->ibl_shadow_bind_layout = NULL;
    }
}

static void FlecsIbl_on_set(
    ecs_iter_t *it)
{
    FlecsEngineImpl *engine = ecs_singleton_get_mut(
        it->world, FlecsEngineImpl);
    if (!engine) {
        ecs_err("failed to create IBL: engine not initialized");
        return;
    }

    if (!flecsEngine_ibl_ensureBindLayout(engine)) {
        ecs_err("failed to create IBL bind layout");
        return;
    }

    FlecsHdri *hdri = ecs_field(it, FlecsHdri, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        FlecsHdriImpl *ibl_impl = ecs_ensure(
            it->world, it->entities[i], FlecsHdriImpl);
        flecsEngine_ibl_releaseRuntimeResources(ibl_impl);

        if (!flecsEngine_ibl_initResources(
            engine,
            ibl_impl,
            hdri[i].file,
            NULL, NULL, NULL, NULL,
            hdri[i].filter_sample_count,
            hdri[i].lut_sample_count))
        {
            ecs_err("failed to initialize IBL resources");
            continue;
        }

        ecs_modified(it->world, it->entities[i], FlecsHdriImpl);
    }
}

void flecsEngine_ibl_ensureSkyBackground(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const flecs_engine_background_t *background)
{
    if (!engine->sky_background_hdri) {
        return;
    }

    if (!memcmp(&engine->sky_bg_colors, background,
        sizeof(flecs_engine_background_t)))
    {
        return;
    }

    engine->sky_bg_colors = *background;

    FlecsHdriImpl *ibl = ecs_get_mut(
        world, engine->sky_background_hdri, FlecsHdriImpl);
    if (!ibl) {
        return;
    }

    const FlecsHdri *hdri = ecs_get(
        world, engine->sky_background_hdri, FlecsHdri);
    if (!hdri) {
        return;
    }

    flecsEngine_ibl_releaseRuntimeResources(ibl);
    flecsEngine_ibl_initResources(
        engine, ibl, NULL,
        &background->sky_color,
        &background->ground_color,
        &background->haze_color,
        &background->horizon_color,
        hdri->filter_sample_count,
        hdri->lut_sample_count);
}

ecs_entity_t flecsEngine_createHdri(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *file,
    uint32_t filterSampleCount,
    uint32_t lutSampleCount)
{
    ecs_entity_t hdri = ecs_entity(world, {
        .parent = parent,
        .name = name
    });

    ecs_set(world, hdri, FlecsHdri, {
        .file = file,
        .filter_sample_count = filterSampleCount,
        .lut_sample_count = lutSampleCount
    });

    return hdri;
}

void flecsEngine_ibl_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsHdri);
    ECS_COMPONENT_DEFINE(world, FlecsHdriImpl);

    ecs_set_hooks(world, FlecsHdri, {
        .ctor = ecs_ctor(FlecsHdri),
        .move = ecs_move(FlecsHdri),
        .copy = ecs_copy(FlecsHdri),
        .dtor = ecs_dtor(FlecsHdri),
        .on_set = FlecsIbl_on_set
    });

    ecs_set_hooks(world, FlecsHdriImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsHdriImpl),
        .dtor = ecs_dtor(FlecsHdriImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsHdri),
        .members = {
            { .name = "file", .type = ecs_id(ecs_string_t) },
            { .name = "filter_sample_count", .type = ecs_id(ecs_u32_t) },
            { .name = "lut_sample_count", .type = ecs_id(ecs_u32_t) }
        }
    });
}
