#define FLECS_ENGINE_INPUT_IMPL
#include "input.h"

ECS_COMPONENT_DECLARE(FlecsInput);
ECS_COMPONENT_DECLARE(FlecsEngineImpl);

static FlecsInput *flecs_engine_input_active = NULL;
static GLFWwindow *flecs_engine_input_window = NULL;

static int flecsEngineInputKeyCode(
    int glfw_key)
{
    switch (glfw_key) {
    case GLFW_KEY_SPACE: return FLECS_KEY_SPACE;
    case GLFW_KEY_APOSTROPHE: return FLECS_KEY_APOSTROPHE;
    case GLFW_KEY_COMMA: return FLECS_KEY_COMMA;
    case GLFW_KEY_MINUS: return FLECS_KEY_MINUS;
    case GLFW_KEY_PERIOD: return FLECS_KEY_PERIOD;
    case GLFW_KEY_SLASH: return FLECS_KEY_SLASH;
    case GLFW_KEY_0: return FLECS_KEY_0;
    case GLFW_KEY_1: return FLECS_KEY_1;
    case GLFW_KEY_2: return FLECS_KEY_2;
    case GLFW_KEY_3: return FLECS_KEY_3;
    case GLFW_KEY_4: return FLECS_KEY_4;
    case GLFW_KEY_5: return FLECS_KEY_5;
    case GLFW_KEY_6: return FLECS_KEY_6;
    case GLFW_KEY_7: return FLECS_KEY_7;
    case GLFW_KEY_8: return FLECS_KEY_8;
    case GLFW_KEY_9: return FLECS_KEY_9;
    case GLFW_KEY_SEMICOLON: return FLECS_KEY_SEMICOLON;
    case GLFW_KEY_EQUAL: return FLECS_KEY_EQUAL;
    case GLFW_KEY_A: return FLECS_KEY_A;
    case GLFW_KEY_B: return FLECS_KEY_B;
    case GLFW_KEY_C: return FLECS_KEY_C;
    case GLFW_KEY_D: return FLECS_KEY_D;
    case GLFW_KEY_E: return FLECS_KEY_E;
    case GLFW_KEY_F: return FLECS_KEY_F;
    case GLFW_KEY_G: return FLECS_KEY_G;
    case GLFW_KEY_H: return FLECS_KEY_H;
    case GLFW_KEY_I: return FLECS_KEY_I;
    case GLFW_KEY_J: return FLECS_KEY_J;
    case GLFW_KEY_K: return FLECS_KEY_K;
    case GLFW_KEY_L: return FLECS_KEY_L;
    case GLFW_KEY_M: return FLECS_KEY_M;
    case GLFW_KEY_N: return FLECS_KEY_N;
    case GLFW_KEY_O: return FLECS_KEY_O;
    case GLFW_KEY_P: return FLECS_KEY_P;
    case GLFW_KEY_Q: return FLECS_KEY_Q;
    case GLFW_KEY_R: return FLECS_KEY_R;
    case GLFW_KEY_S: return FLECS_KEY_S;
    case GLFW_KEY_T: return FLECS_KEY_T;
    case GLFW_KEY_U: return FLECS_KEY_U;
    case GLFW_KEY_V: return FLECS_KEY_V;
    case GLFW_KEY_W: return FLECS_KEY_W;
    case GLFW_KEY_X: return FLECS_KEY_X;
    case GLFW_KEY_Y: return FLECS_KEY_Y;
    case GLFW_KEY_Z: return FLECS_KEY_Z;
    case GLFW_KEY_LEFT_BRACKET: return FLECS_KEY_LEFT_BRACKET;
    case GLFW_KEY_BACKSLASH: return FLECS_KEY_BACKSLASH;
    case GLFW_KEY_RIGHT_BRACKET: return FLECS_KEY_RIGHT_BRACKET;
    case GLFW_KEY_GRAVE_ACCENT: return FLECS_KEY_GRAVE_ACCENT;
    case GLFW_KEY_ESCAPE: return FLECS_KEY_ESCAPE;
    case GLFW_KEY_ENTER: return FLECS_KEY_RETURN;
    case GLFW_KEY_TAB: return FLECS_KEY_TAB;
    case GLFW_KEY_BACKSPACE: return FLECS_KEY_BACKSPACE;
    case GLFW_KEY_INSERT: return FLECS_KEY_INSERT;
    case GLFW_KEY_DELETE: return FLECS_KEY_DELETE;
    case GLFW_KEY_RIGHT: return FLECS_KEY_RIGHT;
    case GLFW_KEY_LEFT: return FLECS_KEY_LEFT;
    case GLFW_KEY_DOWN: return FLECS_KEY_DOWN;
    case GLFW_KEY_UP: return FLECS_KEY_UP;
    case GLFW_KEY_PAGE_UP: return FLECS_KEY_PAGE_UP;
    case GLFW_KEY_PAGE_DOWN: return FLECS_KEY_PAGE_DOWN;
    case GLFW_KEY_HOME: return FLECS_KEY_HOME;
    case GLFW_KEY_END: return FLECS_KEY_END;
    case GLFW_KEY_LEFT_SHIFT: return FLECS_KEY_LEFT_SHIFT;
    case GLFW_KEY_LEFT_CONTROL: return FLECS_KEY_LEFT_CTRL;
    case GLFW_KEY_LEFT_ALT: return FLECS_KEY_LEFT_ALT;
    case GLFW_KEY_RIGHT_SHIFT: return FLECS_KEY_RIGHT_SHIFT;
    case GLFW_KEY_RIGHT_CONTROL: return FLECS_KEY_RIGHT_CTRL;
    case GLFW_KEY_RIGHT_ALT: return FLECS_KEY_RIGHT_ALT;
    default:
        return FLECS_KEY_UNKNOWN;
    }
}

