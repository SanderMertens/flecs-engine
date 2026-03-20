#ifndef FLECS_ENGINE_RENDERER_SHADERS_H
#define FLECS_ENGINE_RENDERER_SHADERS_H

#include "../../../types.h"

bool flecsEngine_shader_usesIbl(
    const FlecsShader *shader);

bool flecsEngine_shader_usesShadow(
    const FlecsShader *shader);

bool flecsEngine_shader_usesCluster(
    const FlecsShader *shader);

ecs_entity_t flecsEngine_shader_pbrColored(
    ecs_world_t *world);

ecs_entity_t flecsEngine_shader_pbrColoredMaterialIndex(
    ecs_world_t *world);

ecs_entity_t flecsEngine_shader_infiniteGrid(
    ecs_world_t *world);

ecs_entity_t flecsEngine_shader_skybox(
    ecs_world_t *world);

#endif
