#include "geometry3.h"
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FLECS_GEOMETRY3_NGON_SIDES_MIN (3)
#define FLECS_GEOMETRY3_NGON_SIDES_MAX (65534)
#define FLECS_GEOMETRY3_NGON_CACHE_SIDES_MASK (0xffffffffULL)

static int32_t flecsGeometry3_ngonSidesNormalize(
    int32_t sides)
{
    if (sides < FLECS_GEOMETRY3_NGON_SIDES_MIN) {
        return FLECS_GEOMETRY3_NGON_SIDES_MIN;
    }
    if (sides > FLECS_GEOMETRY3_NGON_SIDES_MAX) {
        return FLECS_GEOMETRY3_NGON_SIDES_MAX;
    }
    return sides;
}

static uint64_t flecsGeometry3_ngonCacheKey(
    int32_t sides)
{
    return (uint64_t)sides & FLECS_GEOMETRY3_NGON_CACHE_SIDES_MASK;
}

static ecs_entity_t flecsGeometry3_findNGonAsset(
    const FlecsGeometry3Cache *ctx,
    uint64_t key)
{
    ecs_map_val_t *entry = ecs_map_get(
        &ctx->ngon_cache, (ecs_map_key_t)key);
    if (!entry) {
        return 0;
    }

    return (ecs_entity_t)entry[0];
}

static void flecsGeometry3_generateNGonMesh(
    FlecsMesh3 *mesh,
    int32_t sides)
{
    const float radius = 0.5f;
    const int32_t vert_count = sides + 1;
    const int32_t index_count = sides * 3;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    v[0] = (flecs_vec3_t){0.0f, 0.0f, 0.0f};
    vn[0] = (flecs_vec3_t){0.0f, 0.0f, 1.0f};

    for (int32_t i = 0; i < sides; i ++) {
        float t = (float)i / (float)sides;
        float angle = t * 2.0f * (float)M_PI;
        v[i + 1] = (flecs_vec3_t){
            cosf(angle) * radius,
            sinf(angle) * radius,
            0.0f
        };
        vn[i + 1] = (flecs_vec3_t){0.0f, 0.0f, 1.0f};
    }

    int32_t ii = 0;
    for (int32_t i = 0; i < sides; i ++) {
        uint16_t current = (uint16_t)(i + 1);
        uint16_t next = (uint16_t)(((i + 1) % sides) + 1);

        idx[ii ++] = 0;
        idx[ii ++] = next;
        idx[ii ++] = current;
    }
}

static ecs_entity_t flecsGeometry3_getNGonAsset(
    ecs_world_t *world,
    int32_t sides)
{
    int32_t normalized_sides = flecsGeometry3_ngonSidesNormalize(sides);
    uint64_t key = flecsGeometry3_ngonCacheKey(normalized_sides);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsGeometry3_findNGonAsset(ctx, key);
    if (asset) {
        return asset;
    }

    char asset_name[64];
    snprintf(
        asset_name,
        sizeof(asset_name),
        "NGon.ngon%llu", key);

    asset = flecsGeometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsGeometry3_generateNGonMesh(mesh, normalized_sides);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->ngon_cache,
        (ecs_map_key_t)key,
        (ecs_map_val_t)asset);

    return asset;
}

void FlecsNGon_on_replace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const FlecsNGon *old = ecs_field(it, FlecsNGon, 0);
    const FlecsNGon *new = ecs_field(it, FlecsNGon, 1);
    const FlecsGeometry3Cache *ctx = ecs_singleton_get(world, FlecsGeometry3Cache);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t old_sides = flecsGeometry3_ngonSidesNormalize(old[i].sides);
        int32_t new_sides = flecsGeometry3_ngonSidesNormalize(new[i].sides);
        uint64_t old_key = flecsGeometry3_ngonCacheKey(old_sides);
        uint64_t new_key = flecsGeometry3_ngonCacheKey(new_sides);

        if (old_key != new_key) {
            ecs_entity_t old_asset = flecsGeometry3_findNGonAsset(ctx, old_key);
            if (old_asset) {
                ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
            }
        }

        ecs_entity_t asset = flecsGeometry3_getNGonAsset(world, new[i].sides);
        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
