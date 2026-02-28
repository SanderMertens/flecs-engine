#include "geometry3.h"
#include <math.h>
#include <stdio.h>

#define FLECS_GEOMETRY3_CYLINDER_SEGMENTS_MIN (3)
#define FLECS_GEOMETRY3_CYLINDER_SEGMENTS_MAX_SMOOTH (16382)
#define FLECS_GEOMETRY3_CYLINDER_SEGMENTS_MAX_FLAT (10922)
#define FLECS_GEOMETRY3_CYLINDER_CACHE_SEGMENTS_MASK (0x7fffffffULL)
#define FLECS_GEOMETRY3_CYLINDER_CACHE_SMOOTH_MASK (1ULL << 63)

static int32_t flecsGeometry3_cylinderSegmentsNormalize(
    int32_t segments,
    bool smooth)
{
    int32_t max_segments = FLECS_GEOMETRY3_CYLINDER_SEGMENTS_MAX_FLAT;
    if (smooth) {
        max_segments = FLECS_GEOMETRY3_CYLINDER_SEGMENTS_MAX_SMOOTH;
    }

    if (segments < FLECS_GEOMETRY3_CYLINDER_SEGMENTS_MIN) {
        return FLECS_GEOMETRY3_CYLINDER_SEGMENTS_MIN;
    }
    if (segments > max_segments) {
        return max_segments;
    }
    return segments;
}

static uint32_t flecsGeometry3_cylinderLengthBits(
    float length)
{
    union {
        float f;
        uint32_t u;
    } length_bits = { .f = length };

    return length_bits.u;
}

static uint64_t flecsGeometry3_cylinderCacheKey(
    int32_t segments,
    bool smooth,
    float length)
{
    uint64_t key =
        (((uint64_t)segments & FLECS_GEOMETRY3_CYLINDER_CACHE_SEGMENTS_MASK) << 32) |
        (uint64_t)flecsGeometry3_cylinderLengthBits(length);

    if (smooth) {
        key |= FLECS_GEOMETRY3_CYLINDER_CACHE_SMOOTH_MASK;
    }

    return key;
}

static ecs_entity_t flecsGeometry3_findCylinderAsset(
    const FlecsGeometry3Cache *ctx,
    uint64_t key)
{
    ecs_map_val_t *entry = ecs_map_get(
        &ctx->cylinder_cache, (ecs_map_key_t)key);
    if (!entry) {
        return 0;
    }

    return (ecs_entity_t)entry[0];
}

static void flecsGeometry3_generateSmoothCylinderMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    float length)
{
    const float radius = 0.5f;
    const float half_length = length * 0.5f;
    const float y_top = half_length;
    const float y_bottom = -half_length;

    const int32_t side_vert_count = (segments + 1) * 2;
    const int32_t cap_vert_count = segments + 2;
    const int32_t vert_count = side_vert_count + cap_vert_count + cap_vert_count;
    const int32_t index_count = (segments * 6) + (segments * 3) + (segments * 3);

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    int32_t vi = 0;

    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = t * 2.0f * (float)M_PI;
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){ca * radius, y_top, sa * radius};
        vn[vi] = (flecs_vec3_t){ca, 0.0f, sa};
        vi ++;

        v[vi] = (flecs_vec3_t){ca * radius, y_bottom, sa * radius};
        vn[vi] = (flecs_vec3_t){ca, 0.0f, sa};
        vi ++;
    }

    int32_t top_center = vi;
    v[vi] = (flecs_vec3_t){0.0f, y_top, 0.0f};
    vn[vi] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
    vi ++;

    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = t * 2.0f * (float)M_PI;
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){ca * radius, y_top, sa * radius};
        vn[vi] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
        vi ++;
    }

    int32_t bottom_center = vi;
    v[vi] = (flecs_vec3_t){0.0f, y_bottom, 0.0f};
    vn[vi] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};
    vi ++;

    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = t * 2.0f * (float)M_PI;
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){ca * radius, y_bottom, sa * radius};
        vn[vi] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};
        vi ++;
    }

    int32_t ii = 0;

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t a = (uint16_t)(s * 2);
        uint16_t b = (uint16_t)(a + 1);
        uint16_t c = (uint16_t)(a + 2);
        uint16_t d = (uint16_t)(a + 3);

        idx[ii ++] = a;
        idx[ii ++] = b;
        idx[ii ++] = c;

        idx[ii ++] = b;
        idx[ii ++] = d;
        idx[ii ++] = c;
    }

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t a = (uint16_t)(top_center + 1 + s);
        uint16_t b = (uint16_t)(top_center + 1 + s + 1);

        idx[ii ++] = (uint16_t)top_center;
        idx[ii ++] = a;
        idx[ii ++] = b;
    }

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t a = (uint16_t)(bottom_center + 1 + s);
        uint16_t b = (uint16_t)(bottom_center + 1 + s + 1);

        idx[ii ++] = (uint16_t)bottom_center;
        idx[ii ++] = b;
        idx[ii ++] = a;
    }
}

