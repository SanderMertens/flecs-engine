#include "geometry3.h"

static void flecsGeometry3_generateTrianglePrismMesh(
    FlecsMesh3 *mesh)
{
    const float half_z = 0.5f;
    const float half_width = 0.5773503f; /* 1 / sqrt(3), height is 1 */
    const float side_nx = 0.8660254f;
    const float side_ny = 0.5f;

    const int32_t vert_count = 18;
    const int32_t index_count = 24;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    /* Front (-Z). */
    v[0] = (flecs_vec3_t){-half_width, -0.5f, -half_z};
    v[1] = (flecs_vec3_t){half_width, -0.5f, -half_z};
    v[2] = (flecs_vec3_t){0.0f, 0.5f, -half_z};
    vn[0] = vn[1] = vn[2] = (flecs_vec3_t){0.0f, 0.0f, -1.0f};

    /* Back (+Z). */
    v[3] = (flecs_vec3_t){-half_width, -0.5f, half_z};
    v[4] = (flecs_vec3_t){0.0f, 0.5f, half_z};
    v[5] = (flecs_vec3_t){half_width, -0.5f, half_z};
    vn[3] = vn[4] = vn[5] = (flecs_vec3_t){0.0f, 0.0f, 1.0f};

    /* Bottom side (A-B). */
    v[6] = (flecs_vec3_t){-half_width, -0.5f, -half_z};
    v[7] = (flecs_vec3_t){-half_width, -0.5f, half_z};
    v[8] = (flecs_vec3_t){half_width, -0.5f, half_z};
    v[9] = (flecs_vec3_t){half_width, -0.5f, -half_z};
    vn[6] = vn[7] = vn[8] = vn[9] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};

    /* Right side (B-C). */
    v[10] = (flecs_vec3_t){half_width, -0.5f, -half_z};
    v[11] = (flecs_vec3_t){half_width, -0.5f, half_z};
    v[12] = (flecs_vec3_t){0.0f, 0.5f, half_z};
    v[13] = (flecs_vec3_t){0.0f, 0.5f, -half_z};
    vn[10] = vn[11] = vn[12] = vn[13] = (flecs_vec3_t){side_nx, side_ny, 0.0f};

    /* Left side (C-A). */
    v[14] = (flecs_vec3_t){0.0f, 0.5f, -half_z};
    v[15] = (flecs_vec3_t){0.0f, 0.5f, half_z};
    v[16] = (flecs_vec3_t){-half_width, -0.5f, half_z};
    v[17] = (flecs_vec3_t){-half_width, -0.5f, -half_z};
    vn[14] = vn[15] = vn[16] = vn[17] = (flecs_vec3_t){-side_nx, side_ny, 0.0f};

    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;

    idx[3] = 3;
    idx[4] = 4;
    idx[5] = 5;

    idx[6] = 6;
    idx[7] = 7;
    idx[8] = 8;
    idx[9] = 6;
    idx[10] = 8;
    idx[11] = 9;

    idx[12] = 10;
    idx[13] = 11;
    idx[14] = 12;
    idx[15] = 10;
    idx[16] = 12;
    idx[17] = 13;

    idx[18] = 14;
    idx[19] = 15;
    idx[20] = 16;
    idx[21] = 14;
    idx[22] = 16;
    idx[23] = 17;
}

const FlecsMesh3Impl* flecsGeometry3_getTrianglePrismAsset(
    ecs_world_t *world)
{
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);
    if (ctx->unit_triangle_prism_asset) {
        goto done;
    }

    ctx->unit_triangle_prism_asset =
        flecsGeometry3_createAsset(world, ctx, "TrianglePrism");

    FlecsMesh3 *mesh = ecs_ensure(world, ctx->unit_triangle_prism_asset, FlecsMesh3);
    flecsGeometry3_generateTrianglePrismMesh(mesh);
    ecs_modified(world, ctx->unit_triangle_prism_asset, FlecsMesh3);

done:
    return ecs_get(world, ctx->unit_triangle_prism_asset, FlecsMesh3Impl);
}