static flecs_engine_key_state_t* flecsEngineInputKeyGet(
    FlecsInput *input,
    int key_code)
{
    int32_t key_count = (int32_t)(sizeof(input->keys) / sizeof(input->keys[0]));
    if (key_code < 0 || key_code >= key_count) {
        return NULL;
    }
    return &input->keys[key_code];
}

static void flecsEngineInputKeyDown(
    flecs_engine_key_state_t *key)
{
    if (!key) {
        return;
    }

    if (key->state) {
        key->pressed = false;
    } else {
        key->pressed = true;
    }

    key->state = true;
    key->current = true;
}

static void flecsEngineInputKeyUp(
    flecs_engine_key_state_t *key)
{
    if (!key) {
        return;
    }

    key->current = false;
}

static void flecsEngineInputKeyReset(
    flecs_engine_key_state_t *state)
{
    if (!state->current) {
        state->state = false;
        state->pressed = false;
    } else if (state->state) {
        state->pressed = false;
    }
}

static void flecsEngineInputKeysReset(
    FlecsInput *input)
{
    int32_t key_count = (int32_t)(sizeof(input->keys) / sizeof(input->keys[0]));
    for (int32_t k = 0; k < key_count; k ++) {
        flecsEngineInputKeyReset(&input->keys[k]);
    }
}

static void flecsEngineInputMouseButtonReset(
    flecs_engine_key_state_t *mouse)
{
    flecsEngineInputKeyReset(mouse);
}

static void flecsEngineInputMouseReset(
    FlecsInput *input)
{
    flecsEngineInputMouseButtonReset(&input->mouse.left);
    flecsEngineInputMouseButtonReset(&input->mouse.right);
    input->mouse.rel = (flecs_engine_mouse_coord_t){0};
    input->mouse.scroll = (flecs_engine_mouse_coord_t){0};
}

