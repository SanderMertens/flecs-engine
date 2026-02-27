#include "geometry3.h"

static void flecsGeometry3_generatePyramidMesh(
    FlecsMesh3 *mesh)
{
    const float half = 0.5f;
    const int32_t vert_count = 16;
    const int32_t index_count = 18;
    const float side_nx = 0.8944272f;
    const float side_ny = 0.4472136f;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    /* Base (-Y). */
    v[0] = (flecs_vec3_t){-half, -half, -half};
    v[1] = (flecs_vec3_t){ half, -half, -half};
    v[2] = (flecs_vec3_t){ half, -half,  half};
    v[3] = (flecs_vec3_t){-half, -half,  half};
    vn[0] = vn[1] = vn[2] = vn[3] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};

    /* Front (-Z). */
    v[4] = (flecs_vec3_t){-half, -half, -half};
    v[5] = (flecs_vec3_t){0.0f,  half, 0.0f};
    v[6] = (flecs_vec3_t){ half, -half, -half};
    vn[4] = vn[5] = vn[6] = (flecs_vec3_t){0.0f, side_ny, -side_nx};

    /* Right (+X). */
    v[7] = (flecs_vec3_t){ half, -half, -half};
    v[8] = (flecs_vec3_t){0.0f,  half, 0.0f};
    v[9] = (flecs_vec3_t){ half, -half,  half};
    vn[7] = vn[8] = vn[9] = (flecs_vec3_t){side_nx, side_ny, 0.0f};

    /* Back (+Z). */
    v[10] = (flecs_vec3_t){ half, -half,  half};
    v[11] = (flecs_vec3_t){0.0f,  half, 0.0f};
    v[12] = (flecs_vec3_t){-half, -half,  half};
    vn[10] = vn[11] = vn[12] = (flecs_vec3_t){0.0f, side_ny, side_nx};

    /* Left (-X). */
    v[13] = (flecs_vec3_t){-half, -half,  half};
    v[14] = (flecs_vec3_t){0.0f,  half, 0.0f};
    v[15] = (flecs_vec3_t){-half, -half, -half};
    vn[13] = vn[14] = vn[15] = (flecs_vec3_t){-side_nx, side_ny, 0.0f};

    idx[0] = 0;
    idx[1] = 2;
    idx[2] = 1;
    idx[3] = 0;
    idx[4] = 3;
    idx[5] = 2;

    idx[6] = 4;
    idx[7] = 6;
    idx[8] = 5;

    idx[9] = 7;
    idx[10] = 9;
    idx[11] = 8;

    idx[12] = 10;
    idx[13] = 12;
    idx[14] = 11;

    idx[15] = 13;
    idx[16] = 15;
    idx[17] = 14;
}

const FlecsMesh3Impl* flecsGeometry3_getPyramidAsset(
    ecs_world_t *world)
{
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);
    if (ctx->unit_pyramid_asset) {
        goto done;
    }

    ctx->unit_pyramid_asset = flecsGeometry3_createAsset(world, ctx, "Pyramid");

    FlecsMesh3 *mesh = ecs_ensure(world, ctx->unit_pyramid_asset, FlecsMesh3);
    flecsGeometry3_generatePyramidMesh(mesh);
    ecs_modified(world, ctx->unit_pyramid_asset, FlecsMesh3);

done:
    return ecs_get(world, ctx->unit_pyramid_asset, FlecsMesh3Impl);
}
