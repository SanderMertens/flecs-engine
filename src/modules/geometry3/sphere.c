#include "geometry3.h"
#include <math.h>
#include <stdio.h>

#define FLECS_GEOMETRY3_SPHERE_SEGMENTS_MIN (3)
#define FLECS_GEOMETRY3_SPHERE_SEGMENTS_MAX (254)

static int32_t flecsGeometry3_sphereSegmentsNormalize(
    int32_t segments)
{
    if (segments < FLECS_GEOMETRY3_SPHERE_SEGMENTS_MIN) {
        return FLECS_GEOMETRY3_SPHERE_SEGMENTS_MIN;
    }
    if (segments > FLECS_GEOMETRY3_SPHERE_SEGMENTS_MAX) {
        return FLECS_GEOMETRY3_SPHERE_SEGMENTS_MAX;
    }
    return segments;
}

static ecs_entity_t flecsGeometry3_findSphereAsset(
    const FlecsGeometry3Cache *ctx,
    int32_t segments)
{
    ecs_map_val_t *entry = ecs_map_get(
        &ctx->sphere_cache, (ecs_map_key_t)segments);
    if (!entry) {
        return 0;
    }

    return (ecs_entity_t)entry[0];
}

static void flecsGeometry3_generateSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = (rings + 1) * (cols + 1);
    const int32_t index_count = rings * cols * 6;
    const float radius = 0.5f;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    int32_t vi = 0;
    for (int32_t y = 0; y <= rings; y ++) {
        float v_t = (float)y / (float)rings;
        float theta = v_t * (float)M_PI;
        float sin_theta = sinf(theta);
        float cos_theta = cosf(theta);

        for (int32_t x = 0; x <= cols; x ++) {
            float u = (float)x / (float)cols;
            float phi = u * 2.0f * (float)M_PI;
            float sin_phi = sinf(phi);
            float cos_phi = cosf(phi);

            float nx = sin_theta * cos_phi;
            float ny = cos_theta;
            float nz = sin_theta * sin_phi;

            vn[vi] = (flecs_vec3_t){nx, ny, nz};
            v[vi] = (flecs_vec3_t){nx * radius, ny * radius, nz * radius};
            vi ++;
        }
    }

    int32_t ii = 0;
    for (int32_t y = 0; y < rings; y ++) {
        for (int32_t x = 0; x < cols; x ++) {
            uint16_t a = (uint16_t)(y * (cols + 1) + x);
            uint16_t b = (uint16_t)(a + cols + 1);
            uint16_t c = (uint16_t)(b + 1);
            uint16_t d = (uint16_t)(a + 1);

            idx[ii ++] = a;
            idx[ii ++] = b;
            idx[ii ++] = d;

            idx[ii ++] = b;
            idx[ii ++] = c;
            idx[ii ++] = d;
        }
    }
}

static ecs_entity_t flecsGeometry3_getSphereAsset(
    ecs_world_t *world,
    int32_t segments)
{
    int32_t normalized_segments = flecsGeometry3_sphereSegmentsNormalize(segments);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsGeometry3_findSphereAsset(ctx, normalized_segments);
    if (asset) {
        return asset;
    }

    char asset_name[32];
    snprintf(asset_name, sizeof(asset_name), "Sphere.%d", normalized_segments);
    asset = flecsGeometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsGeometry3_generateSphereMesh(mesh, normalized_segments);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->sphere_cache,
        (ecs_map_key_t)normalized_segments,
        (ecs_map_val_t)asset);

    return asset;
}

void FlecsSphere_on_replace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const FlecsSphere *old = ecs_field(it, FlecsSphere, 0);
    const FlecsSphere *new = ecs_field(it, FlecsSphere, 1);
    const FlecsGeometry3Cache *ctx = ecs_singleton_get(world, FlecsGeometry3Cache);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t old_segments = flecsGeometry3_sphereSegmentsNormalize(old[i].segments);
        int32_t new_segments = flecsGeometry3_sphereSegmentsNormalize(new[i].segments);
        if (old_segments == new_segments) {
            continue;
        }

        ecs_entity_t old_asset = flecsGeometry3_findSphereAsset(ctx, old_segments);
        if (old_asset) {
            ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
        }

        ecs_entity_t asset = flecsGeometry3_getSphereAsset(world, new_segments);
        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
