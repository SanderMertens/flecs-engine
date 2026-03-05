#include "material.h"

ECS_COMPONENT_DECLARE(FlecsRgba);
ECS_COMPONENT_DECLARE(FlecsPbrMaterial);
ECS_COMPONENT_DECLARE(FlecsEmissive);
ECS_COMPONENT_DECLARE(FlecsMaterialId);

static void FlecsEmissive_on_add(
    ecs_iter_t *it)
{
    FlecsEmissive *emissive = ecs_field(it, FlecsEmissive, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        const FlecsRgba *color = ecs_get(it->world, it->entities[i], FlecsRgba);
        if (color) {
            ecs_os_memcpy_t(&emissive[i].color, &color, FlecsRgba);
        }
    }
}

static void FlecsMaterialIdOnAdd(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsEngineImpl *impl = ecs_singleton_get_mut(world, FlecsEngineImpl);
    if (!impl) {
        return;
    }

    bool modified = false;

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (ecs_has(world, e, FlecsMaterialId)) {
            continue;
        }

        FlecsMaterialId *material_id = ecs_ensure(world, e, FlecsMaterialId);
        material_id->value = impl->last_material_id;
        impl->last_material_id ++;
        ecs_modified(world, e, FlecsMaterialId);
    }
}

void FlecsEngineMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineMaterial);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsRgba);
    ecs_struct(world, {
        .entity = ecs_id(FlecsRgba),
        .members = {
            { .name = "r", .type = ecs_id(ecs_u8_t) },
            { .name = "g", .type = ecs_id(ecs_u8_t) },
            { .name = "b", .type = ecs_id(ecs_u8_t) },
            { .name = "a", .type = ecs_id(ecs_u8_t) },
        }
    });
    ecs_add_pair(world, ecs_id(FlecsRgba), EcsOnInstantiate, EcsInherit);

    ECS_COMPONENT_DEFINE(world, FlecsPbrMaterial);
    ecs_struct(world, {
        .entity = ecs_id(FlecsPbrMaterial),
        .members = {
            { .name = "metallic", .type = ecs_id(ecs_f32_t) },
            { .name = "roughness", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_add_pair(world, ecs_id(FlecsPbrMaterial), EcsOnInstantiate, EcsInherit);

    ECS_COMPONENT_DEFINE(world, FlecsEmissive);
    ecs_struct(world, {
        .entity = ecs_id(FlecsEmissive),
        .members = {
            { .name = "color", .type = ecs_id(flecs_rgba_t) },
            { .name = "strength", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_add_pair(world, ecs_id(FlecsEmissive), EcsOnInstantiate, EcsInherit);
    ECS_COMPONENT_DEFINE(world, FlecsMaterialId);
    ecs_struct(world, {
        .entity = ecs_id(FlecsMaterialId),
        .members = {
            { .name = "value", .type = ecs_id(ecs_u32_t) }
        }
    });
    ecs_add_pair(world, ecs_id(FlecsMaterialId), EcsOnInstantiate, EcsInherit);

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .parent = ecs_lookup(world, "flecs.engine.material"),
            .name = "MaterialIdOnAdd"
        }),
        .query.terms = {
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = {EcsOnAdd},
        .yield_existing = true,
        .callback = FlecsMaterialIdOnAdd
    });
}
