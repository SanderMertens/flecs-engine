/* Platform compatibility layer — implementation.
 *
 * All #ifdef __EMSCRIPTEN__ blocks for WebGPU API differences live here
 * so the rest of the engine code stays platform-agnostic. */

#include "platform.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

/* ---- GPU initialisation ---- */

#ifdef __EMSCRIPTEN__

typedef struct {
    WGPUAdapter *adapter_out;
    bool done;
} FlecsAdapterRequest;

typedef struct {
    WGPUDevice *device_out;
    bool done;
} FlecsDeviceRequest;

static void flecsEngine_onRequestAdapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    char const *message,
    void *userdata)
{
    FlecsAdapterRequest *req = userdata;
    if (status == WGPURequestAdapterStatus_Success) {
        *req->adapter_out = adapter;
    } else {
        ecs_err("Adapter request failed: %s\n",
            message ? message : "unknown");
    }
    req->done = true;
}

static void flecsEngine_onRequestDevice(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    char const *message,
    void *userdata)
{
    FlecsDeviceRequest *req = userdata;
    if (status == WGPURequestDeviceStatus_Success) {
        *req->device_out = device;
    } else {
        ecs_err("Device request failed: %s\n",
            message ? message : "unknown");
    }
    req->done = true;
}

static void flecsEngine_onDeviceError(
    WGPUErrorType type,
    char const *message,
    void *userdata)
{
    (void)userdata;
    const char *type_str = "unknown";
    switch (type) {
    case WGPUErrorType_Validation: type_str = "validation"; break;
    case WGPUErrorType_OutOfMemory: type_str = "out of memory"; break;
    default: break;
    }
    printf("[wasm] WebGPU %s error: %s\n", type_str, message ? message : "");
}

#else /* native / wgpu-native */

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

#endif /* __EMSCRIPTEN__ */

WGPUAdapter flecsEngine_requestAdapter(
    WGPUInstance instance,
    WGPUSurface surface)
{
    WGPUAdapter adapter = NULL;

    WGPURequestAdapterOptions options = {
        .compatibleSurface = surface
    };

#ifdef __EMSCRIPTEN__
    FlecsAdapterRequest req = {
        .adapter_out = &adapter,
        .done = false
    };

    wgpuInstanceRequestAdapter(
        instance, &options,
        flecsEngine_onRequestAdapter, &req);

    while (!req.done) {
        emscripten_sleep(1);
    }
#else
    bool done = false;

    WGPURequestAdapterCallbackInfo callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = flecsEngine_onRequestAdapter,
        .userdata1 = &adapter,
        .userdata2 = &done
    };

    WGPUFuture future = wgpuInstanceRequestAdapter(
        instance, &options, callback);

    flecsEngine_waitForFuture(instance, future, &done);
#endif

    return adapter;
}

WGPUDevice flecsEngine_requestDevice(
    WGPUAdapter adapter,
    WGPUInstance instance)
{
    WGPUDevice device = NULL;

#ifndef __EMSCRIPTEN__
    WGPUFeatureName required_features[] = {
        WGPUFeatureName_TextureCompressionBC
    };
    WGPUDeviceDescriptor desc = {
        .requiredFeatures = required_features,
        .requiredFeatureCount = 1
    };
#else
    WGPUDeviceDescriptor desc = {0};
#endif

#ifdef __EMSCRIPTEN__
    (void)instance;

    FlecsDeviceRequest req = {
        .device_out = &device,
        .done = false
    };

    wgpuAdapterRequestDevice(
        adapter, &desc,
        flecsEngine_onRequestDevice, &req);

    while (!req.done) {
        emscripten_sleep(1);
    }
#else
    bool done = false;

    WGPURequestDeviceCallbackInfo callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = flecsEngine_onRequestDevice,
        .userdata1 = &device,
        .userdata2 = &done
    };

    WGPUFuture future = wgpuAdapterRequestDevice(
        adapter, &desc, callback);

    flecsEngine_waitForFuture(instance, future, &done);
#endif

    return device;
}

void flecsEngine_setDeviceErrorCallback(
    WGPUDevice device)
{
#ifdef __EMSCRIPTEN__
    wgpuDeviceSetUncapturedErrorCallback(device,
        (WGPUErrorCallback)flecsEngine_onDeviceError, NULL);
#else
    (void)device;
#endif
}

/* ---- Async helpers ---- */

void flecsEngine_processEventsUntilDone(
    WGPUInstance instance,
    bool *done)
{
    while (!*done) {
#ifdef __EMSCRIPTEN__
        emscripten_sleep(1);
#else
        wgpuInstanceProcessEvents(instance);
#endif
    }
    (void)instance;
}

