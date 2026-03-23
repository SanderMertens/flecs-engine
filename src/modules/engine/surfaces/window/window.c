#define FLECS_ENGINE_WINDOW_IMPL
#include "window.h"

ECS_COMPONENT_DECLARE(FlecsWindow);

static bool flecs_engine_glfw_initialized = false;

static int flecsEngine_window_initInstance(
    FlecsEngineImpl *impl,
    const void *config)
{
    const FlecsEngineWindowOutputConfig *output_cfg = config;
    if (!output_cfg || !output_cfg->window) {
        ecs_err("Window output missing GLFWwindow\n");
        return -1;
    }

    impl->window = output_cfg->window;

    impl->surface = flecsEngine_createSurface(impl->instance, impl->window);
    if (!impl->surface) {
        ecs_err("Failed to create wgpu surface\n");
        return -1;
    }

    ecs_dbg("[engine] created instance=%p surface=%p",
        (void*)impl->instance, (void*)impl->surface);
    return 0;
}

static int flecsEngine_window_configureTarget(
    FlecsEngineImpl *impl)
{
    if (flecsEngine_configureSurface(impl)) {
        return -1;
    }

    ecs_dbg("[engine] surface configured %ux%u format=%d",
        impl->surface_config.width,
        impl->surface_config.height,
        (int)impl->surface_config.format);

    return 0;
}

static int flecsEngine_window_prepareFrame(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (glfwWindowShouldClose(impl->window)) {
        ecs_quit(world);
        return 1;
    }

    int width = 0;
    int height = 0;
    flecsEngine_getFramebufferSize(impl->window, &width, &height);

    if (width != impl->width || height != impl->height) {
        impl->width = width;
        impl->height = height;
        impl->surface_config.width = (uint32_t)width;
        impl->surface_config.height = (uint32_t)height;

        int32_t scale = impl->resolution_scale;
        if (scale < 1) scale = 1;
        impl->actual_width = width / scale;
        impl->actual_height = height / scale;

        flecsEngine_reconfigureSurface(impl);
    }

    return 0;
}

static int flecsEngine_window_acquireFrame(
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

    return flecsEngine_acquireFrame(impl, target);
}

static int flecsEngine_window_encodeFrame(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    FlecsEngineSurface *target)
{
    (void)impl;
    (void)encoder;
    (void)target;
    return 0;
}

static int flecsEngine_window_submitFrame(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    const FlecsEngineSurface *target)
{
    (void)world;
    (void)target;

    flecsEngine_presentSurface(impl->surface);

    return 0;
}

static void flecsEngine_window_onFrameFailed(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    (void)world;
    (void)impl;
}

static void flecsEngine_window_cleanup(
    FlecsEngineImpl *impl,
    bool terminate_runtime)
{
    flecsEngine_releaseSwapChain();

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
    .init_instance = flecsEngine_window_initInstance,
    .configure_target = flecsEngine_window_configureTarget,
    .prepare_frame = flecsEngine_window_prepareFrame,
    .acquire_frame = flecsEngine_window_acquireFrame,
    .encode_frame = flecsEngine_window_encodeFrame,
    .submit_frame = flecsEngine_window_submitFrame,
    .on_frame_failed = flecsEngine_window_onFrameFailed,
    .cleanup = flecsEngine_window_cleanup
};

static void FlecsOnWindowCreate(
    ecs_iter_t *it)
{
    FlecsEngineImpl *existing = ecs_singleton_ensure(it->world, FlecsEngineImpl);
    if (existing && existing->device) {
        FlecsWindow *wnd = ecs_field(it, FlecsWindow, 0);
        int32_t scale = wnd->resolution_scale;
        if (scale < 1) scale = 1;
        existing->resolution_scale = scale;
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
        .height = h,
        .resolution_scale = wnd->resolution_scale,
        .sky_color = wnd->sky_color,
        .ground_color = wnd->ground_color
    };

    if (flecsEngine_init(it->world, &output_desc)) {
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
