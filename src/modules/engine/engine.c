#define FLECS_ENGINE_ENGINE_IMPL
#include "engine.h"

#include "surfaces/frame_capture/frame_capture.h"
#include "surfaces/window/window.h"

#include "../renderer/renderer.h"
#include "../geometry3/geometry3.h"
#include "../transform3/transform3.h"
#include "../camera/camera.h"
#include "../material/material.h"

ECS_COMPONENT_DECLARE(flecs_vec3_t);
ECS_COMPONENT_DECLARE(flecs_mat4_t);
ECS_COMPONENT_DECLARE(flecs_rgba_t);

ECS_COMPONENT_DECLARE(FlecsEngineImpl);

static void flecsEngineWaitForFuture(
    WGPUInstance instance,
    WGPUFuture future,
    bool *done)
{
    WGPUFutureWaitInfo wait_info = { .future = future };
    while (!*done) {
        wait_info.completed = false;
        wgpuInstanceWaitAny(instance, 1, &wait_info, 0);
    }
}

static void flecsEngineOnRequestAdapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    WGPUAdapter *adapter_out = userdata1;
    bool *future_cond = userdata2;

    if (status == WGPURequestAdapterStatus_Success) {
        *adapter_out = adapter;
    } else {
        if (message.data) {
            ecs_err("Adapter request failed: %.*s\n",
                (int)message.length, message.data);
        } else {
            ecs_err("Adapter request failed: unknown\n");
        }
    }

    *future_cond = true;
}

static void flecsEngineOnRequestDevice(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    WGPUDevice *device_out = userdata1;
    bool *future_cond = userdata2;

    if (status == WGPURequestDeviceStatus_Success) {
        *device_out = device;
    } else {
        if (message.data) {
            ecs_err("Device request failed: %.*s\n",
                (int)message.length, message.data);
        } else {
            ecs_err("Device request failed: unknown\n");
        }
    }

    *future_cond = true;
}

static void flecsEngineCreateDepthResources(
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
        .usage = WGPUTextureUsage_RenderAttachment,
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

static int flecsEngineEnsureDepthResources(
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

    flecsEngineCreateDepthResources(
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

static bool FlecsEngineSurfaceInterfaceValid(
    const FlecsEngineSurfaceInterface *ops)
{
    return ops != NULL &&
        ops->prepare_frame != NULL &&
        ops->acquire_frame != NULL &&
        ops->encode_frame != NULL &&
        ops->submit_frame != NULL &&
        ops->on_frame_failed != NULL &&
        ops->cleanup != NULL;
}

static void flecsEngineReleaseFrameTarget(
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

static void flecsEngineCleanup(
    FlecsEngineImpl *impl,
    bool terminate_runtime)
{
    if (impl->view_query) {
        ecs_query_fini(impl->view_query);
        impl->view_query = NULL;
    }

    if (impl->surface_impl) {
        impl->surface_impl->cleanup(impl, terminate_runtime);
    }

    if (impl->depth_texture_view) {
        wgpuTextureViewRelease(impl->depth_texture_view);
        impl->depth_texture_view = NULL;
    }

    if (impl->depth_texture) {
        wgpuTextureRelease(impl->depth_texture);
        impl->depth_texture = NULL;
    }

    impl->depth_texture_width = 0;
    impl->depth_texture_height = 0;

    if (impl->queue) {
        wgpuQueueRelease(impl->queue);
        impl->queue = NULL;
    }

    if (impl->device) {
        wgpuDeviceRelease(impl->device);
        impl->device = NULL;
    }

    if (impl->adapter) {
        wgpuAdapterRelease(impl->adapter);
        impl->adapter = NULL;
    }

    if (impl->instance) {
        wgpuInstanceRelease(impl->instance);
        impl->instance = NULL;
    }

    if (impl->frame_output_path) {
        ecs_os_free((char*)impl->frame_output_path);
        impl->frame_output_path = NULL;
    }
}

int flecsEngineInit(
    ecs_world_t *world,
    const FlecsEngineOutputDesc *output)
{
    if (!output || !FlecsEngineSurfaceInterfaceValid(output->ops)) {
        ecs_err("Invalid engine output backend\n");
        return -1;
    }

    int32_t width = output->width;
    int32_t height = output->height;
    if (width <= 0) {
        width = 1280;
    }
    if (height <= 0) {
        height = 800;
    }

    FlecsEngineImpl impl = {
        .width = width,
        .height = height,
        .surface_impl = output->ops,
        .output_done = false,
        .depth_texture_width = 0,
        .depth_texture_height = 0
    };

    WGPUInstanceDescriptor instance_desc = {0};
    impl.instance = wgpuCreateInstance(&instance_desc);
    if (!impl.instance) {
        ecs_err("Failed to create wgpu instance\n");
        goto error;
    }

    if (impl.surface_impl->init_instance &&
        impl.surface_impl->init_instance(&impl, output->config))
    {
        goto error;
    }

    bool future_cond = false;

    WGPURequestAdapterOptions adapter_options = {
        .compatibleSurface = impl.surface
    };

    WGPURequestAdapterCallbackInfo adapter_callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = flecsEngineOnRequestAdapter,
        .userdata1 = &impl.adapter,
        .userdata2 = &future_cond
    };

    WGPUFuture adapter_future = wgpuInstanceRequestAdapter(
        impl.instance, &adapter_options, adapter_callback);

    flecsEngineWaitForFuture(impl.instance, adapter_future, &future_cond);
    if (!impl.adapter) {
        goto error;
    }
    ecs_dbg("[engine] adapter acquired=%p", (void*)impl.adapter);

    future_cond = false;

    WGPUDeviceDescriptor device_desc = {0};

    WGPURequestDeviceCallbackInfo device_callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = flecsEngineOnRequestDevice,
        .userdata1 = &impl.device,
        .userdata2 = &future_cond
    };

    WGPUFuture device_future = wgpuAdapterRequestDevice(
        impl.adapter, &device_desc, device_callback);

    flecsEngineWaitForFuture(impl.instance, device_future, &future_cond);
    if (!impl.device) {
        goto error;
    }
    ecs_dbg("[engine] device acquired=%p", (void*)impl.device);

    impl.queue = wgpuDeviceGetQueue(impl.device);

    if (impl.surface_impl->configure_target &&
        impl.surface_impl->configure_target(&impl))
    {
        goto error;
    }

    if (flecsEngineEnsureDepthResources(&impl)) {
        goto error;
    }

    impl.view_query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = ecs_lookup(world, "flecs.engine")
        }),
        .terms = {{ ecs_id(FlecsRenderView) }},
        .cache_kind = EcsQueryCacheAuto
    });
    if (!impl.view_query) {
        ecs_err("Failed to create render view query\n");
        goto error;
    }

    ecs_singleton_set_ptr(world, FlecsEngineImpl, &impl);
    return 0;

