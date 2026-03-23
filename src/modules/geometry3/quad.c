#include "geometry3.h"

static void flecsEngine_quad_generateMesh(
    FlecsMesh3 *mesh)
{
    const float half = 0.5f;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, 4);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, 4);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, 6);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    v[0] = (flecs_vec3_t){-half, -half, 0.0f};
    v[1] = (flecs_vec3_t){-half,  half, 0.0f};
    v[2] = (flecs_vec3_t){ half,  half, 0.0f};
    v[3] = (flecs_vec3_t){ half, -half, 0.0f};
    vn[0] = vn[1] = vn[2] = vn[3] = (flecs_vec3_t){0.0f, 0.0f, 1.0f};

    idx[0] = 0;
    idx[1] = 2;
    idx[2] = 1;
    idx[3] = 0;
    idx[4] = 3;
    idx[5] = 2;
}

const FlecsMesh3Impl* flecsEngine_quad_getAsset(
    ecs_world_t *world)
{
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);
    if (ctx->unit_quad_asset) {
        goto done;
    }

    ctx->unit_quad_asset = flecsEngine_geometry3_createAsset(world, ctx, "Quad");

    FlecsMesh3 *mesh = ecs_ensure(world, ctx->unit_quad_asset, FlecsMesh3);
    flecsEngine_quad_generateMesh(mesh);
    ecs_modified(world, ctx->unit_quad_asset, FlecsMesh3);

done:
    return ecs_get(world, ctx->unit_quad_asset, FlecsMesh3Impl);
}
