#ifndef FLECS_ENGINE
#define FLECS_ENGINE

#include <flecs.h>
#include <cglm/cglm.h>

typedef struct {
    float x, y, z;
} flecs_vec3_t;

extern ECS_COMPONENT_DECLARE(flecs_vec3_t);

typedef mat4 flecs_mat4_t;

extern ECS_COMPONENT_DECLARE(flecs_mat4_t);

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} flecs_rgba_t;

extern ECS_COMPONENT_DECLARE(flecs_rgba_t);

#include "modules/window.h"
#include "modules/frame_output.h"
#include "modules/engine.h"
#include "modules/renderer.h"
#include "modules/transform3.h"
#include "modules/camera.h"
#include "modules/geometry_mesh.h"
#include "modules/geometry_primitives3.h"
#include "modules/material.h"

void FlecsEngineImport(
    ecs_world_t *world);

#endif
