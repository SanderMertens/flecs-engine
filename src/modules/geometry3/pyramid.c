#include "geometry3.h"
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FLECS_GEOMETRY3_PYRAMID_SIDES_MIN (3)
#define FLECS_GEOMETRY3_PYRAMID_SIDES_MAX_SMOOTH (32766)
#define FLECS_GEOMETRY3_PYRAMID_SIDES_MAX_FLAT (16383)
#define FLECS_GEOMETRY3_PYRAMID_CACHE_SIDES_MASK (0x7fffffffULL)
#define FLECS_GEOMETRY3_PYRAMID_CACHE_SMOOTH_MASK (1ULL << 63)

static int32_t flecsGeometry3_pyramidSidesNormalize(
    int32_t sides,
    bool smooth)
{
    int32_t max_sides = FLECS_GEOMETRY3_PYRAMID_SIDES_MAX_FLAT;
    if (smooth) {
        max_sides = FLECS_GEOMETRY3_PYRAMID_SIDES_MAX_SMOOTH;
    }

    if (sides < FLECS_GEOMETRY3_PYRAMID_SIDES_MIN) {
        return FLECS_GEOMETRY3_PYRAMID_SIDES_MIN;
    }
    if (sides > max_sides) {
        return max_sides;
    }
    return sides;
}

static uint32_t flecsGeometry3_pyramidLengthBits(
    float length)
{
    union {
        float f;
        uint32_t u;
    } length_bits = { .f = length };

    return length_bits.u;
}

static uint64_t flecsGeometry3_pyramidCacheKey(
    int32_t sides,
    bool smooth,
    float length)
{
    uint64_t key =
        (((uint64_t)sides & FLECS_GEOMETRY3_PYRAMID_CACHE_SIDES_MASK) << 32) |
        (uint64_t)flecsGeometry3_pyramidLengthBits(length);

    if (smooth) {
        key |= FLECS_GEOMETRY3_PYRAMID_CACHE_SMOOTH_MASK;
    }

    return key;
}

static ecs_entity_t flecsGeometry3_findPyramidAsset(
    const FlecsGeometry3Cache *ctx,
    uint64_t key)
{
    ecs_map_val_t *entry = ecs_map_get(
        &ctx->pyramid_cache, (ecs_map_key_t)key);
    if (!entry) {
        return 0;
    }

    return (ecs_entity_t)entry[0];
}

static void flecsGeometry3_generateFlatPyramidMesh(
    FlecsMesh3 *mesh,
    int32_t sides,
    float length)
{
    const float half_length = length * 0.5f;
    const float y_top = half_length;
    const float y_bottom = -half_length;
    const float angle_offset = (float)M_PI / (float)sides;
    /* Keep pyramid base footprint constant across side counts. */
    const float radius = 0.5f;

    const int32_t base_center = 0;
    const int32_t base_ring_start = 1;
    const int32_t side_start = base_ring_start + sides;
    const int32_t vert_count = 1 + sides + (sides * 3);
    const int32_t index_count = sides * 6;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    v[base_center] = (flecs_vec3_t){0.0f, y_bottom, 0.0f};
    vn[base_center] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};

    for (int32_t i = 0; i < sides; i ++) {
        float t = (float)i / (float)sides;
        float angle = angle_offset + (t * 2.0f * (float)M_PI);
        float x = cosf(angle) * radius;
        float z = sinf(angle) * radius;

        int32_t vi = base_ring_start + i;
        v[vi] = (flecs_vec3_t){x, y_bottom, z};
        vn[vi] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};
    }

    flecs_vec3_t apex = {0.0f, y_top, 0.0f};
    int32_t vi = side_start;
    for (int32_t i = 0; i < sides; i ++) {
        int32_t current = base_ring_start + i;
        int32_t next = base_ring_start + ((i + 1) % sides);

        flecs_vec3_t b0 = v[current];
        flecs_vec3_t b1 = v[next];

        float edge0x = apex.x - b0.x;
        float edge0y = apex.y - b0.y;
        float edge0z = apex.z - b0.z;

        float edge1x = b1.x - b0.x;
        float edge1y = b1.y - b0.y;
        float edge1z = b1.z - b0.z;

        float nx = edge0y * edge1z - edge0z * edge1y;
        float ny = edge0z * edge1x - edge0x * edge1z;
        float nz = edge0x * edge1y - edge0y * edge1x;
        float normal_len = sqrtf(nx * nx + ny * ny + nz * nz);

        flecs_vec3_t normal;
        if (normal_len > 0.0f) {
            float inv_len = 1.0f / normal_len;
            normal = (flecs_vec3_t){nx * inv_len, ny * inv_len, nz * inv_len};
        } else {
            normal = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
        }

        v[vi] = b0;
        vn[vi] = normal;
        vi ++;

        v[vi] = apex;
        vn[vi] = normal;
        vi ++;

        v[vi] = b1;
        vn[vi] = normal;
        vi ++;
    }

    int32_t ii = 0;

    for (int32_t i = 0; i < sides; i ++) {
        uint16_t current = (uint16_t)(base_ring_start + i);
        uint16_t next = (uint16_t)(base_ring_start + ((i + 1) % sides));

        idx[ii ++] = (uint16_t)base_center;
        idx[ii ++] = next;
        idx[ii ++] = current;
    }

    for (int32_t i = 0; i < sides; i ++) {
        uint16_t face = (uint16_t)(side_start + (i * 3));
        idx[ii ++] = face;
        idx[ii ++] = (uint16_t)(face + 2);
        idx[ii ++] = (uint16_t)(face + 1);
    }
}

