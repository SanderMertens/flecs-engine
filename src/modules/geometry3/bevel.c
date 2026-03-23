#include "geometry3.h"
#include <math.h>
#include <stdio.h>

#define FLECS_GEOMETRY3_BEVEL_SEGMENTS_MIN (1)
#define FLECS_GEOMETRY3_BEVEL_SEGMENTS_MAX_SMOOTH (32766)
#define FLECS_GEOMETRY3_BEVEL_SEGMENTS_MAX_FLAT (10922)
#define FLECS_GEOMETRY3_BEVEL_CACHE_SEGMENTS_MASK (0x7fffffffULL)
#define FLECS_GEOMETRY3_BEVEL_CACHE_SMOOTH_MASK (1ULL << 63)

static int32_t flecsEngine_bevel_segmentsNormalize(
    int32_t segments,
    bool smooth)
{
    int32_t max_segments = FLECS_GEOMETRY3_BEVEL_SEGMENTS_MAX_FLAT;
    if (smooth) {
        max_segments = FLECS_GEOMETRY3_BEVEL_SEGMENTS_MAX_SMOOTH;
    }

    if (segments < FLECS_GEOMETRY3_BEVEL_SEGMENTS_MIN) {
        return FLECS_GEOMETRY3_BEVEL_SEGMENTS_MIN;
    }
    if (segments > max_segments) {
        return max_segments;
    }
    return segments;
}

static uint64_t flecsEngine_bevel_cacheKey(
    int32_t segments,
    bool smooth)
{
    uint64_t key =
        ((uint64_t)segments & FLECS_GEOMETRY3_BEVEL_CACHE_SEGMENTS_MASK) << 32;

    if (smooth) {
        key |= FLECS_GEOMETRY3_BEVEL_CACHE_SMOOTH_MASK;
    }

    return key;
}

static ecs_entity_t flecsEngine_bevel_findAsset(
    const FlecsGeometry3Cache *ctx,
    uint64_t key)
{
    ecs_map_val_t *entry = ecs_map_get(
        &ctx->bevel_cache, (ecs_map_key_t)key);
    if (!entry) {
        return 0;
    }

    return (ecs_entity_t)entry[0];
}

static void flecsEngine_bevel_generateSmoothMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const float radius = 0.5f;
    const float half_length = 0.5f;
    const float start_angle = 0.0f;
    const float end_angle = (float)M_PI * 0.5f;
    const int32_t vert_count = (segments + 1) * 2;
    const int32_t index_count = segments * 6;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    int32_t vi = 0;
    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = start_angle + ((end_angle - start_angle) * t);
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){half_length, sa * radius, ca * radius};
        vn[vi] = (flecs_vec3_t){0.0f, sa, ca};
        vi ++;

        v[vi] = (flecs_vec3_t){-half_length, sa * radius, ca * radius};
        vn[vi] = (flecs_vec3_t){0.0f, sa, ca};
        vi ++;
    }

    int32_t ii = 0;
    for (int32_t s = 0; s < segments; s ++) {
        uint32_t a = (uint32_t)(s * 2);
        uint32_t b = (uint32_t)(a + 1);
        uint32_t c = (uint32_t)(a + 2);
        uint32_t d = (uint32_t)(a + 3);

        idx[ii ++] = a;
        idx[ii ++] = c;
        idx[ii ++] = b;

        idx[ii ++] = b;
        idx[ii ++] = c;
        idx[ii ++] = d;
    }
}

