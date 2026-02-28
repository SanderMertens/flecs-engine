#include "../../types.h"

#ifndef FLECS_ENGINE_GEOMETRY_3D_IMPL
#define FLECS_ENGINE_GEOMETRY_3D_IMPL

typedef struct {
    ecs_map_t sphere_cache;
    ecs_map_t hemisphere_cache;
    ecs_map_t icosphere_cache;
    ecs_map_t pyramid_cache;
    ecs_map_t ngon_cache;
    ecs_map_t cylinder_cache;
    ecs_entity_t unit_box_asset;
    ecs_entity_t unit_quad_asset;
    ecs_entity_t unit_triangle_asset;
    ecs_entity_t unit_right_triangle_asset;
    ecs_entity_t unit_triangle_prism_asset;
    ecs_entity_t unit_right_triangle_prism_asset;
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

const FlecsMesh3Impl* flecsGeometry3_getRightTriangleAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsGeometry3_getTrianglePrismAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsGeometry3_getRightTrianglePrismAsset(
    ecs_world_t *world);

void FlecsSphere_on_replace(
    ecs_iter_t *it);

void FlecsHemiSphere_on_replace(
    ecs_iter_t *it);

void FlecsSphere_on_set(
    ecs_iter_t *it);

void FlecsIcoSphere_on_replace(
    ecs_iter_t *it);

void FlecsNGon_on_replace(
    ecs_iter_t *it);

void FlecsPyramid_on_replace(
    ecs_iter_t *it);

void FlecsCylinder_on_replace(
    ecs_iter_t *it);

void FlecsEngineGeometry3Import(
    ecs_world_t *world);

#endif
