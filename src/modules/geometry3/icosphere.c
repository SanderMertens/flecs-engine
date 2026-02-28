#include "geometry3.h"
#include <math.h>
#include <stdio.h>

#define FLECS_GEOMETRY3_ICOSPHERE_SEGMENTS_MIN (0)
#define FLECS_GEOMETRY3_ICOSPHERE_SEGMENTS_MAX_SMOOTH (6)
#define FLECS_GEOMETRY3_ICOSPHERE_SEGMENTS_MAX_FLAT (5)
#define FLECS_GEOMETRY3_ICOSPHERE_CACHE_SEGMENTS_MASK (0x7fffffffULL)
#define FLECS_GEOMETRY3_ICOSPHERE_CACHE_SMOOTH_MASK (1ULL << 63)

typedef struct {
    uint32_t a;
    uint32_t b;
    uint32_t c;
} flecsGeometry3_icoTriangle;

static int32_t flecsGeometry3_icosphereSegmentsNormalize(
    int32_t segments,
    bool smooth)
{
    int32_t max_segments = FLECS_GEOMETRY3_ICOSPHERE_SEGMENTS_MAX_FLAT;
    if (smooth) {
        max_segments = FLECS_GEOMETRY3_ICOSPHERE_SEGMENTS_MAX_SMOOTH;
    }

    if (segments < FLECS_GEOMETRY3_ICOSPHERE_SEGMENTS_MIN) {
        return FLECS_GEOMETRY3_ICOSPHERE_SEGMENTS_MIN;
    }
    if (segments > max_segments) {
        return max_segments;
    }
    return segments;
}

static uint32_t flecsGeometry3_icosphereRadiusBits(
    float radius)
{
    union {
        float f;
        uint32_t u;
    } radius_bits = { .f = radius };

    return radius_bits.u;
}

static uint64_t flecsGeometry3_icosphereCacheKey(
    int32_t segments,
    bool smooth,
    float radius)
{
    uint64_t key =
        (((uint64_t)segments & FLECS_GEOMETRY3_ICOSPHERE_CACHE_SEGMENTS_MASK) << 32) |
        (uint64_t)flecsGeometry3_icosphereRadiusBits(radius);

    if (smooth) {
        key |= FLECS_GEOMETRY3_ICOSPHERE_CACHE_SMOOTH_MASK;
    }

    return key;
}

static ecs_entity_t flecsGeometry3_findIcoSphereAsset(
    const FlecsGeometry3Cache *ctx,
    uint64_t key)
{
    ecs_map_val_t *entry = ecs_map_get(
        &ctx->icosphere_cache, (ecs_map_key_t)key);
    if (!entry) {
        return 0;
    }

    return (ecs_entity_t)entry[0];
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

    if (flecsGeometry3_vec3_dot(normal, centroid) < 0.0f) {
        normal = (flecs_vec3_t){-normal.x, -normal.y, -normal.z};
    }

    return normal;
}

static bool flecsGeometry3_triangleNeedsFlip(
    flecs_vec3_t a,
    flecs_vec3_t b,
    flecs_vec3_t c)
{
    flecs_vec3_t centroid = {
        (a.x + b.x + c.x) / 3.0f,
        (a.y + b.y + c.y) / 3.0f,
        (a.z + b.z + c.z) / 3.0f
    };
    flecs_vec3_t edge_ab = flecsGeometry3_vec3_sub(b, a);
    flecs_vec3_t edge_ac = flecsGeometry3_vec3_sub(c, a);
    flecs_vec3_t winding_normal = flecsGeometry3_vec3_cross(edge_ab, edge_ac);

    return flecsGeometry3_vec3_dot(winding_normal, centroid) < 0.0f;
}

