#define FLECS_ENGINE_WINDOW_IMPL
#include "window.h"

ECS_COMPONENT_DECLARE(FlecsWindow);

static bool flecs_engine_glfw_initialized = false;

static int flecsEngineWindowInitInstance(
    FlecsEngineImpl *impl,
    const void *config)
{
    const FlecsEngineWindowOutputConfig *output_cfg = config;
    if (!output_cfg || !output_cfg->window) {
        ecs_err("Window output missing GLFWwindow\n");
        return -1;
    }

    impl->window = output_cfg->window;

    void *metal_layer = flecs_create_metal_layer(
        glfwGetCocoaWindow(impl->window));

    WGPUSurfaceSourceMetalLayer metal_desc = {
        .chain = { .sType = WGPUSType_SurfaceSourceMetalLayer },
        .layer = metal_layer
    };

    WGPUSurfaceDescriptor surface_desc = {
        .nextInChain = (WGPUChainedStruct*)&metal_desc
    };

    impl->surface = wgpuInstanceCreateSurface(impl->instance, &surface_desc);
    if (!impl->surface) {
        ecs_err("Failed to create wgpu surface\n");
        return -1;
    }

    ecs_dbg("[engine] created instance=%p surface=%p",
        (void*)impl->instance, (void*)impl->surface);
    return 0;
}

static int flecsEngineWindowConfigureTarget(
    FlecsEngineImpl *impl)
{
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

    ecs_dbg("[engine] surface configured %ux%u format=%d",
        impl->surface_config.width,
        impl->surface_config.height,
        (int)impl->surface_config.format);

    return 0;
}

static int flecsEngineWindowPrepareFrame(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (glfwWindowShouldClose(impl->window)) {
        ecs_quit(world);
        return 1;
    }

    glfwPollEvents();

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(impl->window, &width, &height);

    if (width != impl->width || height != impl->height) {
        impl->width = width;
        impl->height = height;
        impl->surface_config.width = (uint32_t)width;
        impl->surface_config.height = (uint32_t)height;
        wgpuSurfaceConfigure(impl->surface, &impl->surface_config);
    }

    return 0;
}

static int flecsEngineWindowAcquireFrame(
    FlecsEngineImpl *impl,
    FlecsEngineSurface *target)
{
    target->view_texture = NULL;
    target->surface_texture = NULL;
    target->owns_view_texture = false;
    target->surface_status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    target->readback_buffer = NULL;
    target->readback_bytes_per_row = 0;
    target->readback_buffer_size = 0;

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

    return 0;
}

static int flecsEngineWindowEncodeFrame(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    FlecsEngineSurface *target)
{
    (void)impl;
    (void)encoder;
    (void)target;
    return 0;
}

static int flecsEngineWindowSubmitFrame(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    const FlecsEngineSurface *target)
{
    (void)world;

    wgpuSurfacePresent(impl->surface);

    return 0;
}

static void flecsEngineWindowOnFrameFailed(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    (void)world;
    (void)impl;
}

static void flecsEngineWindowCleanup(
    FlecsEngineImpl *impl,
    bool terminate_runtime)
{
    if (impl->surface) {
        wgpuSurfaceRelease(impl->surface);
        impl->surface = NULL;
    }

    if (impl->window) {
        glfwDestroyWindow(impl->window);
        impl->window = NULL;
    }

    if (terminate_runtime && flecs_engine_glfw_initialized) {
        glfwTerminate();
        flecs_engine_glfw_initialized = false;
    }
}

const FlecsEngineSurfaceInterface flecsEngineWindowOutputOps = {
    .init_instance = flecsEngineWindowInitInstance,
    .configure_target = flecsEngineWindowConfigureTarget,
    .prepare_frame = flecsEngineWindowPrepareFrame,
    .acquire_frame = flecsEngineWindowAcquireFrame,
    .encode_frame = flecsEngineWindowEncodeFrame,
    .submit_frame = flecsEngineWindowSubmitFrame,
    .on_frame_failed = flecsEngineWindowOnFrameFailed,
    .cleanup = flecsEngineWindowCleanup
};

static void FlecsOnWindowCreate(
    ecs_iter_t *it)
{
    if (ecs_singleton_get(it->world, FlecsEngineImpl)) {
        return;
    }

    FlecsWindow *wnd = ecs_field(it, FlecsWindow, 0);

    if (!flecs_engine_glfw_initialized) {
        if (!glfwInit()) {
            ecs_err("Failed to initialize GLFW\n");
            ecs_quit(it->world);
            return;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        flecs_engine_glfw_initialized = true;
    }

    int w = wnd->width;
    int h = wnd->height;
    if (!w) w = 1280;
    if (!h) h = 800;

    const char *title = wnd->title;
    if (!title) title = "Flecs Engine";

    GLFWwindow *window = glfwCreateWindow(w, h, title, NULL, NULL);
    if (!window) {
        ecs_err("Failed to create window '%s'\n", title);
        goto error;
    }

    glfwGetFramebufferSize(window, &w, &h);

    FlecsEngineWindowOutputConfig output_cfg = {
        .window = window
    };

    FlecsEngineOutputDesc output_desc = {
        .ops = &flecsEngineWindowOutputOps,
        .config = &output_cfg,
        .width = w,
        .height = h
    };

    if (flecsEngineInit(it->world, &output_desc)) {
        goto error;
    }

    return;
error:
    glfwTerminate();
    flecs_engine_glfw_initialized = false;
    ecs_quit(it->world);
}

void FlecsEngineWindowImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineWindow);

    ecs_set_name_prefix(world, "Flecs");

    ecs_set_scope(world, ecs_get_parent(world, ecs_id(FlecsEngineWindow)));

    ECS_META_COMPONENT(world, FlecsWindow);

    ecs_set_hooks(world, FlecsWindow, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsOnWindowCreate
    });
}
