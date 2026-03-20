#define FLECS_ENGINE_LIGHT_IMPL
#include "light.h"

void FlecsEngineLightImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineLight);

    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, FlecsDirectionalLight);
    ECS_META_COMPONENT(world, FlecsPointLight);
    ECS_META_COMPONENT(world, FlecsSpotLight);

    ecs_add_pair(world, ecs_id(FlecsDirectionalLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsPointLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsSpotLight), EcsOnInstantiate, EcsInherit);
}
