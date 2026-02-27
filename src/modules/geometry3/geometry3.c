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

static ecs_entity_t flecsGeometry3_ensureUnitBoxAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx)
{
    if (ctx->unit_box_asset && ecs_is_alive(world, ctx->unit_box_asset)) {
        return ctx->unit_box_asset;
    }

    ctx->unit_box_asset = flecsGeometry3_createGeneratedAsset(world, ctx, "BoxUnit");

    FlecsMesh3 *mesh = ecs_ensure(world, ctx->unit_box_asset, FlecsMesh3);
    flecsGeometry3_generateBoxMesh(mesh);
    ecs_modified(world, ctx->unit_box_asset, FlecsMesh3);

    return ctx->unit_box_asset;
}

ecs_entity_t flecsEngineGeometry3EnsureUnitBoxAsset(
    ecs_world_t *world)
{
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);
    return flecsGeometry3_ensureUnitBoxAsset(world, ctx);
}

const FlecsMesh3Impl* flecsEngineGeometry3EnsureUnitBoxMesh(
    ecs_world_t *world)
{
    ecs_entity_t asset = flecsEngineGeometry3EnsureUnitBoxAsset(world);
    return ecs_get(world, asset, FlecsMesh3Impl);
}

static ecs_entity_t flecsGeometry3_ensureSphereAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx,
    int32_t segments)
{
    segments = flecsGeometry3_clampSegments(ctx, true, segments);

    ecs_map_val_t *cached = ecs_map_get(&ctx->sphere_cache, (ecs_map_key_t)segments);
    if (cached) {
        ecs_entity_t asset = (ecs_entity_t)*cached;
        if (ecs_is_alive(world, asset)) {
            return asset;
        }
    }

    char name[64];
    ecs_os_snprintf(name, sizeof(name), "Sphere_%d", segments);

    ecs_entity_t asset = flecsGeometry3_createGeneratedAsset(world, ctx, name);
    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsGeometry3_generateSphereMesh(mesh, segments);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(&ctx->sphere_cache, (ecs_map_key_t)segments, (ecs_map_val_t)asset);

    return asset;
}

static ecs_entity_t flecsGeometry3_ensureCylinderAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx,
    int32_t segments)
{
    segments = flecsGeometry3_clampSegments(ctx, false, segments);

    ecs_map_val_t *cached = ecs_map_get(&ctx->cylinder_cache, (ecs_map_key_t)segments);
    if (cached) {
        ecs_entity_t asset = (ecs_entity_t)*cached;
        if (ecs_is_alive(world, asset)) {
            return asset;
        }
    }

    char name[64];
    ecs_os_snprintf(name, sizeof(name), "Cylinder_%d", segments);

    ecs_entity_t asset = flecsGeometry3_createGeneratedAsset(world, ctx, name);
    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsGeometry3_generateCylinderMesh(mesh, segments);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(&ctx->cylinder_cache, (ecs_map_key_t)segments, (ecs_map_val_t)asset);

    return asset;
}

static void flecsGeometry3_copyMeshToAsset(
    ecs_world_t *world,
    ecs_entity_t src,
    ecs_entity_t asset)
{
    const FlecsMesh3 *mesh = ecs_get(world, src, FlecsMesh3);
    if (mesh) {
        ecs_set_ptr(world, asset, FlecsMesh3, mesh);
    }
}

static ecs_entity_t flecsGeometry3_ensureDirectMeshAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx,
    ecs_entity_t e,
    const FlecsGeometryBinding3 *binding)
{
    ecs_entity_t asset = 0;
    if (binding &&
        binding->source == FlecsGeometrySourceDirectMesh &&
        binding->asset &&
        ecs_is_alive(world, binding->asset))
    {
        asset = binding->asset;
    } else {
        char name[64];
        ecs_os_snprintf(name, sizeof(name), "DirectMesh_%llu", (unsigned long long)e);
        asset = flecsGeometry3_createGeneratedAsset(world, ctx, name);
    }

    flecsGeometry3_copyMeshToAsset(world, e, asset);
    return asset;
}

