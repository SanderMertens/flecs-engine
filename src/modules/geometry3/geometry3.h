#include "../../types.h"

#ifndef FLECS_ENGINE_GEOMETRY_3D_IMPL
#define FLECS_ENGINE_GEOMETRY_3D_IMPL

typedef struct {
    ecs_entity_t asset;
    int32_t source;
} FlecsGeometryBinding3;

extern ECS_COMPONENT_DECLARE(FlecsGeometryBinding3);

typedef struct {
    uint8_t value;
} FlecsGeometryConflict3;

extern ECS_COMPONENT_DECLARE(FlecsGeometryConflict3);

const FlecsMesh3Impl* flecsEngineGeometry3EnsureUnitBox(
    ecs_world_t *world);

void FlecsEngineGeometry3Import(
    ecs_world_t *world);

#endif