static void flecsEngineInputOnKey(
    GLFWwindow *window,
    int key,
    int scancode,
    int action,
    int mods)
{
    (void)window;
    (void)scancode;
    (void)mods;

    FlecsInput *input = flecs_engine_input_active;
    if (!input) {
        return;
    }

    flecs_engine_key_state_t *state = flecsEngineInputKeyGet(
        input, flecsEngineInputKeyCode(key));

    if (action == GLFW_RELEASE) {
        flecsEngineInputKeyUp(state);
    } else if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        flecsEngineInputKeyDown(state);
    }
}

static void flecsEngineInputOnMouseButton(
    GLFWwindow *window,
    int button,
    int action,
    int mods)
{
    (void)window;
    (void)mods;

    FlecsInput *input = flecs_engine_input_active;
    if (!input) {
        return;
    }

    flecs_engine_key_state_t *state = NULL;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        state = &input->mouse.left;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        state = &input->mouse.right;
    }

    if (!state) {
        return;
    }

    if (action == GLFW_RELEASE) {
        flecsEngineInputKeyUp(state);
    } else if (action == GLFW_PRESS) {
        flecsEngineInputKeyDown(state);
    }
}

static void flecsEngineInputOnScroll(
    GLFWwindow *window,
    double xoffset,
    double yoffset)
{
    (void)window;

    FlecsInput *input = flecs_engine_input_active;
    if (!input) {
        return;
    }

    input->mouse.scroll.x += (float)xoffset;
    input->mouse.scroll.y += (float)yoffset;
}

static void flecsEngineInputBindWindow(
    GLFWwindow *window)
{
    if (!window || window == flecs_engine_input_window) {
        return;
    }

    flecs_engine_input_window = window;
    glfwSetKeyCallback(window, flecsEngineInputOnKey);
    glfwSetMouseButtonCallback(window, flecsEngineInputOnMouseButton);
    glfwSetScrollCallback(window, flecsEngineInputOnScroll);
}

static void FlecsInputFrame(
    ecs_iter_t *it)
{
    const FlecsEngineImpl *engine = ecs_singleton_get(it->world, FlecsEngineImpl);
    if (!engine || !engine->window) {
        flecs_engine_input_active = NULL;
        flecs_engine_input_window = NULL;
        return;
    }

    flecsEngineInputBindWindow(engine->window);

    FlecsInput *input = ecs_singleton_ensure(it->world, FlecsInput);

    float prev_x = input->mouse.wnd.x;
    float prev_y = input->mouse.wnd.y;

    flecsEngineInputKeysReset(input);
    flecsEngineInputMouseReset(input);

    flecs_engine_input_active = input;
    glfwPollEvents();
    flecs_engine_input_active = NULL;

    double wnd_x = 0.0;
    double wnd_y = 0.0;
    glfwGetCursorPos(engine->window, &wnd_x, &wnd_y);

    input->mouse.wnd.x = (float)wnd_x;
    input->mouse.wnd.y = (float)wnd_y;
    input->mouse.rel.x = input->mouse.wnd.x - prev_x;
    input->mouse.rel.y = input->mouse.wnd.y - prev_y;

    int32_t wnd_w = 0;
    int32_t wnd_h = 0;
    glfwGetWindowSize(engine->window, &wnd_w, &wnd_h);

    input->mouse.view.x = input->mouse.wnd.x - ((float)wnd_w * 0.5f);
    input->mouse.view.y = input->mouse.wnd.y - ((float)wnd_h * 0.5f);
}

void FlecsEngineInputImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineInput);

    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, flecs_engine_key_state_t);
    ECS_META_COMPONENT(world, flecs_engine_mouse_coord_t);
    ECS_META_COMPONENT(world, flecs_engine_mouse_state_t);
    ECS_META_COMPONENT(world, FlecsInput);

    ecs_add_id(world, ecs_id(FlecsInput), EcsSingleton);
    ecs_singleton_set(world, FlecsInput, {0});

    ECS_SYSTEM(world, FlecsInputFrame, EcsOnLoad);
}
