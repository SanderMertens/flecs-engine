#define FLECS_ENGINE_MATERIAL_IMPL
#include "material.h"

ECS_COMPONENT_DECLARE(FlecsRgba);

void FlecsEngineMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineMaterial);

    ecs_set_name_prefix(world, "Flecs");

    ecs_id(FlecsRgba) = ecs_id(flecs_rgba_t);
    
    ecs_add_pair(world, ecs_id(FlecsRgba), EcsOnInstantiate, EcsInherit);
}