static void flecsEngine_bevel_generateFlatMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const float radius = 0.5f;
    const float half_length = 0.5f;
    const float start_angle = 0.0f;
    const float end_angle = (float)M_PI * 0.5f;
    const int32_t vert_count = segments * 4;
    const int32_t index_count = segments * 6;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    int32_t vi = 0;
    for (int32_t s = 0; s < segments; s ++) {
        float t0 = (float)s / (float)segments;
        float t1 = (float)(s + 1) / (float)segments;
        float a0 = start_angle + ((end_angle - start_angle) * t0);
        float a1 = start_angle + ((end_angle - start_angle) * t1);
        float ca0 = cosf(a0);
        float sa0 = sinf(a0);
        float ca1 = cosf(a1);
        float sa1 = sinf(a1);
        float am = (a0 + a1) * 0.5f;
        flecs_vec3_t side_normal = {0.0f, sinf(am), cosf(am)};

        v[vi] = (flecs_vec3_t){half_length, sa0 * radius, ca0 * radius};
        vn[vi] = side_normal;
        vi ++;

        v[vi] = (flecs_vec3_t){-half_length, sa0 * radius, ca0 * radius};
        vn[vi] = side_normal;
        vi ++;

        v[vi] = (flecs_vec3_t){half_length, sa1 * radius, ca1 * radius};
        vn[vi] = side_normal;
        vi ++;

        v[vi] = (flecs_vec3_t){-half_length, sa1 * radius, ca1 * radius};
        vn[vi] = side_normal;
        vi ++;
    }

    int32_t ii = 0;
    for (int32_t s = 0; s < segments; s ++) {
        uint32_t base = (uint32_t)(s * 4);
        uint32_t a = base;
        uint32_t b = (uint32_t)(base + 1);
        uint32_t c = (uint32_t)(base + 2);
        uint32_t d = (uint32_t)(base + 3);

        idx[ii ++] = a;
        idx[ii ++] = c;
        idx[ii ++] = b;

        idx[ii ++] = b;
        idx[ii ++] = c;
        idx[ii ++] = d;
    }
}

static void flecsEngine_bevel_generateMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    bool smooth)
{
    if (smooth) {
        flecsEngine_bevel_generateSmoothMesh(mesh, segments);
    } else {
        flecsEngine_bevel_generateFlatMesh(mesh, segments);
    }
}

static ecs_entity_t flecsEngine_bevel_getAsset(
    ecs_world_t *world,
    int32_t segments,
    bool smooth)
{
    int32_t normalized_segments = flecsEngine_bevel_segmentsNormalize(
        segments, smooth);
    uint64_t key = flecsEngine_bevel_cacheKey(
        normalized_segments, smooth);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsEngine_bevel_findAsset(ctx, key);
    if (asset) {
        return asset;
    }

    char asset_name[64];
    snprintf(
        asset_name,
        sizeof(asset_name),
        "Bevel.bevel%llu", key);

    asset = flecsEngine_geometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsEngine_bevel_generateMesh(mesh, normalized_segments, smooth);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->bevel_cache,
        (ecs_map_key_t)key,
        (ecs_map_val_t)asset);

    return asset;
}

const FlecsMesh3Impl* flecsEngine_bevel_getAssetImpl(
    ecs_world_t *world,
    int32_t segments,
    bool smooth)
{
    ecs_entity_t asset = flecsEngine_bevel_getAsset(world, segments, smooth);
    return ecs_get(world, asset, FlecsMesh3Impl);
}

void FlecsBevel_on_replace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const FlecsBevel *old = ecs_field(it, FlecsBevel, 0);
    const FlecsBevel *new = ecs_field(it, FlecsBevel, 1);
    const FlecsGeometry3Cache *ctx = ecs_singleton_get(world, FlecsGeometry3Cache);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t old_segments = flecsEngine_bevel_segmentsNormalize(
            old[i].segments, old[i].smooth);
        int32_t new_segments = flecsEngine_bevel_segmentsNormalize(
            new[i].segments, new[i].smooth);
        uint64_t old_key = flecsEngine_bevel_cacheKey(
            old_segments, old[i].smooth);
        uint64_t new_key = flecsEngine_bevel_cacheKey(
            new_segments, new[i].smooth);

        if (old_key != new_key) {
            ecs_entity_t old_asset = flecsEngine_bevel_findAsset(ctx, old_key);
            if (old_asset) {
                ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
            }
        }

        ecs_entity_t asset = flecsEngine_bevel_getAsset(
            world, new[i].segments, new[i].smooth);

        /* Don't add bevel mesh for entities that also have Box (beveled box) */
        if (ecs_has(world, it->entities[i], FlecsBox)) {
            continue;
        }

        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