static void flecsGeometry3_generateSmoothPyramidMesh(
    FlecsMesh3 *mesh,
    int32_t sides,
    float length)
{
    const float half_length = length * 0.5f;
    const float y_top = half_length;
    const float y_bottom = -half_length;
    const float angle_offset = (float)M_PI / (float)sides;
    /* Keep pyramid base footprint constant across side counts. */
    const float radius = 0.5f;

    const int32_t base_center = 0;
    const int32_t base_ring_start = 1;
    const int32_t side_apex = base_ring_start + sides;
    const int32_t side_ring_start = side_apex + 1;
    const int32_t vert_count = 2 + (sides * 2);
    const int32_t index_count = sides * 6;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    flecs_vec3_t *side_normals = ecs_os_calloc_n(flecs_vec3_t, sides);

    v[base_center] = (flecs_vec3_t){0.0f, y_bottom, 0.0f};
    vn[base_center] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};

    for (int32_t i = 0; i < sides; i ++) {
        float t = (float)i / (float)sides;
        float angle = angle_offset + (t * 2.0f * (float)M_PI);
        float x = cosf(angle) * radius;
        float z = sinf(angle) * radius;

        int32_t vi = base_ring_start + i;
        v[vi] = (flecs_vec3_t){x, y_bottom, z};
        vn[vi] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};
    }

    v[side_apex] = (flecs_vec3_t){0.0f, y_top, 0.0f};

    for (int32_t i = 0; i < sides; i ++) {
        int32_t current = base_ring_start + i;
        int32_t next = base_ring_start + ((i + 1) % sides);

        flecs_vec3_t b0 = v[current];
        flecs_vec3_t b1 = v[next];

        float edge0x = v[side_apex].x - b0.x;
        float edge0y = v[side_apex].y - b0.y;
        float edge0z = v[side_apex].z - b0.z;

        float edge1x = b1.x - b0.x;
        float edge1y = b1.y - b0.y;
        float edge1z = b1.z - b0.z;

        float nx = edge0y * edge1z - edge0z * edge1y;
        float ny = edge0z * edge1x - edge0x * edge1z;
        float nz = edge0x * edge1y - edge0y * edge1x;
        float normal_len = sqrtf(nx * nx + ny * ny + nz * nz);

        if (normal_len > 0.0f) {
            float inv_len = 1.0f / normal_len;
            side_normals[i] = (flecs_vec3_t){nx * inv_len, ny * inv_len, nz * inv_len};
        } else {
            side_normals[i] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
        }
    }

    flecs_vec3_t apex_normal = {0.0f, 0.0f, 0.0f};
    for (int32_t i = 0; i < sides; i ++) {
        apex_normal.x += side_normals[i].x;
        apex_normal.y += side_normals[i].y;
        apex_normal.z += side_normals[i].z;
    }

    float apex_len = sqrtf(
        apex_normal.x * apex_normal.x +
        apex_normal.y * apex_normal.y +
        apex_normal.z * apex_normal.z);
    if (apex_len > 0.0f) {
        float inv_len = 1.0f / apex_len;
        vn[side_apex] = (flecs_vec3_t){
            apex_normal.x * inv_len,
            apex_normal.y * inv_len,
            apex_normal.z * inv_len
        };
    } else {
        vn[side_apex] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
    }

    for (int32_t i = 0; i < sides; i ++) {
        int32_t prev = (i + sides - 1) % sides;
        flecs_vec3_t n = {
            side_normals[prev].x + side_normals[i].x,
            side_normals[prev].y + side_normals[i].y,
            side_normals[prev].z + side_normals[i].z
        };

        float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0f) {
            float inv_len = 1.0f / len;
            n.x *= inv_len;
            n.y *= inv_len;
            n.z *= inv_len;
        } else {
            n = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
        }

        int32_t vi = side_ring_start + i;
        v[vi] = v[base_ring_start + i];
        vn[vi] = n;
    }

    ecs_os_free(side_normals);

    int32_t ii = 0;

    for (int32_t i = 0; i < sides; i ++) {
        uint16_t current = (uint16_t)(base_ring_start + i);
        uint16_t next = (uint16_t)(base_ring_start + ((i + 1) % sides));

        idx[ii ++] = (uint16_t)base_center;
        idx[ii ++] = next;
        idx[ii ++] = current;
    }

    for (int32_t i = 0; i < sides; i ++) {
        uint16_t current = (uint16_t)(side_ring_start + i);
        uint16_t next = (uint16_t)(side_ring_start + ((i + 1) % sides));

        idx[ii ++] = current;
        idx[ii ++] = next;
        idx[ii ++] = (uint16_t)side_apex;
    }
}