static uint32_t flecsGeometry3_icosphereGetMidpoint(
    ecs_vec_t *vertices,
    ecs_map_t *edge_cache,
    uint32_t a,
    uint32_t b)
{
    uint32_t min_v = a;
    uint32_t max_v = b;
    if (min_v > max_v) {
        min_v = b;
        max_v = a;
    }

    uint64_t key = ((uint64_t)min_v << 32) | (uint64_t)max_v;
    ecs_map_val_t *entry = ecs_map_get(edge_cache, (ecs_map_key_t)key);
    if (entry) {
        return (uint32_t)entry[0];
    }

    flecs_vec3_t *v = ecs_vec_first_t(vertices, flecs_vec3_t);
    flecs_vec3_t midpoint = {
        (v[min_v].x + v[max_v].x) * 0.5f,
        (v[min_v].y + v[max_v].y) * 0.5f,
        (v[min_v].z + v[max_v].z) * 0.5f
    };
    midpoint = flecsGeometry3_vec3_normalize(
        midpoint, (flecs_vec3_t){0.0f, 1.0f, 0.0f});

    uint32_t index = (uint32_t)ecs_vec_count(vertices);
    flecs_vec3_t *new_vertex = ecs_vec_append_t(NULL, vertices, flecs_vec3_t);
    new_vertex[0] = midpoint;

    ecs_map_insert(
        edge_cache,
        (ecs_map_key_t)key,
        (ecs_map_val_t)index);

    return index;
}

static void flecsGeometry3_generateUnitIcoSphere(
    ecs_vec_t *vertices,
    ecs_vec_t *triangles,
    int32_t segments)
{
    const float t = (1.0f + sqrtf(5.0f)) * 0.5f;
    const flecs_vec3_t base_vertices[12] = {
        {-1.0f, t, 0.0f},
        {1.0f, t, 0.0f},
        {-1.0f, -t, 0.0f},
        {1.0f, -t, 0.0f},
        {0.0f, -1.0f, t},
        {0.0f, 1.0f, t},
        {0.0f, -1.0f, -t},
        {0.0f, 1.0f, -t},
        {t, 0.0f, -1.0f},
        {t, 0.0f, 1.0f},
        {-t, 0.0f, -1.0f},
        {-t, 0.0f, 1.0f}
    };
    const flecsGeometry3_icoTriangle base_triangles[20] = {
        {0, 11, 5},
        {0, 5, 1},
        {0, 1, 7},
        {0, 7, 10},
        {0, 10, 11},
        {1, 5, 9},
        {5, 11, 4},
        {11, 10, 2},
        {10, 7, 6},
        {7, 1, 8},
        {3, 9, 4},
        {3, 4, 2},
        {3, 2, 6},
        {3, 6, 8},
        {3, 8, 9},
        {4, 9, 5},
        {2, 4, 11},
        {6, 2, 10},
        {8, 6, 7},
        {9, 8, 1}
    };

    ecs_vec_init_t(NULL, vertices, flecs_vec3_t, 12);
    ecs_vec_init_t(NULL, triangles, flecsGeometry3_icoTriangle, 20);
    ecs_vec_set_count_t(NULL, vertices, flecs_vec3_t, 12);
    ecs_vec_set_count_t(NULL, triangles, flecsGeometry3_icoTriangle, 20);

    flecs_vec3_t *v = ecs_vec_first_t(vertices, flecs_vec3_t);
    flecsGeometry3_icoTriangle *tri = ecs_vec_first_t(
        triangles, flecsGeometry3_icoTriangle);

    for (int32_t i = 0; i < 12; i ++) {
        v[i] = flecsGeometry3_vec3_normalize(
            base_vertices[i], (flecs_vec3_t){0.0f, 1.0f, 0.0f});
    }
    for (int32_t i = 0; i < 20; i ++) {
        tri[i] = base_triangles[i];
    }

    for (int32_t s = 0; s < segments; s ++) {
        int32_t tri_count = ecs_vec_count(triangles);
        const flecsGeometry3_icoTriangle *old_triangles = ecs_vec_first_t(
            triangles, flecsGeometry3_icoTriangle);

        ecs_vec_t next_triangles;
        ecs_vec_init_t(
            NULL, &next_triangles, flecsGeometry3_icoTriangle, tri_count * 4);
        ecs_vec_set_count_t(
            NULL, &next_triangles, flecsGeometry3_icoTriangle, tri_count * 4);
        flecsGeometry3_icoTriangle *next = ecs_vec_first_t(
            &next_triangles, flecsGeometry3_icoTriangle);

        ecs_map_t edge_cache;
        ecs_map_init(&edge_cache, NULL);

        int32_t ti = 0;
        for (int32_t i = 0; i < tri_count; i ++) {
            uint32_t a = old_triangles[i].a;
            uint32_t b = old_triangles[i].b;
            uint32_t c = old_triangles[i].c;

            uint32_t ab = flecsGeometry3_icosphereGetMidpoint(
                vertices, &edge_cache, a, b);
            uint32_t bc = flecsGeometry3_icosphereGetMidpoint(
                vertices, &edge_cache, b, c);
            uint32_t ca = flecsGeometry3_icosphereGetMidpoint(
                vertices, &edge_cache, c, a);

            next[ti ++] = (flecsGeometry3_icoTriangle){a, ab, ca};
            next[ti ++] = (flecsGeometry3_icoTriangle){b, bc, ab};
            next[ti ++] = (flecsGeometry3_icoTriangle){c, ca, bc};
            next[ti ++] = (flecsGeometry3_icoTriangle){ab, bc, ca};
        }

        ecs_map_fini(&edge_cache);
        ecs_vec_fini_t(NULL, triangles, flecsGeometry3_icoTriangle);
        *triangles = next_triangles;
    }
}

