#include "geometry3.h"
#include <math.h>
#include <stdio.h>

#define FLECS_GEOMETRY3_BEVEL_CORNER_SEGMENTS_MIN (1)
#define FLECS_GEOMETRY3_BEVEL_CORNER_SEGMENTS_MAX_SMOOTH (254)
#define FLECS_GEOMETRY3_BEVEL_CORNER_SEGMENTS_MAX_FLAT (104)
#define FLECS_GEOMETRY3_BEVEL_CORNER_CACHE_SEGMENTS_MASK (0x7fffffffULL)
#define FLECS_GEOMETRY3_BEVEL_CORNER_CACHE_SMOOTH_MASK (1ULL << 63)

static int32_t flecsEngine_bevelCorner_segmentsNormalize(
    int32_t segments,
    bool smooth)
{
    int32_t max_segments = FLECS_GEOMETRY3_BEVEL_CORNER_SEGMENTS_MAX_FLAT;
    if (smooth) {
        max_segments = FLECS_GEOMETRY3_BEVEL_CORNER_SEGMENTS_MAX_SMOOTH;
    }

    if (segments < FLECS_GEOMETRY3_BEVEL_CORNER_SEGMENTS_MIN) {
        return FLECS_GEOMETRY3_BEVEL_CORNER_SEGMENTS_MIN;
    }
    if (segments > max_segments) {
        return max_segments;
    }
    return segments;
}

static uint64_t flecsEngine_bevelCorner_cacheKey(
    int32_t segments,
    bool smooth)
{
    uint64_t key =
        ((uint64_t)segments & FLECS_GEOMETRY3_BEVEL_CORNER_CACHE_SEGMENTS_MASK) << 32;

    if (smooth) {
        key |= FLECS_GEOMETRY3_BEVEL_CORNER_CACHE_SMOOTH_MASK;
    }

    return key;
}

static ecs_entity_t flecsEngine_bevelCorner_findAsset(
    const FlecsGeometry3Cache *ctx,
    uint64_t key)
{
    ecs_map_val_t *entry = ecs_map_get(
        &ctx->bevel_corner_cache, (ecs_map_key_t)key);
    if (!entry) {
        return 0;
    }

    return (ecs_entity_t)entry[0];
}

static flecs_vec3_t flecsEngine_bevelCorner_unitPoint(
    float theta,
    float phi)
{
    float sin_theta = sinf(theta);
    float cos_theta = cosf(theta);
    float sin_phi = sinf(phi);
    float cos_phi = cosf(phi);

    return (flecs_vec3_t){
        sin_theta,
        cos_theta * sin_phi,
        cos_theta * cos_phi
    };
}

static flecs_vec3_t flecsEngine_bevelCorner_point(
    float theta,
    float phi,
    float radius)
{
    flecs_vec3_t p = flecsEngine_bevelCorner_unitPoint(theta, phi);
    return (flecs_vec3_t){p.x * radius, p.y * radius, p.z * radius};
}

