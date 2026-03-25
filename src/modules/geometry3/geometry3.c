#define FLECS_ENGINE_GEOMETRY_MESH_IMPL
#define FLECS_ENGINE_GEOMETRY_PRIMITIVES3_IMPL
#include "geometry3.h"

#include <math.h>

ECS_COMPONENT_DECLARE(FlecsMesh3);
ECS_COMPONENT_DECLARE(FlecsBox);
ECS_COMPONENT_DECLARE(FlecsCone);
ECS_COMPONENT_DECLARE(FlecsQuad);
ECS_COMPONENT_DECLARE(FlecsTriangle);
ECS_COMPONENT_DECLARE(FlecsRightTriangle);
ECS_COMPONENT_DECLARE(FlecsTrianglePrism);
ECS_COMPONENT_DECLARE(FlecsRightTrianglePrism);
ECS_COMPONENT_DECLARE(FlecsSphere);
ECS_COMPONENT_DECLARE(FlecsHemiSphere);
ECS_COMPONENT_DECLARE(FlecsIcoSphere);
ECS_COMPONENT_DECLARE(FlecsNGon);
ECS_COMPONENT_DECLARE(FlecsCylinder);
ECS_COMPONENT_DECLARE(FlecsBevel);
ECS_COMPONENT_DECLARE(FlecsBevelCorner);
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
    ecs_vec_fini_t(NULL, &ptr->uvs, flecs_vec2_t);
    ecs_vec_fini_t(NULL, &ptr->indices, uint32_t);
}

ECS_CTOR(FlecsMesh3, ptr, {
    ecs_vec_init_t(NULL, &ptr->vertices, flecs_vec3_t, 0);
    ecs_vec_init_t(NULL, &ptr->normals, flecs_vec3_t, 0);
    ecs_vec_init_t(NULL, &ptr->uvs, flecs_vec2_t, 0);
    ecs_vec_init_t(NULL, &ptr->indices, uint32_t, 0);
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
    dst->uvs = ecs_vec_copy_t(NULL, &src->uvs, flecs_vec2_t);
    dst->indices = ecs_vec_copy_t(NULL, &src->indices, uint32_t);
})

ECS_DTOR(FlecsMesh3, ptr, {
    FlecsMesh3_fini(ptr);
})

ECS_CTOR(FlecsGeometry3Cache, ptr, {
    ecs_map_init(&ptr->sphere_cache, NULL);
    ecs_map_init(&ptr->hemisphere_cache, NULL);
    ecs_map_init(&ptr->icosphere_cache, NULL);
    ecs_map_init(&ptr->cone_cache, NULL);
    ecs_map_init(&ptr->ngon_cache, NULL);
    ecs_map_init(&ptr->cylinder_cache, NULL);
    ecs_map_init(&ptr->bevel_cache, NULL);
    ecs_map_init(&ptr->bevel_corner_cache, NULL);
    ptr->unit_box_asset = 0;
    ptr->unit_quad_asset = 0;
    ptr->unit_triangle_asset = 0;
    ptr->unit_right_triangle_asset = 0;
    ptr->unit_triangle_prism_asset = 0;
    ptr->unit_right_triangle_prism_asset = 0;
})

ECS_DTOR(FlecsGeometry3Cache, ptr, {
    ecs_map_fini(&ptr->sphere_cache);
    ecs_map_fini(&ptr->hemisphere_cache);
    ecs_map_fini(&ptr->icosphere_cache);
    ecs_map_fini(&ptr->cone_cache);
    ecs_map_fini(&ptr->ngon_cache);
    ecs_map_fini(&ptr->cylinder_cache);
    ecs_map_fini(&ptr->bevel_cache);
    ecs_map_fini(&ptr->bevel_corner_cache);
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

        if (mesh_impl->vertex_uv_buffer) {
            wgpuBufferRelease(mesh_impl->vertex_uv_buffer);
            mesh_impl->vertex_uv_buffer = NULL;
        }

        if (mesh_impl->index_buffer) {
            wgpuBufferRelease(mesh_impl->index_buffer);
            mesh_impl->index_buffer = NULL;
        }

        int32_t vert_count = ecs_vec_count(&mesh[i].vertices);
        int32_t ind_count = ecs_vec_count(&mesh[i].indices);
        int32_t uv_count = ecs_vec_count(&mesh[i].uvs);
        bool has_uvs = uv_count == vert_count && uv_count > 0;

        if (!vert_count || !ind_count) {
            mesh_impl->vertex_count = 0;
            mesh_impl->index_count = 0;
            mesh_impl->has_uvs = false;
            continue;
        }

        mesh_impl->has_uvs = has_uvs;

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

        if (has_uvs) {
            int32_t vert_uv_size = vert_count * (int32_t)sizeof(FlecsLitVertexUv);
            WGPUBufferDescriptor vert_uv_desc = {
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = (uint64_t)vert_uv_size
            };

            FlecsLitVertexUv *uv_verts = ecs_os_malloc_n(FlecsLitVertexUv, vert_count);
            flecs_vec2_t *mesh_uvs = ecs_vec_first_t(&mesh[i].uvs, flecs_vec2_t);
            for (int v = 0; v < vert_count; v ++) {
                uv_verts[v].p = mesh_vertices[v];
                uv_verts[v].n = mesh_normals[v];
                uv_verts[v].uv = mesh_uvs[v];
            }

            mesh_impl->vertex_uv_buffer = wgpuDeviceCreateBuffer(impl->device, &vert_uv_desc);
            wgpuQueueWriteBuffer(impl->queue, mesh_impl->vertex_uv_buffer, 0, uv_verts, vert_uv_size);
            ecs_os_free(uv_verts);
        }

        int32_t ind_size = ind_count * (int32_t)sizeof(uint32_t);
        WGPUBufferDescriptor ind_desc = {
            .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)ind_size
        };

        mesh_impl->index_buffer = wgpuDeviceCreateBuffer(impl->device, &ind_desc);
        uint32_t *indices = ecs_vec_first_t(&mesh[i].indices, uint32_t);
        wgpuQueueWriteBuffer(
            impl->queue, mesh_impl->index_buffer, 0, indices, ind_size);

        mesh_impl->vertex_count = vert_count;
        mesh_impl->index_count = ind_count;

        /* Compute local-space AABB from vertex positions */
        float *bb_min = mesh_impl->aabb_min;
        float *bb_max = mesh_impl->aabb_max;
        bb_min[0] = bb_min[1] = bb_min[2] =  1e18f;
        bb_max[0] = bb_max[1] = bb_max[2] = -1e18f;
        for (int v = 0; v < vert_count; v ++) {
            float px = mesh_vertices[v].x;
            float py = mesh_vertices[v].y;
            float pz = mesh_vertices[v].z;
            if (px < bb_min[0]) bb_min[0] = px;
            if (py < bb_min[1]) bb_min[1] = py;
            if (pz < bb_min[2]) bb_min[2] = pz;
            if (px > bb_max[0]) bb_max[0] = px;
            if (py > bb_max[1]) bb_max[1] = py;
            if (pz > bb_max[2]) bb_max[2] = pz;
        }
    }
}

