#include "engine.h"
#include "../window/window.h"
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
  (void)userdata2;

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
    (void)userdata2;

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
    *view = wgpuTextureCreateView(*texture, NULL);
}

int flecsEngineInit(
    ecs_world_t *world,
    GLFWwindow *window,
    int32_t width,
    int32_t height)
{
    FlecsEngineImpl impl = {
        .window = window,
        .width = width,
        .height = height
    };

    // Create wgpu instance
    WGPUInstanceDescriptor instance_desc = {0};
    impl.instance = wgpuCreateInstance(&instance_desc);
    if (!impl.instance) {
        ecs_err("Failed to create wgpu instance\n");
        goto error;
    }

    // Create wgpu surface
    void *metal_layer = flecs_create_metal_layer(
        glfwGetCocoaWindow(impl.window));

    WGPUSurfaceSourceMetalLayer metal_desc = {
        .chain = { .sType = WGPUSType_SurfaceSourceMetalLayer },
        .layer = metal_layer
    };

    WGPUSurfaceDescriptor surface_desc = {
        .nextInChain = (WGPUChainedStruct *)&metal_desc
    };

    impl.surface = wgpuInstanceCreateSurface(impl.instance, &surface_desc);

    bool future_cond = false;

    // Create wgpu adapter
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

    flecsEngineWaitForFuture(
        impl.instance, adapter_future, &future_cond);
    if (!impl.adapter) {
        goto error;
    }

    future_cond = false;

    // Create wgpu device
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

    // Get wgpu queue
    impl.queue = wgpuDeviceGetQueue(impl.device);

    // Configure surface
    WGPUSurfaceCapabilities surface_caps = {0};
    if (wgpuSurfaceGetCapabilities(impl.surface, impl.adapter,
                                    &surface_caps) != WGPUStatus_Success ||
        surface_caps.formatCount == 0) {
        ecs_err("Failed to get surface capabilities\n");
        goto error;
    }

    // Choose the first supported surface format.
    WGPUTextureFormat surface_format = surface_caps.formats[0];

    // Surface config: color attachment usage, format, size, present mode.
    impl.surface_config = (WGPUSurfaceConfiguration){
        .device = impl.device,
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = surface_format,
        .width = (uint32_t)width,
        .height = (uint32_t)height,
        .presentMode = WGPUPresentMode_Fifo,
        .alphaMode = surface_caps.alphaModes[0]
    };

    wgpuSurfaceConfigure(impl.surface, &impl.surface_config);
    wgpuSurfaceCapabilitiesFreeMembers(surface_caps);

    // Configure depth resources
    flecsEngineCreateDepthResources(
        impl.device, width, height, 
        &impl.depth_texture, &impl.depth_texture_view);


    // Create query to iterate active views
    impl.view_query = ecs_query(world, {
        .entity = ecs_entity(world, { 
            .parent = ecs_lookup(world, "flecs.engine") 
        }),
        .terms = {{ ecs_id(FlecsRenderView) }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_singleton_set_ptr(world, FlecsEngineImpl, &impl);

    return 0;
error:
    return -1;
}

static void FlecsEngineDestroy(
    ecs_iter_t *it)
{
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    wgpuSurfaceRelease(impl->surface);
    wgpuQueueRelease(impl->queue);
    wgpuDeviceRelease(impl->device);
    wgpuAdapterRelease(impl->adapter);
    wgpuInstanceRelease(impl->instance);
    glfwDestroyWindow(impl->window);
    glfwTerminate();
}

static void FlecsEngineHandleResize(
    FlecsEngineImpl *impl)
{
    int w, h;
    glfwGetFramebufferSize(impl->window, &w, &h);

    if (w != impl->width || h != impl->height) {
        impl->width = impl->surface_config.width = (uint32_t)w;
        impl->height = impl->surface_config.height = (uint32_t)h;
        wgpuSurfaceConfigure(impl->surface, &impl->surface_config);

        flecsEngineCreateDepthResources(impl->device, w, h, 
            &impl->depth_texture, &impl->depth_texture_view);
    }
}

static void FlecsEngineRender(ecs_iter_t *it) {
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);
    if (glfwWindowShouldClose(impl->window)) {
        ecs_quit(it->world);
    }

    glfwPollEvents();
    
    FlecsEngineHandleResize(impl);

    if (!impl->width || !impl->height) {
        return;
    }

    // Acquire the next swapchain texture.
    WGPUSurfaceTexture surface_texture = {0};
    wgpuSurfaceGetCurrentTexture(impl->surface, &surface_texture);
    if (surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
      wgpuSurfaceConfigure(impl->surface, &impl->surface_config);
      return;
    }

    // Create a view of the swapchain texture.
    WGPUTextureView view_texture =
        wgpuTextureCreateView(surface_texture.texture, NULL);

    // Record GPU commands for this frame
    WGPUCommandEncoderDescriptor encoder_desc = {0};
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(impl->device, &encoder_desc);

    // Render
    flecsEngineRenderViews(it->world, impl, view_texture, encoder);

    // Finalize the command buffer for submission.
    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(impl->queue, 1, &cmd);

    // Present surface
    wgpuSurfacePresent(impl->surface);

    // Release resources
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view_texture);
    wgpuTextureRelease(surface_texture.texture);
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