static void flecsGeometry3_generateFlatCylinderMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    float length)
{
    const float radius = 0.5f;
    const float half_length = length * 0.5f;
    const float y_top = half_length;
    const float y_bottom = -half_length;

    const int32_t side_vert_count = segments * 4;
    const int32_t cap_vert_count = segments + 2;
    const int32_t vert_count = side_vert_count + cap_vert_count + cap_vert_count;
    const int32_t index_count = (segments * 6) + (segments * 3) + (segments * 3);

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    int32_t vi = 0;

    for (int32_t s = 0; s < segments; s ++) {
        float t0 = (float)s / (float)segments;
        float t1 = (float)(s + 1) / (float)segments;
        float a0 = t0 * 2.0f * (float)M_PI;
        float a1 = t1 * 2.0f * (float)M_PI;

        float ca0 = cosf(a0);
        float sa0 = sinf(a0);
        float ca1 = cosf(a1);
        float sa1 = sinf(a1);

        float am = (a0 + a1) * 0.5f;
        flecs_vec3_t side_normal = {cosf(am), 0.0f, sinf(am)};

        v[vi] = (flecs_vec3_t){ca0 * radius, y_top, sa0 * radius};
        vn[vi] = side_normal;
        vi ++;

        v[vi] = (flecs_vec3_t){ca0 * radius, y_bottom, sa0 * radius};
        vn[vi] = side_normal;
        vi ++;

        v[vi] = (flecs_vec3_t){ca1 * radius, y_top, sa1 * radius};
        vn[vi] = side_normal;
        vi ++;

        v[vi] = (flecs_vec3_t){ca1 * radius, y_bottom, sa1 * radius};
        vn[vi] = side_normal;
        vi ++;
    }

    int32_t top_center = vi;
    v[vi] = (flecs_vec3_t){0.0f, y_top, 0.0f};
    vn[vi] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
    vi ++;

    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = t * 2.0f * (float)M_PI;
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){ca * radius, y_top, sa * radius};
        vn[vi] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
        vi ++;
    }

    int32_t bottom_center = vi;
    v[vi] = (flecs_vec3_t){0.0f, y_bottom, 0.0f};
    vn[vi] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};
    vi ++;

    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = t * 2.0f * (float)M_PI;
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){ca * radius, y_bottom, sa * radius};
        vn[vi] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};
        vi ++;
    }

    int32_t ii = 0;

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t base = (uint16_t)(s * 4);
        uint16_t a = base;
        uint16_t b = (uint16_t)(base + 1);
        uint16_t c = (uint16_t)(base + 2);
        uint16_t d = (uint16_t)(base + 3);

        idx[ii ++] = a;
        idx[ii ++] = b;
        idx[ii ++] = c;

        idx[ii ++] = b;
        idx[ii ++] = d;
        idx[ii ++] = c;
    }

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t a = (uint16_t)(top_center + 1 + s);
        uint16_t b = (uint16_t)(top_center + 1 + s + 1);

        idx[ii ++] = (uint16_t)top_center;
        idx[ii ++] = a;
        idx[ii ++] = b;
    }

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t a = (uint16_t)(bottom_center + 1 + s);
        uint16_t b = (uint16_t)(bottom_center + 1 + s + 1);

        idx[ii ++] = (uint16_t)bottom_center;
        idx[ii ++] = b;
        idx[ii ++] = a;
    }
}

static void flecsGeometry3_generateCylinderMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    bool smooth,
    float length)
{
    if (smooth) {
        flecsGeometry3_generateSmoothCylinderMesh(mesh, segments, length);
    } else {
        flecsGeometry3_generateFlatCylinderMesh(mesh, segments, length);
    }
}

static ecs_entity_t flecsGeometry3_getCylinderAsset(
    ecs_world_t *world,
    int32_t segments,
    bool smooth,
    float length)
{
    int32_t normalized_segments = flecsGeometry3_cylinderSegmentsNormalize(
        segments, smooth);
    uint64_t key = flecsGeometry3_cylinderCacheKey(
        normalized_segments, smooth, length);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsGeometry3_findCylinderAsset(ctx, key);
    if (asset) {
        return asset;
    }

    char asset_name[64];
    snprintf(
        asset_name,
        sizeof(asset_name),
        "Cylinder.cylinder%llu", key);

    asset = flecsGeometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsGeometry3_generateCylinderMesh(
        mesh, normalized_segments, smooth, length);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->cylinder_cache,
        (ecs_map_key_t)key,
        (ecs_map_val_t)asset);

    return asset;
}

void FlecsCylinder_on_replace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const FlecsCylinder *old = ecs_field(it, FlecsCylinder, 0);
    const FlecsCylinder *new = ecs_field(it, FlecsCylinder, 1);
    const FlecsGeometry3Cache *ctx = ecs_singleton_get(world, FlecsGeometry3Cache);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t old_segments = flecsGeometry3_cylinderSegmentsNormalize(
            old[i].segments, old[i].smooth);
        int32_t new_segments = flecsGeometry3_cylinderSegmentsNormalize(
            new[i].segments, new[i].smooth);
        uint64_t old_key = flecsGeometry3_cylinderCacheKey(
            old_segments, old[i].smooth, old[i].length);
        uint64_t new_key = flecsGeometry3_cylinderCacheKey(
            new_segments, new[i].smooth, new[i].length);

        if (old_key != new_key) {
            ecs_entity_t old_asset = flecsGeometry3_findCylinderAsset(ctx, old_key);
            if (old_asset) {
                ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
            }
        }

        ecs_entity_t asset = flecsGeometry3_getCylinderAsset(
            world, new[i].segments, new[i].smooth, new[i].length);
        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