ecs_entity_t flecsEngine_geometry3_createAsset(
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
    ECS_COMPONENT_DEFINE(world, FlecsCone);
    ECS_COMPONENT_DEFINE(world, FlecsQuad);
    ECS_COMPONENT_DEFINE(world, FlecsTriangle);
    ECS_COMPONENT_DEFINE(world, FlecsRightTriangle);
    ECS_COMPONENT_DEFINE(world, FlecsTrianglePrism);
    ECS_COMPONENT_DEFINE(world, FlecsRightTrianglePrism);
    ECS_COMPONENT_DEFINE(world, FlecsSphere);
    ECS_COMPONENT_DEFINE(world, FlecsHemiSphere);
    ECS_COMPONENT_DEFINE(world, FlecsIcoSphere);
    ECS_COMPONENT_DEFINE(world, FlecsNGon);
    ECS_COMPONENT_DEFINE(world, FlecsCylinder);
    ECS_COMPONENT_DEFINE(world, FlecsBevel);
    ECS_COMPONENT_DEFINE(world, FlecsBevelCorner);
    ECS_COMPONENT_DEFINE(world, FlecsGeometry3Cache);

    ecs_struct(world, {
        .entity = ecs_id(FlecsMesh3),
        .members = {
            { .name = "vertices", .type = flecsEngine_vecVec3(world) },
            { .name = "normals", .type = flecsEngine_vecVec3(world) },
            { .name = "uvs", .type = flecsEngine_vecVec2(world) },
            { .name = "indices", .type = flecsEngine_vecU16(world) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBox),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsCone),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "length", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsSphere),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsHemiSphere),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsIcoSphere),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsNGon),
        .members = {
            { .name = "sides", .type = ecs_id(ecs_i32_t) }
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
        .entity = ecs_id(FlecsTriangle),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRightTriangle),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTrianglePrism),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRightTrianglePrism),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsCylinder),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "length", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBevel),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBevelCorner),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) }
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

    ecs_set_hooks(world, FlecsSphere, {
        .on_replace = FlecsSphere_on_replace
    });

    ecs_set_hooks(world, FlecsHemiSphere, {
        .on_replace = FlecsHemiSphere_on_replace
    });

    ecs_set_hooks(world, FlecsIcoSphere, {
        .on_replace = FlecsIcoSphere_on_replace
    });

    ecs_set_hooks(world, FlecsCone, {
        .on_replace = FlecsCone_on_replace
    });

    ecs_set_hooks(world, FlecsNGon, {
        .on_replace = FlecsNGon_on_replace
    });

    ecs_set_hooks(world, FlecsCylinder, {
        .on_replace = FlecsCylinder_on_replace
    });

    ecs_set_hooks(world, FlecsBevel, {
        .on_replace = FlecsBevel_on_replace
    });

    ecs_set_hooks(world, FlecsBevelCorner, {
        .on_replace = FlecsBevelCorner_on_replace
    });

    ecs_set_hooks(world, FlecsGeometry3Cache, {
        .ctor = ecs_ctor(FlecsGeometry3Cache),
        .dtor = ecs_dtor(FlecsGeometry3Cache)
    });

    ecs_add_pair(world, ecs_id(FlecsMesh3), EcsWith, ecs_id(FlecsMesh3Impl));
    ecs_add_pair(world, ecs_id(FlecsMesh3), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsMesh3Impl), EcsOnInstantiate, EcsInherit);

    ecs_singleton_ensure(world, FlecsGeometry3Cache);
}
