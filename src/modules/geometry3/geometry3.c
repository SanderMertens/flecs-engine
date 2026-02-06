#define FLECS_ENGINE_GEOMETRY_MESH_IMPL
#define FLECS_ENGINE_GEOMETRY_PRIMITIVES3_IMPL
#include "geometry3.h"

ECS_COMPONENT_DECLARE(FlecsMesh3);
ECS_COMPONENT_DECLARE(FlecsCube);
ECS_COMPONENT_DECLARE(FlecsMesh3Impl);

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

static void FlecsMesh3_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsMesh3 *mesh = ecs_field(it, FlecsMesh3, 0);

    const FlecsEngineImpl *impl = ecs_singleton_get(world, FlecsEngineImpl);
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        FlecsMesh3Impl *mesh_impl = ecs_ensure(world, e, FlecsMesh3Impl);

        if (mesh_impl->vertex_buffer) {
            wgpuBufferRelease(mesh_impl->vertex_buffer);
        }

        if (mesh_impl->index_buffer) {
            wgpuBufferRelease(mesh_impl->index_buffer);
        }

        // Create vertex buffer
        int32_t vert_count = ecs_vec_count(&mesh[i].vertices);
        int32_t vert_size = vert_count * sizeof(FlecsLitVertex);
        WGPUBufferDescriptor vert_desc = {
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)vert_size
        };

        FlecsLitVertex *verts = ecs_os_malloc_n(FlecsLitVertex, vert_count);
        flecs_vec3_t *mesh_vertices = ecs_vec_first(&mesh[i].vertices);
        flecs_vec3_t *mesh_normals = ecs_vec_first(&mesh[i].normals);
        for (int v = 0; v < vert_count; v ++) {
            verts[v].p = mesh_vertices[v];
            verts[v].n = mesh_normals[v];
        }

        mesh_impl->vertex_buffer = wgpuDeviceCreateBuffer(
            impl->device, &vert_desc);
        wgpuQueueWriteBuffer(impl->queue, mesh_impl->vertex_buffer, 0, 
            verts, vert_size);
        ecs_os_free(verts);

        // Create index buffer
        int32_t ind_count = ecs_vec_count(&mesh[i].indices);
        int32_t ind_size = ind_count * sizeof(uint16_t);
        WGPUBufferDescriptor ind_desc = {
            .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)ind_size
        };

        mesh_impl->index_buffer = wgpuDeviceCreateBuffer(
            impl->device, &ind_desc);
        wgpuQueueWriteBuffer(impl->queue, mesh_impl->index_buffer, 0, 
            ecs_vec_first(&mesh[i].indices), ind_size);

        mesh_impl->vertex_count = vert_count;
        mesh_impl->index_count = ind_count;
    }
}

static void FlecsCube_on_set(
    ecs_iter_t *it) 
{
    FlecsCube *cubes = ecs_field(it, FlecsCube, 0);

    for (int i = 0; i < it->count; i ++) {
        FlecsCube *cube = &cubes[i];
        FlecsMesh3 *mesh = ecs_ensure(it->world, it->entities[i], FlecsMesh3);

        float half = (float)cube->size / 2.0f;
        float n = 1 / sqrt(3);

        ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, 8);
        ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, 8);
        ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, 36);

        flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
        flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
        uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

        v[0] = (flecs_vec3_t){-half, -half, -half};
        v[1] = (flecs_vec3_t){ half, -half, -half};
        v[2] = (flecs_vec3_t){ half,  half, -half};
        v[3] = (flecs_vec3_t){-half,  half, -half};
        v[4] = (flecs_vec3_t){-half, -half,  half};
        v[5] = (flecs_vec3_t){ half, -half,  half};
        v[6] = (flecs_vec3_t){ half,  half,  half};
        v[7] = (flecs_vec3_t){-half,  half,  half};

        vn[0] = (flecs_vec3_t){-n, -n, -n};
        vn[1] = (flecs_vec3_t){ n, -n, -n};
        vn[2] = (flecs_vec3_t){ n,  n, -n};
        vn[3] = (flecs_vec3_t){-n,  n, -n};
        vn[4] = (flecs_vec3_t){-n, -n,  n};
        vn[5] = (flecs_vec3_t){ n, -n,  n};
        vn[6] = (flecs_vec3_t){ n,  n,  n};
        vn[7] = (flecs_vec3_t){-n,  n,  n};

        idx[0]  = 0; idx[1]  = 1; idx[2]  = 2;
        idx[3]  = 0; idx[4]  = 2; idx[5]  = 3;
        idx[6]  = 4; idx[7]  = 6; idx[8]  = 5;
        idx[9]  = 4; idx[10] = 7; idx[11] = 6;
        idx[12] = 0; idx[13] = 3; idx[14] = 7;
        idx[15] = 0; idx[16] = 7; idx[17] = 4;
        idx[18] = 1; idx[19] = 5; idx[20] = 6;
        idx[21] = 1; idx[22] = 6; idx[23] = 2;
        idx[24] = 0; idx[25] = 4; idx[26] = 5;
        idx[27] = 0; idx[28] = 5; idx[29] = 1;
        idx[30] = 3; idx[31] = 2; idx[32] = 6;
        idx[33] = 3; idx[34] = 6; idx[35] = 7;

        ecs_modified(it->world, it->entities[i], FlecsMesh3);
    }
}

void FlecsEngineGeometry3Import(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineGeometry3);
    
    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsMesh3);
    ECS_COMPONENT_DEFINE(world, FlecsMesh3Impl);
    ECS_META_COMPONENT(world, FlecsCube);

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

    ecs_set_hooks(world, FlecsCube, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsCube_on_set
    });

    ecs_add_pair(world, ecs_id(FlecsCube), EcsWith, ecs_id(FlecsMesh3));
    ecs_add_pair(world, ecs_id(FlecsMesh3), EcsWith, ecs_id(FlecsMesh3Impl));
}
