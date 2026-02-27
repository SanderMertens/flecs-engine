#define FLECS_ENGINE_GEOMETRY_MESH_IMPL
#define FLECS_ENGINE_GEOMETRY_PRIMITIVES3_IMPL
#include "geometry3.h"

#include <math.h>

ECS_COMPONENT_DECLARE(FlecsMesh3);
ECS_COMPONENT_DECLARE(FlecsBox);
ECS_COMPONENT_DECLARE(FlecsSphere);
ECS_COMPONENT_DECLARE(FlecsCylinder);
ECS_COMPONENT_DECLARE(FlecsMesh3Impl);
ECS_COMPONENT_DECLARE(FlecsGeometryBinding3);
ECS_COMPONENT_DECLARE(FlecsGeometryConflict3);

typedef struct {
    uint8_t value;
} FlecsGeneratedGeometryAsset3;

typedef struct {
    ecs_map_t sphere_cache;
    ecs_map_t cylinder_cache;
    ecs_map_t sphere_warned;
    ecs_map_t cylinder_warned;
    ecs_entity_t generated_root;
    ecs_entity_t unit_box_asset;
} FlecsGeometry3Cache;

ECS_COMPONENT_DECLARE(FlecsGeneratedGeometryAsset3);
ECS_COMPONENT_DECLARE(FlecsGeometry3Cache);

typedef enum {
    FlecsGeometrySourceNone = 0,
    FlecsGeometrySourceBox = 1,
    FlecsGeometrySourceSphere = 2,
    FlecsGeometrySourceCylinder = 3,
    FlecsGeometrySourceDirectMesh = 4
} flecs_geometry_source_t;

static const int32_t kSegmentsMin = 3;
static const int32_t kSegmentsMax = 255;
static const int32_t kGeometryLogLimit = 200;
static int32_t g_geometry_log_count = 0;

static bool flecsGeometryLogEnabled(void) {
    return g_geometry_log_count < kGeometryLogLimit;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void FlecsMesh3_fini(
    FlecsMesh3 *ptr)
{
    ecs_vec_fini_t(NULL, &ptr->vertices, flecs_vec3_t);
    ecs_vec_fini_t(NULL, &ptr->normals, flecs_vec3_t);
    ecs_vec_fini_t(NULL, &ptr->indices, uint16_t);
}

ECS_CTOR(FlecsMesh3, ptr, {
    ecs_vec_init_t(NULL, &ptr->vertices, flecs_vec3_t, 0);
    ecs_vec_init_t(NULL, &ptr->normals, flecs_vec3_t, 0);
    ecs_vec_init_t(NULL, &ptr->indices, uint16_t, 0);
})

ECS_MOVE(FlecsMesh3, dst, src, {
    FlecsMesh3_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsMesh3, dst, src, {
    FlecsMesh3_fini(dst);
    dst->vertices = ecs_vec_copy_t(NULL, &src->vertices, flecs_vec3_t);
    dst->normals = ecs_vec_copy_t(NULL, &src->normals, flecs_vec3_t);
    dst->indices = ecs_vec_copy_t(NULL, &src->indices, uint16_t);
})

ECS_DTOR(FlecsMesh3, ptr, {
    FlecsMesh3_fini(ptr);
})

ECS_CTOR(FlecsGeometry3Cache, ptr, {
    ecs_map_init(&ptr->sphere_cache, NULL);
    ecs_map_init(&ptr->cylinder_cache, NULL);
    ecs_map_init(&ptr->sphere_warned, NULL);
    ecs_map_init(&ptr->cylinder_warned, NULL);
    ptr->generated_root = 0;
    ptr->unit_box_asset = 0;
})

ECS_DTOR(FlecsGeometry3Cache, ptr, {
    ecs_map_fini(&ptr->sphere_cache);
    ecs_map_fini(&ptr->cylinder_cache);
    ecs_map_fini(&ptr->sphere_warned);
    ecs_map_fini(&ptr->cylinder_warned);
})

static void FlecsMesh3_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsMesh3 *mesh = ecs_field(it, FlecsMesh3, 0);

    const FlecsEngineImpl *impl = ecs_singleton_get(world, FlecsEngineImpl);
    ecs_assert(impl != NULL, ECS_INVALID_OPERATION, NULL);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        FlecsMesh3Impl *mesh_impl = ecs_ensure(world, e, FlecsMesh3Impl);

        if (mesh_impl->vertex_buffer) {
            wgpuBufferRelease(mesh_impl->vertex_buffer);
            mesh_impl->vertex_buffer = NULL;
        }

        if (mesh_impl->index_buffer) {
            wgpuBufferRelease(mesh_impl->index_buffer);
            mesh_impl->index_buffer = NULL;
        }

        int32_t vert_count = ecs_vec_count(&mesh[i].vertices);
        int32_t ind_count = ecs_vec_count(&mesh[i].indices);

        if (!vert_count || !ind_count) {
            mesh_impl->vertex_count = 0;
            mesh_impl->index_count = 0;
            continue;
        }

        int32_t vert_size = vert_count * (int32_t)sizeof(FlecsLitVertex);
        WGPUBufferDescriptor vert_desc = {
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)vert_size
        };

        FlecsLitVertex *verts = ecs_os_malloc_n(FlecsLitVertex, vert_count);
        flecs_vec3_t *mesh_vertices = ecs_vec_first_t(&mesh[i].vertices, flecs_vec3_t);
        flecs_vec3_t *mesh_normals = ecs_vec_first_t(&mesh[i].normals, flecs_vec3_t);
        for (int v = 0; v < vert_count; v ++) {
            verts[v].p = mesh_vertices[v];
            verts[v].n = mesh_normals[v];
        }

        mesh_impl->vertex_buffer = wgpuDeviceCreateBuffer(impl->device, &vert_desc);
        wgpuQueueWriteBuffer(impl->queue, mesh_impl->vertex_buffer, 0, verts, vert_size);
        ecs_os_free(verts);

        int32_t ind_size = ind_count * (int32_t)sizeof(uint16_t);
        WGPUBufferDescriptor ind_desc = {
            .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)ind_size
        };

        mesh_impl->index_buffer = wgpuDeviceCreateBuffer(impl->device, &ind_desc);
        wgpuQueueWriteBuffer(
            impl->queue,
            mesh_impl->index_buffer,
            0,
            ecs_vec_first_t(&mesh[i].indices, uint16_t),
            ind_size);

        mesh_impl->vertex_count = vert_count;
        mesh_impl->index_count = ind_count;

        if (flecsGeometryLogEnabled()) {
            char *path = ecs_get_path(world, e);
            ecs_dbg("[geom] upload mesh entity=%s vertices=%d indices=%d",
                path ? path : "<unnamed>",
                vert_count, ind_count);
            ecs_os_free(path);
            g_geometry_log_count ++;
        }
    }
}

