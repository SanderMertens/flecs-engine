#include "renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(flecs_engine_background_t);
ECS_COMPONENT_DECLARE(flecs_engine_shadow_params_t);
ECS_COMPONENT_DECLARE(flecs_render_view_effect_t);
ECS_COMPONENT_DECLARE(FlecsRenderView);
ECS_COMPONENT_DECLARE(FlecsRenderViewImpl);

ECS_CTOR(FlecsRenderView, ptr, {
    ecs_vec_init_t(NULL, &ptr->effects, flecs_render_view_effect_t, 0);
    ptr->camera = 0;
    ptr->light = 0;
    ptr->hdri = 0;
    ptr->ambient_light = (flecs_rgba_t){20, 20, 20, 255};
    ptr->background = (flecs_engine_background_t){
        .sky_color = {0},
        .ground_color = {0},
        .haze_color = {255, 255, 255, 255},
        .horizon_color = {255, 255, 255, 255},
        .ibl = true
    };
    ptr->shadow.enabled = true;
    ptr->shadow.map_size = FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT;
    ptr->shadow.bias = 0.0005f;
    ptr->shadow.max_range = 100.0f;
})

ECS_MOVE(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, flecs_render_view_effect_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, flecs_render_view_effect_t);
    dst->camera = src->camera;
    dst->light = src->light;
    dst->hdri = src->hdri;
    dst->ambient_light = src->ambient_light;
    dst->background = src->background;
    dst->shadow = src->shadow;
    dst->effects = ecs_vec_copy_t(NULL, &src->effects, flecs_render_view_effect_t);
})

ECS_DTOR(FlecsRenderView, ptr, {
    ecs_vec_fini_t(NULL, &ptr->effects, flecs_render_view_effect_t);
})

static void flecsEngine_renderView_releaseTargets(
    FlecsRenderViewImpl *impl)
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

    if (impl->passthrough_bind_group) {
        wgpuBindGroupRelease(impl->passthrough_bind_group);
        impl->passthrough_bind_group = NULL;
    }

    impl->effect_target_count = 0;
    impl->effect_target_width = 0;
    impl->effect_target_height = 0;
    impl->effect_target_format = WGPUTextureFormat_Undefined;
}

ECS_MOVE(FlecsRenderViewImpl, dst, src, {
    flecsEngine_renderView_releaseTargets(dst);
    *dst = *src;
    ecs_os_memset_t(src, 0, FlecsRenderViewImpl);
})

ECS_DTOR(FlecsRenderViewImpl, ptr, {
    flecsEngine_renderView_releaseTargets(ptr);
})

static bool flecsEngine_renderView_createTargets(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl,
    int32_t effect_count,
    WGPUTextureFormat format)
{
    uint32_t width = (uint32_t)engine->actual_width;
    uint32_t height = (uint32_t)engine->actual_height;

    impl->effect_target_textures = ecs_os_calloc_n(WGPUTexture, effect_count);
    impl->effect_target_views = ecs_os_calloc_n(WGPUTextureView, effect_count);
    if (!impl->effect_target_textures || !impl->effect_target_views) {
        goto error;
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
            engine->device, &color_desc);
        if (!impl->effect_target_textures[i]) {
            goto error;
        }

        impl->effect_target_views[i] = wgpuTextureCreateView(
            impl->effect_target_textures[i], NULL);
        if (!impl->effect_target_views[i]) {
            goto error;
        }
    }

    impl->effect_target_count = effect_count;
    impl->effect_target_width = width;
    impl->effect_target_height = height;
    impl->effect_target_format = format;

    return true;
error:
    flecsEngine_renderView_releaseTargets(impl);
    return false;
}

static int flecsEngine_renderView_ensureTargets(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl,
    int32_t effect_count)
{
    if (effect_count <= 0) {
        return 0;
    }

    uint32_t width = (uint32_t)engine->actual_width;
    uint32_t height = (uint32_t)engine->actual_height;
    WGPUTextureFormat surface_format = engine->surface_config.format;
    WGPUTextureFormat desired_format = flecsEngine_getHdrFormat(engine);

    if (impl->effect_target_count >= effect_count) {
        if (impl->effect_target_textures && impl->effect_target_views) {
            if (impl->effect_target_width == width &&
                impl->effect_target_height == height &&
                impl->effect_target_format == desired_format)
            {
                return 0;
            }
        }
    }

    flecsEngine_renderView_releaseTargets(impl);

    if (flecsEngine_renderView_createTargets(
        engine, impl, effect_count, desired_format))
    {
        return 0;
    }

    if (desired_format != surface_format &&
        flecsEngine_renderView_createTargets(
            engine, impl, effect_count, surface_format))
    {
        engine->hdr_color_format = surface_format;
        ecs_warn("falling back to LDR targets: HDR format unavailable");
        return 0;
    }

    return -1;
}

