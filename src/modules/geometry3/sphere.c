#include "geometry3.h"
#include <math.h>
#include <stdio.h>

#define FLECS_GEOMETRY3_SPHERE_SEGMENTS_MIN (3)
#define FLECS_GEOMETRY3_SPHERE_SEGMENTS_MAX_SMOOTH (254)
#define FLECS_GEOMETRY3_SPHERE_SEGMENTS_MAX_FLAT (104)
#define FLECS_GEOMETRY3_SPHERE_CACHE_SEGMENTS_MASK (0x7fffffffULL)
#define FLECS_GEOMETRY3_SPHERE_CACHE_SMOOTH_MASK (1ULL << 63)

static int32_t flecsGeometry3_sphereSegmentsNormalize(
    int32_t segments,
    bool smooth)
{
    int32_t max_segments = FLECS_GEOMETRY3_SPHERE_SEGMENTS_MAX_FLAT;
    if (smooth) {
        max_segments = FLECS_GEOMETRY3_SPHERE_SEGMENTS_MAX_SMOOTH;
    }

    if (segments < FLECS_GEOMETRY3_SPHERE_SEGMENTS_MIN) {
        return FLECS_GEOMETRY3_SPHERE_SEGMENTS_MIN;
    }
    if (segments > max_segments) {
        return max_segments;
    }
    return segments;
}

static uint32_t flecsGeometry3_sphereRadiusBits(
    float radius)
{
    union {
        float f;
        uint32_t u;
    } radius_bits = { .f = radius };

    return radius_bits.u;
}

static uint64_t flecsGeometry3_sphereCacheKey(
    int32_t segments,
    bool smooth,
    float radius)
{
    uint64_t key =
        (((uint64_t)segments & FLECS_GEOMETRY3_SPHERE_CACHE_SEGMENTS_MASK) << 32) |
        (uint64_t)flecsGeometry3_sphereRadiusBits(radius);

    if (smooth) {
        key |= FLECS_GEOMETRY3_SPHERE_CACHE_SMOOTH_MASK;
    }

    return key;
}

static ecs_entity_t flecsGeometry3_findSphereAsset(
    const FlecsGeometry3Cache *ctx,
    uint64_t key)
{
    ecs_map_val_t *entry = ecs_map_get(
        &ctx->sphere_cache, (ecs_map_key_t)key);
    if (!entry) {
        return 0;
    }

    return (ecs_entity_t)entry[0];
}

static flecs_vec3_t flecsGeometry3_sphereUnitPoint(
    float theta,
    float phi)
{
    float sin_theta = sinf(theta);
    float cos_theta = cosf(theta);
    float sin_phi = sinf(phi);
    float cos_phi = cosf(phi);

    return (flecs_vec3_t){
        sin_theta * cos_phi,
        cos_theta,
        sin_theta * sin_phi
    };
}

static flecs_vec3_t flecsGeometry3_spherePoint(
    float theta,
    float phi,
    float radius)
{
    flecs_vec3_t p = flecsGeometry3_sphereUnitPoint(theta, phi);
    return (flecs_vec3_t){p.x * radius, p.y * radius, p.z * radius};
}