error:
    flecsEngineCleanup(&impl, false);
    return -1;
}

static void FlecsEngineDestroy(
    ecs_iter_t *it)
{
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);
    flecsEngineCleanup(impl, true);
}

static void FlecsEngineRender(
    ecs_iter_t *it)
{
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    int prep_result = impl->surface_impl->prepare_frame(it->world, impl);
    if (prep_result > 0) {
        return;
    }
    if (prep_result < 0) {
        impl->surface_impl->on_frame_failed(it->world, impl);
        return;
    }

    if (!impl->width || !impl->height) {
        return;
    }

    if (flecsEngineEnsureDepthResources(impl)) {
        impl->surface_impl->on_frame_failed(it->world, impl);
        return;
    }

    bool failed = false;
    FlecsEngineSurface frame_target = {0};
    WGPUCommandEncoder encoder = NULL;
    WGPUCommandBuffer cmd = NULL;

    int target_result = impl->surface_impl->acquire_frame(impl, &frame_target);
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

    flecsEngineRenderViews(it->world, impl, frame_target.view_texture, encoder);

    if (impl->surface_impl->encode_frame(impl, encoder, &frame_target)) {
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

    if (impl->surface_impl->submit_frame(it->world, impl, &frame_target)) {
        failed = true;
    }

cleanup:
    if (cmd) {
        wgpuCommandBufferRelease(cmd);
    }
    if (encoder) {
        wgpuCommandEncoderRelease(encoder);
    }

    flecsEngineReleaseFrameTarget(&frame_target);

    if (failed) {
        impl->surface_impl->on_frame_failed(it->world, impl);
    }
}

void FlecsEngineImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngine);

    ecs_id(flecs_vec3_t) = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "vec3" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
        }
    });

    ecs_id(flecs_rgba_t) = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "rgba" }),
        .members = {
            { .name = "r", .type = ecs_id(ecs_u8_t) },
            { .name = "g", .type = ecs_id(ecs_u8_t) },
            { .name = "b", .type = ecs_id(ecs_u8_t) },
            { .name = "a", .type = ecs_id(ecs_u8_t) },
        }
    });

    ecs_id(flecs_mat4_t) = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "mat4" }),
        .type.size = ECS_SIZEOF(flecs_mat4_t),
        .type.alignment = ECS_ALIGNOF(flecs_mat4_t)
    });

    ecs_struct(world, {
        .entity = ecs_id(flecs_mat4_t),
        .members = {
            { .name = "v", .type = ecs_id(ecs_f32_t), .count = 16}
        }
    });

    ECS_IMPORT(world, FlecsEngineWindow);
    ECS_IMPORT(world, FlecsEngineFrameCapture);
    ECS_IMPORT(world, FlecsEngineRenderer);
    ECS_IMPORT(world, FlecsEngineGeometry3);
    ECS_IMPORT(world, FlecsEngineTransform3);
    ECS_IMPORT(world, FlecsEngineCamera);
    ECS_IMPORT(world, FlecsEngineMaterial);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsEngineImpl);

    ecs_set_hooks(world, FlecsEngineImpl, {
        .on_remove = FlecsEngineDestroy
    });

    ECS_SYSTEM(world, FlecsEngineRender, EcsOnStore, EngineImpl);
}