static void flecsGeometry3_generateBoxMesh(
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
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

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
        uint16_t base = (uint16_t)(f * verts_per_face);
        uint16_t i = (uint16_t)(f * idx_per_face);
        idx[i + 0] = base + 0;
        idx[i + 1] = base + 1;
        idx[i + 2] = base + 2;
        idx[i + 3] = base + 0;
        idx[i + 4] = base + 2;
        idx[i + 5] = base + 3;
    }
}

static void flecsGeometry3_generateSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = (rings + 1) * (cols + 1);
    const int32_t index_count = rings * cols * 6;
    const float radius = 0.5f;

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
        float sin_theta = sinf(theta);
        float cos_theta = cosf(theta);

        for (int32_t x = 0; x <= cols; x ++) {
            float u = (float)x / (float)cols;
            float phi = u * 2.0f * (float)M_PI;
            float sin_phi = sinf(phi);
            float cos_phi = cosf(phi);

            float nx = sin_theta * cos_phi;
            float ny = cos_theta;
            float nz = sin_theta * sin_phi;

            vn[vi] = (flecs_vec3_t){nx, ny, nz};
            v[vi] = (flecs_vec3_t){nx * radius, ny * radius, nz * radius};
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

static void flecsGeometry3_generateCylinderMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const float radius = 0.5f;
    const float y_top = 0.5f;
    const float y_bottom = -0.5f;

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

static ecs_entity_t flecsGeometry3_module(
    ecs_world_t *world)
{
    return ecs_lookup(world, "flecs.engine.geometry3");
}

static ecs_entity_t flecsGeometry3_ensureGeneratedRoot(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx)
{
    if (ctx->generated_root && ecs_is_alive(world, ctx->generated_root)) {
        return ctx->generated_root;
    }

    ecs_entity_desc_t desc = {
        .name = "GeneratedAssets"
    };

    ecs_entity_t module = flecsGeometry3_module(world);
    if (module) {
        desc.parent = module;
    }

    ctx->generated_root = ecs_entity_init(world, &desc);
    ecs_add_id(world, ctx->generated_root, EcsPrefab);
    ecs_set(world, ctx->generated_root, FlecsGeneratedGeometryAsset3, {1});

    return ctx->generated_root;
}

static ecs_entity_t flecsGeometry3_createGeneratedAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx,
    const char *name)
{
    ecs_entity_t root = flecsGeometry3_ensureGeneratedRoot(world, ctx);
    ecs_entity_t asset = ecs_entity(world, {
        .name = name,
        .parent = root
    });

    ecs_add_id(world, asset, EcsPrefab);
    ecs_set(world, asset, FlecsGeneratedGeometryAsset3, {1});

    if (flecsGeometryLogEnabled()) {
        char *path = ecs_get_path(world, asset);
        ecs_dbg("[geom] create generated asset=%s", path ? path : "<unnamed>");
        ecs_os_free(path);
        g_geometry_log_count ++;
    }

    return asset;
}

