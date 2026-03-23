#include "geometry3.h"

static void flecsEngine_triangle_generateMesh(
    FlecsMesh3 *mesh)
{
    const float half_width = 0.5773503f; /* 1 / sqrt(3), height is 1 */

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, 3);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, 3);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, 3);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    v[0] = (flecs_vec3_t){-half_width, -0.5f, 0.0f};
    v[1] = (flecs_vec3_t){half_width, -0.5f, 0.0f};
    v[2] = (flecs_vec3_t){0.0f, 0.5f, 0.0f};
    vn[0] = vn[1] = vn[2] = (flecs_vec3_t){0.0f, 0.0f, 1.0f};

    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;
}

const FlecsMesh3Impl* flecsEngine_triangle_getAsset(
    ecs_world_t *world)
{
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);
    if (ctx->unit_triangle_asset) {
        goto done;
    }

    ctx->unit_triangle_asset = flecsEngine_geometry3_createAsset(world, ctx, "Triangle");

    FlecsMesh3 *mesh = ecs_ensure(world, ctx->unit_triangle_asset, FlecsMesh3);
    flecsEngine_triangle_generateMesh(mesh);
    ecs_modified(world, ctx->unit_triangle_asset, FlecsMesh3);

done:
    return ecs_get(world, ctx->unit_triangle_asset, FlecsMesh3Impl);
}