static void flecsGeometry3_generateSmoothIcoSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    float radius)
{
    ecs_vec_t unit_vertices;
    ecs_vec_t unit_triangles;
    flecsGeometry3_generateUnitIcoSphere(
        &unit_vertices, &unit_triangles, segments);

    int32_t vert_count = ecs_vec_count(&unit_vertices);
    int32_t tri_count = ecs_vec_count(&unit_triangles);
    int32_t index_count = tri_count * 3;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    const flecs_vec3_t *unit = ecs_vec_first_t(&unit_vertices, flecs_vec3_t);
    const flecsGeometry3_icoTriangle *triangles = ecs_vec_first_t(
        &unit_triangles, flecsGeometry3_icoTriangle);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    for (int32_t i = 0; i < vert_count; i ++) {
        vn[i] = unit[i];
        v[i] = (flecs_vec3_t){
            unit[i].x * radius,
            unit[i].y * radius,
            unit[i].z * radius
        };
    }

    int32_t ii = 0;
    for (int32_t i = 0; i < tri_count; i ++) {
        uint32_t a = triangles[i].a;
        uint32_t b = triangles[i].b;
        uint32_t c = triangles[i].c;

        if (flecsGeometry3_triangleNeedsFlip(unit[a], unit[b], unit[c])) {
            uint32_t tmp = b;
            b = c;
            c = tmp;
        }

        /* Invert face orientation by reversing winding. */
        {
            uint32_t tmp = b;
            b = c;
            c = tmp;
        }

        idx[ii ++] = (uint16_t)a;
        idx[ii ++] = (uint16_t)b;
        idx[ii ++] = (uint16_t)c;
    }

    ecs_vec_fini_t(NULL, &unit_vertices, flecs_vec3_t);
    ecs_vec_fini_t(NULL, &unit_triangles, flecsGeometry3_icoTriangle);
}

