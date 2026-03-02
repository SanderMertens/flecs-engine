#ifndef FLECS_ENGINE_INPUT_H
#define FLECS_ENGINE_INPUT_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_INPUT_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(flecs_engine_key_state_t, {
    bool pressed;
    bool state;
    bool current;
});

ECS_STRUCT(flecs_engine_mouse_coord_t, {
    float x;
    float y;
});

ECS_STRUCT(flecs_engine_mouse_state_t, {
    flecs_engine_key_state_t left;
    flecs_engine_key_state_t right;
    flecs_engine_mouse_coord_t wnd;
    flecs_engine_mouse_coord_t rel;
    flecs_engine_mouse_coord_t view;
    flecs_engine_mouse_coord_t scroll;
});

ECS_STRUCT(FlecsInput, {
    flecs_engine_key_state_t keys[128];
    flecs_engine_mouse_state_t mouse;
});

#define FLECS_KEY_UNKNOWN          ((int)0)
#define FLECS_KEY_RETURN           ((int)'\r')
#define FLECS_KEY_ESCAPE           ((int)'\033')
#define FLECS_KEY_BACKSPACE        ((int)'\b')
#define FLECS_KEY_TAB              ((int)'\t')
#define FLECS_KEY_SPACE            ((int)' ')
#define FLECS_KEY_EXCLAIM          ((int)'!')
#define FLECS_KEY_QUOTEDBL         ((int)'"')
#define FLECS_KEY_HASH             ((int)'#')
#define FLECS_KEY_PERCENT          ((int)'%')
#define FLECS_KEY_DOLLAR           ((int)'$')
#define FLECS_KEY_AMPERSAND        ((int)'&')
#define FLECS_KEY_QUOTE            ((int)'\'')
#define FLECS_KEY_LEFT_PAREN       ((int)'(')
#define FLECS_KEY_RIGHT_PAREN      ((int)')')
#define FLECS_KEY_ASTERISK         ((int)'*')
#define FLECS_KEY_PLUS             ((int)'+')
#define FLECS_KEY_COMMA            ((int)',')
#define FLECS_KEY_MINUS            ((int)'-')
#define FLECS_KEY_PERIOD           ((int)'.')
#define FLECS_KEY_SLASH            ((int)'/')

#define FLECS_KEY_0                ((int)'0')
#define FLECS_KEY_1                ((int)'1')
#define FLECS_KEY_2                ((int)'2')
#define FLECS_KEY_3                ((int)'3')
#define FLECS_KEY_4                ((int)'4')
#define FLECS_KEY_5                ((int)'5')
#define FLECS_KEY_6                ((int)'6')
#define FLECS_KEY_7                ((int)'7')
#define FLECS_KEY_8                ((int)'8')
#define FLECS_KEY_9                ((int)'9')

#define FLECS_KEY_COLON            ((int)':')
#define FLECS_KEY_SEMICOLON        ((int)';')
#define FLECS_KEY_LESS             ((int)'<')
#define FLECS_KEY_EQUAL            ((int)'=')
#define FLECS_KEY_GREATER          ((int)'>')
#define FLECS_KEY_QUESTION         ((int)'?')
#define FLECS_KEY_AT               ((int)'@')
#define FLECS_KEY_LEFT_BRACKET     ((int)'[')
#define FLECS_KEY_RIGHT_BRACKET    ((int)']')
#define FLECS_KEY_BACKSLASH        ((int)'\\')
#define FLECS_KEY_CARET            ((int)'^')
#define FLECS_KEY_UNDERSCORE       ((int)'_')
#define FLECS_KEY_GRAVE_ACCENT     ((int)'`')
#define FLECS_KEY_APOSTROPHE       ((int)'\'')

#define FLECS_KEY_A                ((int)'a')
#define FLECS_KEY_B                ((int)'b')
#define FLECS_KEY_C                ((int)'c')
#define FLECS_KEY_D                ((int)'d')
#define FLECS_KEY_E                ((int)'e')
#define FLECS_KEY_F                ((int)'f')
#define FLECS_KEY_G                ((int)'g')
#define FLECS_KEY_H                ((int)'h')
#define FLECS_KEY_I                ((int)'i')
#define FLECS_KEY_J                ((int)'j')
#define FLECS_KEY_K                ((int)'k')
#define FLECS_KEY_L                ((int)'l')
#define FLECS_KEY_M                ((int)'m')
#define FLECS_KEY_N                ((int)'n')
#define FLECS_KEY_O                ((int)'o')
#define FLECS_KEY_P                ((int)'p')
#define FLECS_KEY_Q                ((int)'q')
#define FLECS_KEY_R                ((int)'r')
#define FLECS_KEY_S                ((int)'s')
#define FLECS_KEY_T                ((int)'t')
#define FLECS_KEY_U                ((int)'u')
#define FLECS_KEY_V                ((int)'v')
#define FLECS_KEY_W                ((int)'w')
#define FLECS_KEY_X                ((int)'x')
#define FLECS_KEY_Y                ((int)'y')
#define FLECS_KEY_Z                ((int)'z')
#define FLECS_KEY_DELETE           ((int)127)

#define FLECS_KEY_RIGHT            ((int)'R')
#define FLECS_KEY_LEFT             ((int)'L')
#define FLECS_KEY_DOWN             ((int)'D')
#define FLECS_KEY_UP               ((int)'U')
#define FLECS_KEY_LEFT_CTRL        ((int)'C')
#define FLECS_KEY_LEFT_ALT         ((int)'A')
#define FLECS_KEY_LEFT_SHIFT       ((int)'S')
#define FLECS_KEY_RIGHT_CTRL       ((int)'T')
#define FLECS_KEY_RIGHT_ALT        ((int)'Z')
#define FLECS_KEY_RIGHT_SHIFT      ((int)'H')
#define FLECS_KEY_INSERT           ((int)'I')
#define FLECS_KEY_HOME             ((int)'H')
#define FLECS_KEY_END              ((int)'E')
#define FLECS_KEY_PAGE_UP          ((int)'O')
#define FLECS_KEY_PAGE_DOWN        ((int)'P')

#endif
