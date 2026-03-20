#define FLECS_ENGINE_ENGINE_IMPL
#include "engine.h"

#include "surfaces/frame_capture/frame_capture.h"
#include "surfaces/window/window.h"

#include "../renderer/renderer.h"
#include "../geometry3/geometry3.h"
#include "../transform3/transform3.h"
#include "../movement/movement.h"
#include "../input/input.h"
#include "../camera/camera.h"
#include "../light/light.h"
#include "../material/material.h"

ECS_COMPONENT_DECLARE(flecs_vec3_t);
ECS_COMPONENT_DECLARE(flecs_mat4_t);
ECS_COMPONENT_DECLARE(flecs_rgba_t);

ECS_COMPONENT_DECLARE(FlecsEngineImpl);

static void flecsEngine_waitForFuture(
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

static void flecsEngine_onRequestAdapter(
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

static void flecsEngine_onRequestDevice(
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

static void flecsEngine_cleanup(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    bool terminate_runtime)
{
    if (impl->view_query) {
        ecs_query_fini(impl->view_query);
        impl->view_query = NULL;
    }

    if (impl->point_light_query) {
        ecs_query_fini(impl->point_light_query);
        impl->point_light_query = NULL;
    }

    if (impl->spot_light_query) {
        ecs_query_fini(impl->spot_light_query);
        impl->spot_light_query = NULL;
    }

    impl->fallback_hdri = 0;

    flecsEngine_shadow_cleanup(impl);
    flecsEngine_material_releaseBuffer(impl);

    flecsEngine_surfaceInterface_cleanup(
        impl->surface_impl, impl, terminate_runtime);

    ecs_delete_with(world, ecs_id(FlecsRenderView));
    ecs_delete_with(world, ecs_id(FlecsRenderBatch));

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

    flecsEngine_defaultAttrCache_free(impl->default_attr_cache);
}

int flecsEngine_init(
    ecs_world_t *world,
    const FlecsEngineOutputDesc *output)
{
    if (!output || !flecsEngine_surfaceInterface_isValid(output->ops)) {
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

    FlecsEngineImpl *ptr = ecs_singleton_ensure(world, FlecsEngineImpl);
    FlecsEngineImpl impl = {
        .width = width,
        .height = height,
        .surface_impl = output->ops,
        .clear_color = output->clear_color,
        .output_done = false,
        .depth_texture_width = 0,
        .depth_texture_height = 0,
        .last_material_id = 0,
        .default_attr_cache = flecsEngine_defaultAttrCache_create()
    };

    WGPUInstanceDescriptor instance_desc = {0};
    impl.instance = wgpuCreateInstance(&instance_desc);
    if (!impl.instance) {
        ecs_err("Failed to create wgpu instance\n");
        goto error;
    }

    if (flecsEngine_surfaceInterface_initInstance(
        impl.surface_impl, &impl, output->config))
    {
        goto error;
    }

    bool future_cond = false;

    WGPURequestAdapterOptions adapter_options = {
        .compatibleSurface = impl.surface
    };

    WGPURequestAdapterCallbackInfo adapter_callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = flecsEngine_onRequestAdapter,
        .userdata1 = &impl.adapter,
        .userdata2 = &future_cond
    };

    WGPUFuture adapter_future = wgpuInstanceRequestAdapter(
        impl.instance, &adapter_options, adapter_callback);

    flecsEngine_waitForFuture(impl.instance, adapter_future, &future_cond);
    if (!impl.adapter) {
        goto error;
    }

    future_cond = false;

    WGPUDeviceDescriptor device_desc = {0};

    WGPURequestDeviceCallbackInfo device_callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = flecsEngine_onRequestDevice,
        .userdata1 = &impl.device,
        .userdata2 = &future_cond
    };

    WGPUFuture device_future = wgpuAdapterRequestDevice(
        impl.adapter, &device_desc, device_callback);

    flecsEngine_waitForFuture(impl.instance, device_future, &future_cond);
    if (!impl.device) {
        goto error;
    }

    impl.queue = wgpuDeviceGetQueue(impl.device);

    if (flecsEngine_surfaceInterface_configureTarget(
        impl.surface_impl, &impl))
    {
        goto error;
    }

    if (flecsEngine_initRenderer(world, &impl)) {
        goto error;
    }

    *ptr = impl;

    return 0;

error:
    flecsEngine_cleanup(world, &impl, false);
    return -1;
}

static void flecsEngine_destroy(
    ecs_iter_t *it)
{
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);
    flecsEngine_cleanup(it->world, impl, true);
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

    ecs_set_alias(world, ecs_id(flecs_vec3_t), "flecs_vec3_t");
    ecs_set_alias(world, ecs_id(flecs_rgba_t), "flecs_rgba_t");

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

    ecs_set_alias(world, ecs_id(flecs_mat4_t), "flecs_mat4_t");

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsEngineImpl);

    ecs_set_hooks(world, FlecsEngineImpl, {
        .on_remove = flecsEngine_destroy
    });

    ECS_IMPORT(world, FlecsEngineWindow);
    ECS_IMPORT(world, FlecsEngineFrameCapture);
    ECS_IMPORT(world, FlecsEngineRenderer);
    ECS_IMPORT(world, FlecsEngineGeometry3);
    ECS_IMPORT(world, FlecsEngineTransform3);
    ECS_IMPORT(world, FlecsEngineMovement);
    ECS_IMPORT(world, FlecsEngineInput);
    ECS_IMPORT(world, FlecsEngineCamera);
    ECS_IMPORT(world, FlecsEngineLight);
    ECS_IMPORT(world, FlecsEngineMaterial);
}
