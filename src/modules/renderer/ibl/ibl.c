#include "../renderer.h"
#include "ibl_internal.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsIbl);
ECS_COMPONENT_DECLARE(FlecsIblImpl);

ECS_CTOR(FlecsIbl, ptr, {
    ptr->file = NULL;
})

ECS_MOVE(FlecsIbl, dst, src, {
    if (dst->file) {
        ecs_os_free((char*)dst->file);
    }

    *dst = *src;
    src->file = NULL;
})

ECS_COPY(FlecsIbl, dst, src, {
    if (dst->file) {
        ecs_os_free((char*)dst->file);
    }

    dst->file = src->file ? ecs_os_strdup(src->file) : NULL;
})

ECS_DTOR(FlecsIbl, ptr, {
    if (ptr->file) {
        ecs_os_free((char*)ptr->file);
        ptr->file = NULL;
    }
})

ECS_DTOR(FlecsIblImpl, ptr, {
    flecsIblReleaseRuntimeResources(ptr);
})

ECS_MOVE(FlecsIblImpl, dst, src, {
    flecsIblReleaseRuntimeResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

WGPUBindGroupLayout flecsEngineEnsureIblBindLayout(
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

bool flecsIblCreateRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecsIblImpl *ibl)
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

void flecsIblReleaseRuntimeResources(
    FlecsIblImpl *ibl)
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

void flecsEngineReleaseIblResources(
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
        return;
    }

    if (!flecsEngineEnsureIblBindLayout(engine)) {
        ecs_err("failed to create IBL bind layout");
        return;
    }

    FlecsIbl *hdri = ecs_field(it, FlecsIbl, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        FlecsIblImpl *ibl_impl = ecs_ensure(
            it->world, it->entities[i], FlecsIblImpl);
        flecsIblReleaseRuntimeResources(ibl_impl);

        if (!flecsEngineInitIblResources(engine, ibl_impl, hdri[i].file)) {
            ecs_err("failed to initialize IBL resources");
            continue;
        }

        ecs_modified(it->world, it->entities[i], FlecsIblImpl);
    }
}

ecs_entity_t flecsEngine_createHdri(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *file)
{
    if (!file || !file[0]) {
        ecs_err("cannot create hdri entity: missing file path");
        return 0;
    }

    ecs_entity_t hdri = ecs_entity(world, {
        .parent = parent,
        .name = name
    });

    ecs_set(world, hdri, FlecsIbl, {
        .file = file
    });

    return hdri;
}

void flecsEngine_ibl_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsIbl);
    ECS_COMPONENT_DEFINE(world, FlecsIblImpl);

    ecs_set_hooks(world, FlecsIbl, {
        .ctor = ecs_ctor(FlecsIbl),
        .move = ecs_move(FlecsIbl),
        .copy = ecs_copy(FlecsIbl),
        .dtor = ecs_dtor(FlecsIbl),
        .on_set = FlecsIbl_on_set
    });

    ecs_set_hooks(world, FlecsIblImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsIblImpl),
        .dtor = ecs_dtor(FlecsIblImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsIbl),
        .members = {
            { .name = "file", .type = ecs_id(ecs_string_t) }
        }
    });
}