static void flecsGeometry3_warnSegments(
    ecs_map_t *warned,
    const char *shape,
    int32_t input,
    int32_t clamped)
{
    ecs_map_key_t key = (ecs_map_key_t)(uint64_t)(uint32_t)input;
    if (!ecs_map_get(warned, key)) {
        ecs_map_insert(warned, key, 1);
        ecs_warn("%s segments %d invalid, clamped to %d", shape, input, clamped);
    }
}

static int32_t flecsGeometry3_clampSegments(
    FlecsGeometry3Cache *ctx,
    bool sphere,
    int32_t segments)
{
    ecs_map_t *warned = sphere ? &ctx->sphere_warned : &ctx->cylinder_warned;
    const char *shape = sphere ? "Sphere" : "Cylinder";

    if (segments < kSegmentsMin) {
        flecsGeometry3_warnSegments(warned, shape, segments, kSegmentsMin);
        return kSegmentsMin;
    }

    if (segments > kSegmentsMax) {
        flecsGeometry3_warnSegments(warned, shape, segments, kSegmentsMax);
        return kSegmentsMax;
    }

    return segments;
}

const FlecsMesh3Impl* flecsEngineGeometry3EnsureUnitBox(
    ecs_world_t *world)
{
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);
    ecs_entity_t asset = 0;

    if (ctx->unit_box_asset && ecs_is_alive(world, ctx->unit_box_asset)) {
        asset = ctx->unit_box_asset;
        goto done;
    }

    asset = ctx->unit_box_asset = flecsGeometry3_createGeneratedAsset(
        world, ctx, "BoxUnit");

    FlecsMesh3 *mesh = ecs_ensure(world, ctx->unit_box_asset, FlecsMesh3);
    flecsGeometry3_generateBoxMesh(mesh);
    ecs_modified(world, ctx->unit_box_asset, FlecsMesh3);

done:
    return ecs_get(world, asset, FlecsMesh3Impl);
}

void FlecsEngineGeometry3Import(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineGeometry3);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsMesh3);
    ECS_COMPONENT_DEFINE(world, FlecsMesh3Impl);
    ECS_COMPONENT_DEFINE(world, FlecsBox);
    ECS_COMPONENT_DEFINE(world, FlecsSphere);
    ECS_COMPONENT_DEFINE(world, FlecsCylinder);
    ECS_COMPONENT_DEFINE(world, FlecsGeometryBinding3);
    ECS_COMPONENT_DEFINE(world, FlecsGeometryConflict3);
    ECS_COMPONENT_DEFINE(world, FlecsGeneratedGeometryAsset3);
    ECS_COMPONENT_DEFINE(world, FlecsGeometry3Cache);

    ecs_struct(world, {
        .entity = ecs_id(FlecsBox),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsSphere),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsCylinder),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) }
        }
    });

    ecs_set_hooks(world, FlecsMesh3, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsMesh3),
        .copy = ecs_copy(FlecsMesh3),
        .dtor = ecs_dtor(FlecsMesh3),
        .on_set = FlecsMesh3_on_set
    });

    ecs_set_hooks(world, FlecsMesh3Impl, {
        .ctor = flecs_default_ctor
    });

    ecs_set_hooks(world, FlecsGeometry3Cache, {
        .ctor = ecs_ctor(FlecsGeometry3Cache),
        .dtor = ecs_dtor(FlecsGeometry3Cache)
    });

    ecs_add_pair(world, ecs_id(FlecsMesh3), EcsWith, ecs_id(FlecsMesh3Impl));
    ecs_add_pair(world, ecs_id(FlecsMesh3Impl), EcsOnInstantiate, EcsInherit);

    ecs_singleton_ensure(world, FlecsGeometry3Cache);
}