static int32_t flecsGeometry3_countExternalIsA(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t managed_asset)
{
    const ecs_type_t *type = ecs_get_type(world, e);
    if (!type) {
        return 0;
    }

    int32_t result = 0;
    for (int32_t i = 0; i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (!ecs_id_is_pair(id)) {
            continue;
        }

        if (ecs_pair_first(world, id) != EcsIsA) {
            continue;
        }

        ecs_entity_t target = ecs_pair_second(world, id);
        if (managed_asset && target == managed_asset) {
            continue;
        }

        result ++;
    }

    return result;
}

static void flecsGeometry3_clearBinding(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const FlecsGeometryBinding3 *binding = ecs_get(world, e, FlecsGeometryBinding3);
    if (!binding) {
        return;
    }

    if (binding->asset) {
        ecs_remove_pair(world, e, EcsIsA, binding->asset);
    }

    ecs_remove(world, e, FlecsGeometryBinding3);
}

static void flecsGeometry3_setBinding(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t asset,
    flecs_geometry_source_t source)
{
    const FlecsGeometryBinding3 *binding = ecs_get(world, e, FlecsGeometryBinding3);
    if (binding && binding->asset != asset && binding->asset) {
        ecs_remove_pair(world, e, EcsIsA, binding->asset);
    }

    if (binding &&
        binding->asset == asset &&
        binding->source == source &&
        ecs_has_pair(world, e, EcsIsA, asset))
    {
        return;
    }

    if (!ecs_has_pair(world, e, EcsIsA, asset)) {
        ecs_add_pair(world, e, EcsIsA, asset);
    }

    ecs_set(world, e, FlecsGeometryBinding3, {
        .asset = asset,
        .source = source
    });

    if (flecsGeometryLogEnabled()) {
        char *entity_path = ecs_get_path(world, e);
        char *asset_path = ecs_get_path(world, asset);
        ecs_dbg("[geom] bind entity=%s source=%d asset=%s",
            entity_path ? entity_path : "<unnamed>",
            (int)source,
            asset_path ? asset_path : "<unnamed>");
        ecs_os_free(entity_path);
        ecs_os_free(asset_path);
        g_geometry_log_count ++;
    }
}

static void flecsGeometry3_markConflict(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *reason)
{
    if (!ecs_has(world, e, FlecsGeometryConflict3)) {
        char *path = ecs_get_path(world, e);
        ecs_err("geometry conflict for '%s': %s", path, reason);
        ecs_os_free(path);
    }

    flecsGeometry3_clearBinding(world, e);
    ecs_set(world, e, FlecsGeometryConflict3, {1});
}

static void flecsGeometry3_clearConflict(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_remove(world, e, FlecsGeometryConflict3);
}