static flecs_vec3_t flecsEngine_bevelCorner_vec3Sub(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return (flecs_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static flecs_vec3_t flecsEngine_bevelCorner_vec3Cross(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return (flecs_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static float flecsEngine_bevelCorner_vec3Dot(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static flecs_vec3_t flecsEngine_bevelCorner_vec3Normalize(
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

static flecs_vec3_t flecsEngine_bevelCorner_triangleNormal(
    flecs_vec3_t a,
    flecs_vec3_t b,
    flecs_vec3_t c)
{
    flecs_vec3_t centroid = {
        (a.x + b.x + c.x) / 3.0f,
        (a.y + b.y + c.y) / 3.0f,
        (a.z + b.z + c.z) / 3.0f
    };
    flecs_vec3_t fallback = flecsEngine_bevelCorner_vec3Normalize(
        centroid, (flecs_vec3_t){1.0f, 0.0f, 0.0f});

    flecs_vec3_t edge_ab = flecsEngine_bevelCorner_vec3Sub(b, a);
    flecs_vec3_t edge_ac = flecsEngine_bevelCorner_vec3Sub(c, a);
    flecs_vec3_t normal = flecsEngine_bevelCorner_vec3Normalize(
        flecsEngine_bevelCorner_vec3Cross(edge_ab, edge_ac), fallback);

    if (flecsEngine_bevelCorner_vec3Dot(normal, centroid) < 0.0f) {
        normal = (flecs_vec3_t){-normal.x, -normal.y, -normal.z};
    }

    return normal;
}

static void flecsEngine_bevelCorner_generateSmoothMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const float radius = 0.5f;
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = (rings + 1) * (cols + 1);
    const int32_t index_count = rings * cols * 6;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    int32_t vi = 0;
    for (int32_t y = 0; y <= rings; y ++) {
        float v_t = (float)y / (float)rings;
        float theta = v_t * 0.5f * (float)M_PI;

        for (int32_t x = 0; x <= cols; x ++) {
            float u = (float)x / (float)cols;
            float phi = u * 0.5f * (float)M_PI;
            vn[vi] = flecsEngine_bevelCorner_unitPoint(theta, phi);
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
            uint32_t a = (uint32_t)(y * (cols + 1) + x);
            uint32_t b = (uint32_t)(a + cols + 1);
            uint32_t c = (uint32_t)(b + 1);
            uint32_t d = (uint32_t)(a + 1);

            idx[ii ++] = a;
            idx[ii ++] = b;
            idx[ii ++] = d;

            idx[ii ++] = b;
            idx[ii ++] = c;
            idx[ii ++] = d;
        }
    }
}

static void flecsEngine_bevelCorner_generateFlatMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const float radius = 0.5f;
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = rings * cols * 6;
    const int32_t index_count = vert_count;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    int32_t vi = 0;
    int32_t ii = 0;
    for (int32_t y = 0; y < rings; y ++) {
        float v0_t = (float)y / (float)rings;
        float v1_t = (float)(y + 1) / (float)rings;
        float theta0 = v0_t * 0.5f * (float)M_PI;
        float theta1 = v1_t * 0.5f * (float)M_PI;

        for (int32_t x = 0; x < cols; x ++) {
            float u0 = (float)x / (float)cols;
            float u1 = (float)(x + 1) / (float)cols;
            float phi0 = u0 * 0.5f * (float)M_PI;
            float phi1 = u1 * 0.5f * (float)M_PI;

            flecs_vec3_t a = flecsEngine_bevelCorner_point(theta0, phi0, radius);
            flecs_vec3_t b = flecsEngine_bevelCorner_point(theta1, phi0, radius);
            flecs_vec3_t c = flecsEngine_bevelCorner_point(theta1, phi1, radius);
            flecs_vec3_t d = flecsEngine_bevelCorner_point(theta0, phi1, radius);

            flecs_vec3_t n0 = flecsEngine_bevelCorner_triangleNormal(a, b, d);
            flecs_vec3_t n1 = flecsEngine_bevelCorner_triangleNormal(b, c, d);

            uint32_t base = (uint32_t)vi;

            v[vi] = a;
            vn[vi] = n0;
            vi ++;

            v[vi] = b;
            vn[vi] = n0;
            vi ++;

            v[vi] = d;
            vn[vi] = n0;
            vi ++;

            idx[ii ++] = base + 0;
            idx[ii ++] = base + 1;
            idx[ii ++] = base + 2;

            base = (uint32_t)vi;

            v[vi] = b;
            vn[vi] = n1;
            vi ++;

            v[vi] = c;
            vn[vi] = n1;
            vi ++;

            v[vi] = d;
            vn[vi] = n1;
            vi ++;

            idx[ii ++] = base + 0;
            idx[ii ++] = base + 1;
            idx[ii ++] = base + 2;
        }
    }
}

static void flecsEngine_bevelCorner_generateMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    bool smooth)
{
    if (smooth) {
        flecsEngine_bevelCorner_generateSmoothMesh(mesh, segments);
    } else {
        flecsEngine_bevelCorner_generateFlatMesh(mesh, segments);
    }
}

static ecs_entity_t flecsEngine_bevelCorner_getAsset(
    ecs_world_t *world,
    int32_t segments,
    bool smooth)
{
    int32_t normalized_segments = flecsEngine_bevelCorner_segmentsNormalize(
        segments, smooth);
    uint64_t key = flecsEngine_bevelCorner_cacheKey(
        normalized_segments, smooth);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsEngine_bevelCorner_findAsset(ctx, key);
    if (asset) {
        return asset;
    }

    char asset_name[64];
    snprintf(
        asset_name,
        sizeof(asset_name),
        "BevelCorner.bevelCorner%llu", key);

    asset = flecsEngine_geometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsEngine_bevelCorner_generateMesh(mesh, normalized_segments, smooth);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->bevel_corner_cache,
        (ecs_map_key_t)key,
        (ecs_map_val_t)asset);

    return asset;
}

const FlecsMesh3Impl* flecsEngine_bevelCorner_getAssetImpl(
    ecs_world_t *world,
    int32_t segments,
    bool smooth)
{
    ecs_entity_t asset = flecsEngine_bevelCorner_getAsset(world, segments, smooth);
    return ecs_get(world, asset, FlecsMesh3Impl);
}

void FlecsBevelCorner_on_replace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const FlecsBevelCorner *old = ecs_field(it, FlecsBevelCorner, 0);
    const FlecsBevelCorner *new = ecs_field(it, FlecsBevelCorner, 1);
    const FlecsGeometry3Cache *ctx = ecs_singleton_get(world, FlecsGeometry3Cache);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t old_segments = flecsEngine_bevelCorner_segmentsNormalize(
            old[i].segments, old[i].smooth);
        int32_t new_segments = flecsEngine_bevelCorner_segmentsNormalize(
            new[i].segments, new[i].smooth);
        uint64_t old_key = flecsEngine_bevelCorner_cacheKey(
            old_segments, old[i].smooth);
        uint64_t new_key = flecsEngine_bevelCorner_cacheKey(
            new_segments, new[i].smooth);

        if (old_key != new_key) {
            ecs_entity_t old_asset = flecsEngine_bevelCorner_findAsset(ctx, old_key);
            if (old_asset) {
                ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
            }
        }

        ecs_entity_t asset = flecsEngine_bevelCorner_getAsset(
            world, new[i].segments, new[i].smooth);
        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