#ifdef __EMSCRIPTEN__

typedef struct {
    FlecsCompatBufferMapCallback callback;
    void *userdata;
} FlecsCompatBufferMapCtx;

static void flecsEngine_onBufferMap(
    WGPUBufferMapAsyncStatus status,
    void *userdata)
{
    FlecsCompatBufferMapCtx *ctx = userdata;
    FlecsCompatBufferMapCallback cb = ctx->callback;
    void *ud = ctx->userdata;
    ecs_os_free(ctx);
    cb(status, NULL, ud);
}

void flecsEngine_bufferMapAsync(
    WGPUBuffer buffer,
    WGPUMapMode mode,
    size_t offset,
    size_t size,
    FlecsCompatBufferMapCallback callback,
    void *userdata)
{
    FlecsCompatBufferMapCtx *ctx = ecs_os_malloc_t(FlecsCompatBufferMapCtx);
    ctx->callback = callback;
    ctx->userdata = userdata;

    wgpuBufferMapAsync(buffer, mode, offset, size,
        flecsEngine_onBufferMap, ctx);
}

#else /* native */

typedef struct {
    FlecsCompatBufferMapCallback callback;
    void *userdata;
} FlecsCompatBufferMapCtx;

static void flecsEngine_onBufferMap(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)userdata2;

    FlecsCompatBufferMapCtx *ctx = userdata1;
    const char *msg = (message.data && message.length > 0) ? message.data : NULL;
    FlecsCompatBufferMapCallback cb = ctx->callback;
    void *ud = ctx->userdata;
    ecs_os_free(ctx);
    cb(status, msg, ud);
}

void flecsEngine_bufferMapAsync(
    WGPUBuffer buffer,
    WGPUMapMode mode,
    size_t offset,
    size_t size,
    FlecsCompatBufferMapCallback callback,
    void *userdata)
{
    FlecsCompatBufferMapCtx *ctx = ecs_os_malloc_t(FlecsCompatBufferMapCtx);
    ctx->callback = callback;
    ctx->userdata = userdata;

    WGPUBufferMapCallbackInfo info = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = flecsEngine_onBufferMap,
        .userdata1 = ctx,
        .userdata2 = NULL
    };

    wgpuBufferMapAsync(buffer, mode, offset, size, info);
}

#endif /* __EMSCRIPTEN__ */

/* ---- Surface / swap-chain ---- */

#ifdef __EMSCRIPTEN__
static WGPUSwapChain compat_swap_chain;
#else
extern void *flecs_create_metal_layer(void *ns_window);
#endif

WGPUSurface flecsEngine_createSurface(
    WGPUInstance instance,
    GLFWwindow *window)
{
#ifdef __EMSCRIPTEN__
    (void)window;

    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvas_desc = {
        .chain = {
            .sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector
        },
        .selector = "#canvas"
    };

    WGPUSurfaceDescriptor surface_desc = {
        .nextInChain = (WGPUChainedStruct*)&canvas_desc
    };
#else
    void *metal_layer = flecs_create_metal_layer(
        glfwGetCocoaWindow(window));

    WGPUSurfaceSourceMetalLayer metal_desc = {
        .chain = { .sType = WGPUSType_SurfaceSourceMetalLayer },
        .layer = metal_layer
    };

    WGPUSurfaceDescriptor surface_desc = {
        .nextInChain = (WGPUChainedStruct*)&metal_desc
    };
#endif

    return wgpuInstanceCreateSurface(instance, &surface_desc);
}

int flecsEngine_configureSurface(
    FlecsEngineImpl *impl)
{
#ifdef __EMSCRIPTEN__
    WGPUTextureFormat preferred =
        wgpuSurfaceGetPreferredFormat(impl->surface, impl->adapter);

    impl->surface_config = (WGPUSurfaceConfiguration){
        .device = impl->device,
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = preferred,
        .width = (uint32_t)impl->width,
        .height = (uint32_t)impl->height,
        .presentMode = WGPUPresentMode_Fifo,
    };

    WGPUSwapChainDescriptor sc_desc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = preferred,
        .width = (uint32_t)impl->width,
        .height = (uint32_t)impl->height,
        .presentMode = WGPUPresentMode_Fifo,
    };

    compat_swap_chain =
        wgpuDeviceCreateSwapChain(impl->device, impl->surface, &sc_desc);
    if (!compat_swap_chain) {
        ecs_err("Failed to create swap chain\n");
        return -1;
    }