static void flecsGeometry3_generateFlatIcoSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    float radius)
{
    ecs_vec_t unit_vertices;
    ecs_vec_t unit_triangles;
    flecsGeometry3_generateUnitIcoSphere(
        &unit_vertices, &unit_triangles, segments);

    int32_t tri_count = ecs_vec_count(&unit_triangles);
    int32_t vert_count = tri_count * 3;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, vert_count);

    const flecs_vec3_t *unit = ecs_vec_first_t(&unit_vertices, flecs_vec3_t);
    const flecsGeometry3_icoTriangle *triangles = ecs_vec_first_t(
        &unit_triangles, flecsGeometry3_icoTriangle);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    int32_t vi = 0;
    for (int32_t i = 0; i < tri_count; i ++) {
        flecs_vec3_t a = {
            unit[triangles[i].a].x * radius,
            unit[triangles[i].a].y * radius,
            unit[triangles[i].a].z * radius
        };
        flecs_vec3_t b = {
            unit[triangles[i].b].x * radius,
            unit[triangles[i].b].y * radius,
            unit[triangles[i].b].z * radius
        };
        flecs_vec3_t c = {
            unit[triangles[i].c].x * radius,
            unit[triangles[i].c].y * radius,
            unit[triangles[i].c].z * radius
        };

        if (flecsGeometry3_triangleNeedsFlip(a, b, c)) {
            flecs_vec3_t tmp = b;
            b = c;
            c = tmp;
        }

        flecs_vec3_t normal = flecsGeometry3_triangleNormal(a, b, c);

        {
            flecs_vec3_t tmp = b;
            b = c;
            c = tmp;
        }

        v[vi] = a;
        vn[vi] = normal;
        idx[vi] = (uint16_t)vi;
        vi ++;

        v[vi] = b;
        vn[vi] = normal;
        idx[vi] = (uint16_t)vi;
        vi ++;

        v[vi] = c;
        vn[vi] = normal;
        idx[vi] = (uint16_t)vi;
        vi ++;
    }

    ecs_vec_fini_t(NULL, &unit_vertices, flecs_vec3_t);
    ecs_vec_fini_t(NULL, &unit_triangles, flecsGeometry3_icoTriangle);
}

static void flecsGeometry3_generateIcoSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    bool smooth,
    float radius)
{
    if (smooth) {
        flecsGeometry3_generateSmoothIcoSphereMesh(mesh, segments, radius);
    } else {
        flecsGeometry3_generateFlatIcoSphereMesh(mesh, segments, radius);
    }
}

static ecs_entity_t flecsGeometry3_getIcoSphereAsset(
    ecs_world_t *world,
    int32_t segments,
    bool smooth,
    float radius)
{
    int32_t normalized_segments = flecsGeometry3_icosphereSegmentsNormalize(
        segments, smooth);
    uint64_t key = flecsGeometry3_icosphereCacheKey(
        normalized_segments, smooth, radius);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsGeometry3_findIcoSphereAsset(ctx, key);
    if (asset) {
        return asset;
    }

    char asset_name[64];
    snprintf(
        asset_name,
        sizeof(asset_name),
        "IcoSphere.icosphere%llu", key);

    asset = flecsGeometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsGeometry3_generateIcoSphereMesh(
        mesh, normalized_segments, smooth, radius);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->icosphere_cache,
        (ecs_map_key_t)key,
        (ecs_map_val_t)asset);

    return asset;
}

void FlecsIcoSphere_on_replace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const FlecsIcoSphere *old = ecs_field(it, FlecsIcoSphere, 0);
    const FlecsIcoSphere *new = ecs_field(it, FlecsIcoSphere, 1);
    const FlecsGeometry3Cache *ctx = ecs_singleton_get(world, FlecsGeometry3Cache);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t old_segments = flecsGeometry3_icosphereSegmentsNormalize(
            old[i].segments, old[i].smooth);
        int32_t new_segments = flecsGeometry3_icosphereSegmentsNormalize(
            new[i].segments, new[i].smooth);
        uint64_t old_key = flecsGeometry3_icosphereCacheKey(
            old_segments, old[i].smooth, old[i].radius);
        uint64_t new_key = flecsGeometry3_icosphereCacheKey(
            new_segments, new[i].smooth, new[i].radius);

        if (old_key == new_key) {
            continue;
        }

        ecs_entity_t old_asset = flecsGeometry3_findIcoSphereAsset(ctx, old_key);
        if (old_asset) {
            ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
        }

        ecs_entity_t asset = flecsGeometry3_getIcoSphereAsset(
            world, new[i].segments, new[i].smooth, new[i].radius);
        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