static flecs_vec3_t flecsGeometry3_vec3_sub(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return (flecs_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static flecs_vec3_t flecsGeometry3_vec3_cross(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return (flecs_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static float flecsGeometry3_vec3_dot(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static flecs_vec3_t flecsGeometry3_vec3_normalize(
    flecs_vec3_t v,
    flecs_vec3_t fallback)
{
    float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq <= 1e-20f) {
        return fallback;
    }

    float inv_len = 1.0f / sqrtf(len_sq);
    return (flecs_vec3_t){v.x * inv_len, v.y * inv_len, v.z * inv_len};
}

static flecs_vec3_t flecsGeometry3_triangleNormal(
    flecs_vec3_t a,
    flecs_vec3_t b,
    flecs_vec3_t c)
{
    flecs_vec3_t centroid = {
        (a.x + b.x + c.x) / 3.0f,
        (a.y + b.y + c.y) / 3.0f,
        (a.z + b.z + c.z) / 3.0f
    };
    flecs_vec3_t fallback = flecsGeometry3_vec3_normalize(
        centroid, (flecs_vec3_t){0.0f, 1.0f, 0.0f});

    flecs_vec3_t edge_ab = flecsGeometry3_vec3_sub(b, a);
    flecs_vec3_t edge_ac = flecsGeometry3_vec3_sub(c, a);
    flecs_vec3_t normal = flecsGeometry3_vec3_normalize(
        flecsGeometry3_vec3_cross(edge_ab, edge_ac), fallback);

    /* Ensure flat-shaded sphere normals always point outwards. */
    if (flecsGeometry3_vec3_dot(normal, centroid) < 0.0f) {
        normal = (flecs_vec3_t){-normal.x, -normal.y, -normal.z};
    }

    return normal;
}

static void flecsGeometry3_generateSmoothSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    float radius)
{
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = (rings + 1) * (cols + 1);
    const int32_t index_count = rings * cols * 6;

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

        for (int32_t x = 0; x <= cols; x ++) {
            float u = (float)x / (float)cols;
            float phi = u * 2.0f * (float)M_PI;
            vn[vi] = flecsGeometry3_sphereUnitPoint(theta, phi);
            v[vi] = (flecs_vec3_t){
                vn[vi].x * radius,
                vn[vi].y * radius,
                vn[vi].z * radius
            };
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

static void flecsGeometry3_generateFlatSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    float radius)
{
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = rings * cols * 6;
    const int32_t index_count = vert_count;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    int32_t vi = 0;
    int32_t ii = 0;
    for (int32_t y = 0; y < rings; y ++) {
        float v0_t = (float)y / (float)rings;
        float v1_t = (float)(y + 1) / (float)rings;
        float theta0 = v0_t * (float)M_PI;
        float theta1 = v1_t * (float)M_PI;

        for (int32_t x = 0; x < cols; x ++) {
            float u0 = (float)x / (float)cols;
            float u1 = (float)(x + 1) / (float)cols;
            float phi0 = u0 * 2.0f * (float)M_PI;
            float phi1 = u1 * 2.0f * (float)M_PI;

            flecs_vec3_t a = flecsGeometry3_spherePoint(theta0, phi0, radius);
            flecs_vec3_t b = flecsGeometry3_spherePoint(theta1, phi0, radius);
            flecs_vec3_t c = flecsGeometry3_spherePoint(theta1, phi1, radius);
            flecs_vec3_t d = flecsGeometry3_spherePoint(theta0, phi1, radius);

            flecs_vec3_t n0 = flecsGeometry3_triangleNormal(a, b, d);
            flecs_vec3_t n1 = flecsGeometry3_triangleNormal(b, c, d);

            v[vi] = a;
            vn[vi] = n0;
            idx[ii] = (uint16_t)vi;
            vi ++;
            ii ++;

            v[vi] = b;
            vn[vi] = n0;
            idx[ii] = (uint16_t)vi;
            vi ++;
            ii ++;

            v[vi] = d;
            vn[vi] = n0;
            idx[ii] = (uint16_t)vi;
            vi ++;
            ii ++;

            v[vi] = b;
            vn[vi] = n1;
            idx[ii] = (uint16_t)vi;
            vi ++;
            ii ++;

            v[vi] = c;
            vn[vi] = n1;
            idx[ii] = (uint16_t)vi;
            vi ++;
            ii ++;

            v[vi] = d;
            vn[vi] = n1;
            idx[ii] = (uint16_t)vi;
            vi ++;
            ii ++;
        }
    }
}

static void flecsGeometry3_generateSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    bool smooth,
    float radius)
{
    if (smooth) {
        flecsGeometry3_generateSmoothSphereMesh(mesh, segments, radius);
    } else {
        flecsGeometry3_generateFlatSphereMesh(mesh, segments, radius);
    }
}

static ecs_entity_t flecsGeometry3_getSphereAsset(
    ecs_world_t *world,
    int32_t segments,
    bool smooth,
    float radius)
{
    int32_t normalized_segments = flecsGeometry3_sphereSegmentsNormalize(
        segments, smooth);
    uint64_t key = flecsGeometry3_sphereCacheKey(
        normalized_segments, smooth, radius);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsGeometry3_findSphereAsset(ctx, key);
    if (asset) {
        return asset;
    }

    char asset_name[64];
    snprintf(
        asset_name,
        sizeof(asset_name),
        "Sphere.sphere%llu", key);

    asset = flecsGeometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsGeometry3_generateSphereMesh(mesh, normalized_segments, smooth, radius);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->sphere_cache,
        (ecs_map_key_t)key,
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
        int32_t old_segments = flecsGeometry3_sphereSegmentsNormalize(
            old[i].segments, old[i].smooth);
        int32_t new_segments = flecsGeometry3_sphereSegmentsNormalize(
            new[i].segments, new[i].smooth);
        uint64_t old_key = flecsGeometry3_sphereCacheKey(
            old_segments, old[i].smooth, old[i].radius);
        uint64_t new_key = flecsGeometry3_sphereCacheKey(
            new_segments, new[i].smooth, new[i].radius);

        if (old_key == new_key) {
            continue;
        }

        ecs_entity_t old_asset = flecsGeometry3_findSphereAsset(ctx, old_key);
        if (old_asset) {
            ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
        }

        ecs_entity_t asset = flecsGeometry3_getSphereAsset(
            world, new[i].segments, new[i].smooth, new[i].radius);
        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