static void flecsEngine_renderView_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t view_entity,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView view_texture)
{
    engine->last_pipeline = NULL;

    int32_t effect_count = ecs_vec_count(&view->effects);
    int32_t target_count = effect_count > 0 ? effect_count : 1;
    if (effect_count > 0 && engine->resolution_scale > 1) {
        target_count = effect_count + 1;
    }
    if (flecsEngine_renderView_ensureTargets(engine, impl, target_count))
    {
        ecs_err("failed to allocate effect render targets");
        return;
    }

    if (view->shadow.enabled) {
        if (flecsEngine_shadow_ensureSize(
            world, engine, (uint32_t)view->shadow.map_size))
        {
            ecs_err("failed to resize shadow maps");
        }

        flecsEngine_renderView_renderShadow(
            world, view_entity, engine, view, encoder);
    } else {
        for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
            memset(engine->shadow.current_light_vp[i], 0, sizeof(mat4));
            engine->shadow.cascade_splits[i] = 0.0f;
        }
    }

    flecsEngine_setupPointLights(world, engine);
    flecsEngine_setupSpotLights(world, engine);
    flecsEngine_cluster_build(world, engine, view);

    flecsEngine_renderView_renderBatches(
        world, view_entity, engine, view, impl, encoder);

    /* When MSAA is active, the batch pass writes to the MSAA depth texture
     * rather than the 1-sample depth texture.  Resolve the multisampled depth
     * into the 1-sample texture so that post-process effects (SSAO, fog, …)
     * can read it. */
    if (engine->sample_count > 1) {
        flecsEngine_depthResolve(engine, encoder);
    }

    flecsEngine_renderView_renderEffects(
        world, view_entity, engine, view, impl, view_texture, encoder);
}

static void flecsEngine_renderView_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t view_entity,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *impl)
{
    (void)impl;

    /* Rebuild the sky background HDRI if the view's background colors
     * changed since the last frame. */
    if (!view->hdri) {
        flecsEngine_ibl_ensureSkyBackground(
            world, engine, &view->background);
    }

    flecsEngine_renderView_extractBatches(world, view_entity, engine, view);
}

void flecsEngine_renderView_extractAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_extract(
                world,
                engine,
                it.entities[i],
                &views[i],
                &viewImpls[i]);
        }
    }
}

void flecsEngine_renderView_renderAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_render(world, engine, it.entities[i], 
                &views[i], &viewImpls[i],
                encoder, view_texture);
        }
    }
}

void flecsEngine_renderView_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, flecs_engine_background_t);
    ECS_COMPONENT_DEFINE(world, flecs_engine_shadow_params_t);
    ECS_COMPONENT_DEFINE(world, flecs_render_view_effect_t);
    ECS_COMPONENT_DEFINE(world, FlecsRenderView);
    ECS_COMPONENT_DEFINE(world, FlecsRenderViewImpl);

    ecs_set_hooks(world, FlecsRenderView, {
        .ctor = ecs_ctor(FlecsRenderView),
        .move = ecs_move(FlecsRenderView),
        .copy = ecs_copy(FlecsRenderView),
        .dtor = ecs_dtor(FlecsRenderView)
    });

    ecs_set_hooks(world, FlecsRenderViewImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderViewImpl),
        .dtor = ecs_dtor(FlecsRenderViewImpl)
    });

    ecs_add_pair(world, ecs_id(FlecsRenderView), EcsWith, ecs_id(FlecsRenderViewImpl));

    ecs_struct(world, {
        .entity = ecs_id(flecs_engine_background_t),
        .members = {
            { .name = "sky_color", .type = ecs_id(flecs_rgba_t) },
            { .name = "ground_color", .type = ecs_id(flecs_rgba_t) },
            { .name = "haze_color", .type = ecs_id(flecs_rgba_t) },
            { .name = "horizon_color", .type = ecs_id(flecs_rgba_t) },
            { .name = "ibl", .type = ecs_id(ecs_bool_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(flecs_engine_shadow_params_t),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "map_size", .type = ecs_id(ecs_i32_t) },
            { .name = "bias", .type = ecs_id(ecs_f32_t) },
            { .name = "max_range", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(flecs_render_view_effect_t),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "effect", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_entity_t vec_view_effect = ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "::flecs.engine.types.vecViewEffect",
        }),
        .type = ecs_id(flecs_render_view_effect_t)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderView),
        .members = {
            { .name = "camera", .type = ecs_id(ecs_entity_t) },
            { .name = "light", .type = ecs_id(ecs_entity_t) },
            { .name = "hdri", .type = ecs_id(ecs_entity_t) },
            { .name = "ambient_light", .type = ecs_id(flecs_rgba_t) },
            { .name = "background", .type = ecs_id(flecs_engine_background_t) },
            { .name = "shadow", .type = ecs_id(flecs_engine_shadow_params_t) },
            { .name = "effects", .type = vec_view_effect }
        }
    });
}