static void flecsGeometry3_generatePyramidMesh(
    FlecsMesh3 *mesh,
    int32_t sides,
    bool smooth,
    float length)
{
    if (smooth) {
        flecsGeometry3_generateSmoothPyramidMesh(mesh, sides, length);
    } else {
        flecsGeometry3_generateFlatPyramidMesh(mesh, sides, length);
    }
}

static ecs_entity_t flecsGeometry3_getPyramidEntity(
    ecs_world_t *world,
    int32_t sides,
    bool smooth,
    float length)
{
    int32_t normalized_sides = flecsGeometry3_pyramidSidesNormalize(
        sides, smooth);
    uint64_t key = flecsGeometry3_pyramidCacheKey(
        normalized_sides, smooth, length);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsGeometry3_findPyramidAsset(ctx, key);
    if (asset) {
        return asset;
    }

    char asset_name[64];
    snprintf(
        asset_name,
        sizeof(asset_name),
        "Pyramid.pyramid%llu", key);

    asset = flecsGeometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsGeometry3_generatePyramidMesh(
        mesh, normalized_sides, smooth, length);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->pyramid_cache,
        (ecs_map_key_t)key,
        (ecs_map_val_t)asset);

    return asset;
}

const FlecsMesh3Impl* flecsGeometry3_getPyramidAsset(
    ecs_world_t *world)
{
    ecs_entity_t asset = flecsGeometry3_getPyramidEntity(world, 4, false, 1.0f);
    return ecs_get(world, asset, FlecsMesh3Impl);
}

void FlecsPyramid_on_replace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const FlecsPyramid *old = ecs_field(it, FlecsPyramid, 0);
    const FlecsPyramid *new = ecs_field(it, FlecsPyramid, 1);
    const FlecsGeometry3Cache *ctx = ecs_singleton_get(world, FlecsGeometry3Cache);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t old_sides = flecsGeometry3_pyramidSidesNormalize(
            old[i].sides, old[i].smooth);
        int32_t new_sides = flecsGeometry3_pyramidSidesNormalize(
            new[i].sides, new[i].smooth);
        uint64_t old_key = flecsGeometry3_pyramidCacheKey(
            old_sides, old[i].smooth, old[i].length);
        uint64_t new_key = flecsGeometry3_pyramidCacheKey(
            new_sides, new[i].smooth, new[i].length);

        if (old_key != new_key) {
            ecs_entity_t old_asset = flecsGeometry3_findPyramidAsset(ctx, old_key);
            if (old_asset) {
                ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
            }
        }

        ecs_entity_t asset = flecsGeometry3_getPyramidEntity(
            world, new[i].sides, new[i].smooth, new[i].length);
        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