static void flecsGeometry3_resolveEntity(
    ecs_world_t *world,
    FlecsGeometry3Cache *cache,
    ecs_entity_t e)
{
    if (ecs_has_id(world, e, EcsPrefab)) {
        flecsGeometry3_clearBinding(world, e);
        flecsGeometry3_clearConflict(world, e);
        return;
    }

    bool has_box = ecs_owns_id(world, e, ecs_id(FlecsBox));
    bool has_sphere = ecs_owns_id(world, e, ecs_id(FlecsSphere));
    bool has_cylinder = ecs_owns_id(world, e, ecs_id(FlecsCylinder));
    bool has_mesh = ecs_owns_id(world, e, ecs_id(FlecsMesh3));

    int32_t source_count =
        (has_box ? 1 : 0) +
        (has_sphere ? 1 : 0) +
        (has_cylinder ? 1 : 0) +
        (has_mesh ? 1 : 0);

    const FlecsGeometryBinding3 *binding = ecs_get(world, e, FlecsGeometryBinding3);
    ecs_entity_t managed_asset = binding ? binding->asset : 0;

    if (!source_count) {
        flecsGeometry3_clearBinding(world, e);
        flecsGeometry3_clearConflict(world, e);
        return;
    }

    if (source_count > 1) {
        flecsGeometry3_markConflict(world, e, "multiple geometry sources on entity");
        return;
    }

    if (flecsGeometry3_countExternalIsA(world, e, managed_asset) > 0) {
        flecsGeometry3_markConflict(world, e, "primitive/direct mesh cannot be combined with explicit IsA assets");
        return;
    }

    ecs_entity_t asset = 0;
    flecs_geometry_source_t source = FlecsGeometrySourceNone;

    if (has_box) {
        flecsGeometry3_clearBinding(world, e);
        flecsGeometry3_clearConflict(world, e);
        return;
    } else if (has_sphere) {
        source = FlecsGeometrySourceSphere;
        const FlecsSphere *sphere = ecs_get(world, e, FlecsSphere);
        ecs_assert(sphere != NULL, ECS_INTERNAL_ERROR, NULL);
        asset = flecsGeometry3_ensureSphereAsset(world, cache, sphere->segments);
    } else if (has_cylinder) {
        source = FlecsGeometrySourceCylinder;
        const FlecsCylinder *cyl = ecs_get(world, e, FlecsCylinder);
        ecs_assert(cyl != NULL, ECS_INTERNAL_ERROR, NULL);
        asset = flecsGeometry3_ensureCylinderAsset(world, cache, cyl->segments);
    } else {
        source = FlecsGeometrySourceDirectMesh;
        asset = flecsGeometry3_ensureDirectMeshAsset(world, cache, e, binding);
    }

    flecsGeometry3_setBinding(world, e, asset, source);
    flecsGeometry3_clearConflict(world, e);
}

static void flecsGeometry3_resolveById(
    ecs_world_t *world,
    FlecsGeometry3Cache *cache,
    ecs_map_t *visited,
    ecs_id_t id)
{
    ecs_iter_t qit = ecs_each_id(world, id);
    while (ecs_each_next(&qit)) {
        for (int32_t i = 0; i < qit.count; i ++) {
            ecs_entity_t e = qit.entities[i];
            if (ecs_map_get(visited, (ecs_map_key_t)e)) {
                continue;
            }

            ecs_map_insert(visited, (ecs_map_key_t)e, 1);
            flecsGeometry3_resolveEntity(world, cache, e);
        }
    }
}

static void FlecsGeometry3ResolveSources(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsGeometry3Cache *cache = ecs_singleton_ensure(world, FlecsGeometry3Cache);
    ecs_map_t visited;
    ecs_map_init(&visited, NULL);

    flecsGeometry3_resolveById(world, cache, &visited, ecs_id(FlecsBox));
    flecsGeometry3_resolveById(world, cache, &visited, ecs_id(FlecsSphere));
    flecsGeometry3_resolveById(world, cache, &visited, ecs_id(FlecsCylinder));
    flecsGeometry3_resolveById(world, cache, &visited, ecs_id(FlecsMesh3));
    flecsGeometry3_resolveById(world, cache, &visited, ecs_id(FlecsGeometryBinding3));
    flecsGeometry3_resolveById(world, cache, &visited, ecs_id(FlecsGeometryConflict3));

    ecs_map_fini(&visited);
}

static void FlecsGeometry3SyncDirectMeshAssets(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsMesh3 *mesh = ecs_field(it, FlecsMesh3, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const FlecsGeometryBinding3 *binding = ecs_get(world, e, FlecsGeometryBinding3);
        if (!binding || binding->source != FlecsGeometrySourceDirectMesh || !binding->asset) {
            continue;
        }

        ecs_set_ptr(world, binding->asset, FlecsMesh3, &mesh[i]);
    }
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

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "Geometry3ResolveSources"
        }),
        .phase = EcsOnUpdate,
        .callback = FlecsGeometry3ResolveSources
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "Geometry3SyncDirectMeshAssets"
        }),
        .events = { EcsOnSet },
        .query.terms = {
            { .id = ecs_id(FlecsMesh3) }
        },
        .callback = FlecsGeometry3SyncDirectMeshAssets
    });
}
