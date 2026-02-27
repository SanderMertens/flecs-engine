#define FLECS_ENGINE_GEOMETRY_MESH_IMPL
#define FLECS_ENGINE_GEOMETRY_PRIMITIVES3_IMPL
#include "geometry3.h"

#include <math.h>

ECS_COMPONENT_DECLARE(FlecsMesh3);
ECS_COMPONENT_DECLARE(FlecsBox);
ECS_COMPONENT_DECLARE(FlecsQuad);
ECS_COMPONENT_DECLARE(FlecsSphere);
ECS_COMPONENT_DECLARE(FlecsCylinder);
ECS_COMPONENT_DECLARE(FlecsMesh3Impl);
ECS_COMPONENT_DECLARE(FlecsGeometry3Cache);

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
    ptr->unit_box_asset = 0;
    ptr->unit_quad_asset = 0;
})

ECS_DTOR(FlecsGeometry3Cache, ptr, {
    ecs_map_fini(&ptr->sphere_cache);
    ecs_map_fini(&ptr->cylinder_cache);
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
    }
}

ecs_entity_t flecsGeometry3_createAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx,
    const char *name)
{
    ecs_entity_t asset = ecs_entity(world, {
        .name = name,
        .parent = ecs_entity(world, {
            .name = "flecs.engine.geometry3"
        })
    });

    ecs_add_id(world, asset, EcsPrefab);

    return asset;
}

void FlecsEngineGeometry3Import(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineGeometry3);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsMesh3);
    ECS_COMPONENT_DEFINE(world, FlecsMesh3Impl);
    ECS_COMPONENT_DEFINE(world, FlecsBox);
    ECS_COMPONENT_DEFINE(world, FlecsQuad);
    ECS_COMPONENT_DEFINE(world, FlecsSphere);
    ECS_COMPONENT_DEFINE(world, FlecsCylinder);
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
        .entity = ecs_id(FlecsQuad),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
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
