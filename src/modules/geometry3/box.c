#include "geometry3.h"
#include <math.h>

static void flecsEngine_box_generateMesh(
    FlecsMesh3 *mesh)
{
    const float half = 0.5f;
    const int32_t face_count = 6;
    const int32_t verts_per_face = 4;
    const int32_t idx_per_face = 6;
    const int32_t vert_count = face_count * verts_per_face;
    const int32_t index_count = face_count * idx_per_face;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    /* -Z */
    v[0] = (flecs_vec3_t){-half, -half, -half};
    v[1] = (flecs_vec3_t){ half, -half, -half};
    v[2] = (flecs_vec3_t){ half,  half, -half};
    v[3] = (flecs_vec3_t){-half,  half, -half};
    vn[0] = vn[1] = vn[2] = vn[3] = (flecs_vec3_t){0.0f, 0.0f, -1.0f};

    /* +Z */
    v[4] = (flecs_vec3_t){-half, -half,  half};
    v[5] = (flecs_vec3_t){-half,  half,  half};
    v[6] = (flecs_vec3_t){ half,  half,  half};
    v[7] = (flecs_vec3_t){ half, -half,  half};
    vn[4] = vn[5] = vn[6] = vn[7] = (flecs_vec3_t){0.0f, 0.0f, 1.0f};

    /* -X */
    v[8] = (flecs_vec3_t){-half, -half, -half};
    v[9] = (flecs_vec3_t){-half,  half, -half};
    v[10] = (flecs_vec3_t){-half,  half,  half};
    v[11] = (flecs_vec3_t){-half, -half,  half};
    vn[8] = vn[9] = vn[10] = vn[11] = (flecs_vec3_t){-1.0f, 0.0f, 0.0f};

    /* +X */
    v[12] = (flecs_vec3_t){ half, -half, -half};
    v[13] = (flecs_vec3_t){ half, -half,  half};
    v[14] = (flecs_vec3_t){ half,  half,  half};
    v[15] = (flecs_vec3_t){ half,  half, -half};
    vn[12] = vn[13] = vn[14] = vn[15] = (flecs_vec3_t){1.0f, 0.0f, 0.0f};

    /* -Y */
    v[16] = (flecs_vec3_t){-half, -half, -half};
    v[17] = (flecs_vec3_t){-half, -half,  half};
    v[18] = (flecs_vec3_t){ half, -half,  half};
    v[19] = (flecs_vec3_t){ half, -half, -half};
    vn[16] = vn[17] = vn[18] = vn[19] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};

    /* +Y */
    v[20] = (flecs_vec3_t){-half,  half, -half};
    v[21] = (flecs_vec3_t){ half,  half, -half};
    v[22] = (flecs_vec3_t){ half,  half,  half};
    v[23] = (flecs_vec3_t){-half,  half,  half};
    vn[20] = vn[21] = vn[22] = vn[23] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};

    for (int32_t f = 0; f < face_count; f ++) {
        uint32_t base = (uint32_t)(f * verts_per_face);
        uint32_t i = (uint32_t)(f * idx_per_face);
        idx[i + 0] = base + 0;
        idx[i + 1] = base + 2;
        idx[i + 2] = base + 1;
        idx[i + 3] = base + 0;
        idx[i + 4] = base + 3;
        idx[i + 5] = base + 2;
    }
}

const FlecsMesh3Impl* flecsEngine_box_getAsset(
    ecs_world_t *world)
{
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);
    if (ctx->unit_box_asset) {
        goto done;
    }

    ctx->unit_box_asset = flecsEngine_geometry3_createAsset(world, ctx, "Box");

    FlecsMesh3 *mesh = ecs_ensure(world, ctx->unit_box_asset, FlecsMesh3);
    flecsEngine_box_generateMesh(mesh);
    ecs_modified(world, ctx->unit_box_asset, FlecsMesh3);

done:
    return ecs_get(world, ctx->unit_box_asset, FlecsMesh3Impl);
}
