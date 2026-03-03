#include "material.h"

ECS_COMPONENT_DECLARE(FlecsRgba);
ECS_COMPONENT_DECLARE(FlecsPbrMaterial);

void FlecsEngineMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineMaterial);

    ecs_set_name_prefix(world, "Flecs");

    ecs_id(FlecsRgba) = ecs_id(flecs_rgba_t);
    
    ecs_add_pair(world, ecs_id(FlecsRgba), EcsOnInstantiate, EcsInherit);

    ECS_COMPONENT_DEFINE(world, FlecsPbrMaterial);
    ecs_struct(world, {
        .entity = ecs_id(FlecsPbrMaterial),
        .members = {
            { .name = "metallic", .type = ecs_id(ecs_f32_t) },
            { .name = "roughness", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_add_pair(
        world, ecs_id(FlecsPbrMaterial), EcsOnInstantiate, EcsInherit);
}