#else
    WGPUSurfaceCapabilities surface_caps = {0};
    if (wgpuSurfaceGetCapabilities(
        impl->surface,
        impl->adapter,
        &surface_caps) != WGPUStatus_Success ||
        surface_caps.formatCount == 0)
    {
        ecs_err("Failed to get surface capabilities\n");
        wgpuSurfaceCapabilitiesFreeMembers(surface_caps);
        return -1;
    }

    impl->surface_config = (WGPUSurfaceConfiguration){
        .device = impl->device,
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = surface_caps.formats[0],
        .width = (uint32_t)impl->width,
        .height = (uint32_t)impl->height,
        .presentMode = WGPUPresentMode_Fifo,
        .alphaMode = surface_caps.alphaModes[0]
    };

    wgpuSurfaceConfigure(impl->surface, &impl->surface_config);
    wgpuSurfaceCapabilitiesFreeMembers(surface_caps);
#endif

    return 0;
}

void flecsEngine_reconfigureSurface(
    FlecsEngineImpl *impl)
{
#ifdef __EMSCRIPTEN__
    if (compat_swap_chain) {
        wgpuSwapChainRelease(compat_swap_chain);
    }

    WGPUSwapChainDescriptor sc_desc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = impl->surface_config.format,
        .width = impl->surface_config.width,
        .height = impl->surface_config.height,
        .presentMode = WGPUPresentMode_Fifo,
    };

    compat_swap_chain =
        wgpuDeviceCreateSwapChain(impl->device, impl->surface, &sc_desc);
#else
    wgpuSurfaceConfigure(impl->surface, &impl->surface_config);
#endif
}

void flecsEngine_getFramebufferSize(
    GLFWwindow *window,
    int *width,
    int *height)
{
#ifdef __EMSCRIPTEN__
    (void)window;

    double css_w, css_h;
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);
    double dpr = emscripten_get_device_pixel_ratio();
    *width = (int)(css_w * dpr);
    *height = (int)(css_h * dpr);
#else
    glfwGetFramebufferSize(window, width, height);
#endif
}

int flecsEngine_acquireFrame(
    FlecsEngineImpl *impl,
    FlecsEngineSurface *target)
{
#ifdef __EMSCRIPTEN__
    WGPUTextureView view =
        wgpuSwapChainGetCurrentTextureView(compat_swap_chain);
    if (!view) {
        ecs_err("Failed to get current swap chain texture view\n");
        return -1;
    }

    target->view_texture = view;
    target->owns_view_texture = true;
#else
    WGPUSurfaceTexture surface_frame = {0};
    wgpuSurfaceGetCurrentTexture(impl->surface, &surface_frame);

    target->surface_status = surface_frame.status;
    if (surface_frame.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_frame.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        if (surface_frame.texture) {
            wgpuTextureRelease(surface_frame.texture);
        }

        wgpuSurfaceConfigure(impl->surface, &impl->surface_config);
        return 1;
    }

    target->surface_texture = surface_frame.texture;
    target->view_texture = wgpuTextureCreateView(target->surface_texture, NULL);
    target->owns_view_texture = true;
    if (!target->view_texture) {
        ecs_err("Failed to create color texture view\n");
        wgpuTextureRelease(target->surface_texture);
        target->surface_texture = NULL;
        return -1;
    }
#endif

    return 0;
}

void flecsEngine_presentSurface(
    WGPUSurface surface)
{
#ifdef __EMSCRIPTEN__
    (void)surface;
#else
    wgpuSurfacePresent(surface);
#endif
}

void flecsEngine_releaseSwapChain(void)
{
#ifdef __EMSCRIPTEN__
    if (compat_swap_chain) {
        wgpuSwapChainRelease(compat_swap_chain);
        compat_swap_chain = NULL;
    }
#endif
}

/* ---- Gamma correction ---- */

#ifdef __EMSCRIPTEN__
extern WGPUTextureView flecsEngine_gamma_getRenderTarget(
    FlecsEngineImpl *impl);
extern void flecsEngine_gamma_blit(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView src_view,
    WGPUTextureView dst_view);
#endif

WGPUTextureView flecsEngine_getGammaRenderTarget(
    FlecsEngineImpl *impl,
    WGPUTextureView fallback)
{
#ifdef __EMSCRIPTEN__
    WGPUTextureView target = flecsEngine_gamma_getRenderTarget(impl);
    return target ? target : fallback;
#else
    (void)impl;
    return fallback;
#endif
}

void flecsEngine_gammaBlitIfNeeded(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView render_target,
    WGPUTextureView frame_target)
{
#ifdef __EMSCRIPTEN__
    if (render_target != frame_target) {
        flecsEngine_gamma_blit(impl, encoder, render_target, frame_target);
    }
#else
    (void)impl;
    (void)encoder;
    (void)render_target;
    (void)frame_target;
#endif
}
