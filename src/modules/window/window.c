#define FLECS_ENGINE_WINDOW_IMPL
#include "window.h"
#include "../engine/engine.h"

ECS_COMPONENT_DECLARE(FlecsWindow);

static bool flecs_engine_glfw_initialized = false;

static void FlecsOnWindowCreate(
    ecs_iter_t *it) 
{
    FlecsWindow *wnd = ecs_field(it, FlecsWindow, 0);

    if (!flecs_engine_glfw_initialized) {
        if (!glfwInit()) {
            ecs_err("Failed to initialize GLFW\n");
            return;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        flecs_engine_glfw_initialized = true;
    }

    // Create window
    int w = wnd->width, h = wnd->height;
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

    if (flecsEngineInit(it->world, window, w, h)) {
        goto error;
    }

    return;
error:
    glfwTerminate();
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
