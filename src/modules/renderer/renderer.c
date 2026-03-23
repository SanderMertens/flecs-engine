#include "renderer.h"
#include "../engine/engine.h"

#define FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL_IMPL
#include "flecs_engine.h"

extern ECS_COMPONENT_DECLARE(FlecsRenderEffect);
ECS_COMPONENT_DECLARE(FlecsVertex);
ECS_COMPONENT_DECLARE(FlecsLitVertex);
ECS_COMPONENT_DECLARE(FlecsInstanceTransform);
extern ECS_COMPONENT_DECLARE(FlecsRgba);
extern ECS_COMPONENT_DECLARE(FlecsPbrMaterial);
extern ECS_COMPONENT_DECLARE(FlecsEmissive);
extern ECS_COMPONENT_DECLARE(FlecsMaterialId);
ECS_COMPONENT_DECLARE(FlecsUniform);

static void flecsEngine_releaseFrameTarget(
    FlecsEngineSurface *target)
{
    if (target->owns_view_texture && target->view_texture) {
        wgpuTextureViewRelease(target->view_texture);
    }

    if (target->surface_texture) {
        wgpuTextureRelease(target->surface_texture);
    }

    if (target->readback_buffer) {
        wgpuBufferRelease(target->readback_buffer);
    }

    target->view_texture = NULL;
    target->surface_texture = NULL;
    target->owns_view_texture = false;
    target->surface_status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    target->readback_buffer = NULL;
    target->readback_bytes_per_row = 0;
    target->readback_buffer_size = 0;
}

static void flecsEngine_createDepthResources(
    WGPUDevice device,
    uint32_t width,
    uint32_t height,
    WGPUTexture *texture,
    WGPUTextureView *view)
{
    if (*view) {
        wgpuTextureViewRelease(*view);
        *view = NULL;
    }

    if (*texture) {
        wgpuTextureRelease(*texture);
        *texture = NULL;
    }

    WGPUTextureDescriptor depth_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = WGPUTextureFormat_Depth24Plus,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    *texture = wgpuDeviceCreateTexture(device, &depth_desc);
    if (!*texture) {
        ecs_err("Failed to create depth texture\n");
        return;
    }

    *view = wgpuTextureCreateView(*texture, NULL);
    if (!*view) {
        ecs_err("Failed to create depth texture view\n");
        wgpuTextureRelease(*texture);
        *texture = NULL;
    }
}

static int flecsEngine_ensureDepthResources(
    FlecsEngineImpl *impl)
{
    if (impl->width <= 0 || impl->height <= 0) {
        return 0;
    }

    uint32_t width = (uint32_t)impl->width;
    uint32_t height = (uint32_t)impl->height;

    if (impl->depth_texture &&
        impl->depth_texture_view &&
        impl->depth_texture_width == width &&
        impl->depth_texture_height == height)
    {
        return 0;
    }

    flecsEngine_createDepthResources(
        impl->device,
        width,
        height,
        &impl->depth_texture,
        &impl->depth_texture_view);
    if (!impl->depth_texture || !impl->depth_texture_view) {
        impl->depth_texture_width = 0;
        impl->depth_texture_height = 0;
        return -1;
    }

    impl->depth_texture_width = width;
    impl->depth_texture_height = height;
    return 0;
}

