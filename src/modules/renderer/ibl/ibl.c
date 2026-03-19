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

ECS_DTOR(FlecsHdriImpl, ptr, {
    flecsEngine_ibl_releaseRuntimeResources(ptr);
})

ECS_MOVE(FlecsHdriImpl, dst, src, {
    flecsEngine_ibl_releaseRuntimeResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

WGPUBindGroupLayout flecsEngine_ibl_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->ibl_bind_layout) {
        return impl->ibl_bind_layout;
    }

    WGPUBindGroupLayoutEntry layout_entries[3] = {
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
        }
    };

    impl->ibl_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 3,
            .entries = layout_entries
        });

    return impl->ibl_bind_layout;
}

bool flecsEngine_ibl_createRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl)
{
    if (ibl->ibl_bind_group) {
        wgpuBindGroupRelease(ibl->ibl_bind_group);
        ibl->ibl_bind_group = NULL;
    }

    WGPUBindGroupLayout bind_layout = engine->ibl_bind_layout;
    if (!bind_layout) {
        return false;
    }

    ibl->ibl_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = bind_layout,
            .entryCount = 3,
            .entries = (WGPUBindGroupEntry[3]){
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
                }
            }
        });

    return ibl->ibl_bind_group != NULL;
}

void flecsEngine_ibl_releaseRuntimeResources(
    FlecsHdriImpl *ibl)
{
    if (!ibl) {
        return;
    }

    if (ibl->ibl_bind_group) {
        wgpuBindGroupRelease(ibl->ibl_bind_group);
        ibl->ibl_bind_group = NULL;
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
    if (impl && impl->ibl_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->ibl_bind_layout);
        impl->ibl_bind_layout = NULL;
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
            hdri[i].filter_sample_count,
            hdri[i].lut_sample_count))
        {
            ecs_err("failed to initialize IBL resources");
            continue;
        }

        ecs_modified(it->world, it->entities[i], FlecsHdriImpl);
    }
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
