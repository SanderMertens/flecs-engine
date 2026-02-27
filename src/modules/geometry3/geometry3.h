#include "../../types.h"

#ifndef FLECS_ENGINE_GEOMETRY_3D_IMPL
#define FLECS_ENGINE_GEOMETRY_3D_IMPL

typedef struct {
    ecs_map_t sphere_cache;
    ecs_map_t cylinder_cache;
    ecs_entity_t unit_box_asset;
    ecs_entity_t unit_pyramid_asset;
    ecs_entity_t unit_quad_asset;
    ecs_entity_t unit_triangle_asset;
} FlecsGeometry3Cache;

extern ECS_COMPONENT_DECLARE(FlecsGeometry3Cache);

ecs_entity_t flecsGeometry3_createAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx,
    const char *name);

const FlecsMesh3Impl* flecsGeometry3_getBoxAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsGeometry3_getPyramidAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsGeometry3_getQuadAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsGeometry3_getTriangleAsset(
    ecs_world_t *world);

void FlecsEngineGeometry3Import(
    ecs_world_t *world);

#endif