int flecsEngine_initRenderer(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    impl->hdr_color_format = WGPUTextureFormat_RGBA16Float;

    if (flecsEngine_ensureDepthResources(impl)) {
        goto error;
    }

    ecs_entity_t engine_parent = ecs_lookup(world, "flecs.engine");

    impl->view_query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {{ ecs_id(FlecsRenderView) }, { ecs_id(FlecsRenderViewImpl) }},
        .cache_kind = EcsQueryCacheAuto
    });

    if (flecsEngine_shadow_init(world, impl, FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT)) {
        goto error;
    }

    if (flecsEngine_cluster_init(impl)) {
        goto error;
    }

    impl->material_query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsSelf },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    impl->point_light_query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {
            { .id = ecs_id(FlecsPointLight), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    impl->spot_light_query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {
            { .id = ecs_id(FlecsSpotLight), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    impl->fallback_hdri = flecsEngine_createHdri(
        world, 0, "FallbackHdri", NULL, 1014, 64);

    if (flecsEngine_initPassthrough(impl)) {
        goto error;
    }

    return 0;
error:
    return -1;
}

static void FlecsEngineExtract(
    ecs_iter_t *it)
{
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    if (!impl->device || !impl->queue) {
        return;
    }

    flecsEngine_renderView_extractAll(it->world, impl);
}

static void FlecsEngineRender(
    ecs_iter_t *it)
{
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    const FlecsEngineSurfaceInterface *surface_impl = impl->surface_impl;

    int prep_result = flecsEngine_surfaceInterface_prepareFrame(
        surface_impl, it->world, impl);
    if (prep_result > 0) {
        return;
    }
    if (prep_result < 0) {
        flecsEngine_surfaceInterface_onFrameFailed(
            surface_impl, it->world, impl);
        return;
    }

    if (!impl->width || !impl->height) {
        return;
    }

    if (flecsEngine_ensureDepthResources(impl)) {
        flecsEngine_surfaceInterface_onFrameFailed(
            surface_impl, it->world, impl);
        return;
    }

    bool failed = false;
    FlecsEngineSurface frame_target = {0};
    WGPUCommandEncoder encoder = NULL;
    WGPUCommandBuffer cmd = NULL;

    int target_result = flecsEngine_surfaceInterface_acquireFrame(
        surface_impl, impl, &frame_target);
    if (target_result > 0) {
        return;
    }

    if (target_result < 0) {
        failed = true;
        goto cleanup;
    }

    WGPUCommandEncoderDescriptor encoder_desc = {0};
    encoder = wgpuDeviceCreateCommandEncoder(impl->device, &encoder_desc);
    if (!encoder) {
        ecs_err("Failed to create command encoder\n");
        failed = true;
        goto cleanup;
    }

    // Sync materials
    flecsEngine_material_uploadBuffer(it->world, impl);

    WGPUTextureView render_target = flecsEngine_getGammaRenderTarget(
        impl, frame_target.view_texture);

    // Render all views
    flecsEngine_renderView_renderAll(it->world, impl, render_target, encoder);

    flecsEngine_gammaBlitIfNeeded(
        impl, encoder, render_target, frame_target.view_texture);

    if (flecsEngine_surfaceInterface_encodeFrame(
        surface_impl, impl, encoder, &frame_target))
    {
        failed = true;
        goto cleanup;
    }

    WGPUCommandBufferDescriptor cmd_desc = {0};
    cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    if (!cmd) {
        ecs_err("Failed to create command buffer\n");
        failed = true;
        goto cleanup;
    }

    wgpuQueueSubmit(impl->queue, 1, &cmd);

    if (flecsEngine_surfaceInterface_submitFrame(
        surface_impl, it->world, impl, &frame_target))
    {
        failed = true;
    }

cleanup:
    if (cmd) {
        wgpuCommandBufferRelease(cmd);
    }
    if (encoder) {
        wgpuCommandEncoderRelease(encoder);
    }

    flecsEngine_releaseFrameTarget(&frame_target);

    if (failed) {
        flecsEngine_surfaceInterface_onFrameFailed(
            surface_impl, it->world, impl);
    }
}

void FlecsEngineRendererImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineRenderer);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsVertex);
    ECS_COMPONENT_DEFINE(world, FlecsLitVertex);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceTransform);
    ECS_COMPONENT_DEFINE(world, FlecsUniform);

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
            { .name = "c0", .type = ecs_id(flecs_vec3_t) },
            { .name = "c1", .type = ecs_id(flecs_vec3_t) },
            { .name = "c2", .type = ecs_id(flecs_vec3_t) },
            { .name = "c3", .type = ecs_id(flecs_vec3_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsUniform),
        .members = {
            { .name = "mvp", .type = ecs_id(flecs_mat4_t) },
            { .name = "inv_vp", .type = ecs_id(flecs_mat4_t) },
            { .name = "light_vp", .type = ecs_id(flecs_mat4_t), .count = FLECS_ENGINE_SHADOW_CASCADE_COUNT },
            { .name = "cascade_splits", .type = ecs_id(ecs_f32_t), .count = FLECS_ENGINE_SHADOW_CASCADE_COUNT },
            { .name = "sky_color", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "light_ray_dir", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "light_color", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "camera_pos", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "shadow_info", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "shadow_cascade_scales", .type = ecs_id(ecs_f32_t), .count = FLECS_ENGINE_SHADOW_CASCADE_COUNT },
            { .name = "ambient_light", .type = ecs_id(ecs_f32_t), .count = 4 },
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

    flecsEngine_shader_register(world);
    flecsEngine_renderBatch_register(world);
    flecsEngine_renderEffect_register(world);
    flecsEngine_renderView_register(world);
    flecsEngine_ibl_register(world);
    flecsEngine_tonyMcMapFace_register(world);
    flecsEngine_bloom_register(world);
    flecsEngine_exponentialHeightFog_register(world);
    flecsEngine_ssao_register(world);

    ecs_set_name_prefix(world, "FlecsEngine");

    ECS_SYSTEM(world, FlecsEngineExtract, EcsOnStore,
        flecs.engine.EngineImpl);

    ECS_SYSTEM(world, FlecsEngineRender, EcsOnStore, 
        flecs.engine.EngineImpl);
}
