#ifndef FLECS_ENGINE_GEOMETRY_MESH_H
#define FLECS_ENGINE_GEOMETRY_MESH_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_GEOMETRY_MESH_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsMesh3, {
    ecs_vec_t vertices;  /* vec<vec3> */
    ecs_vec_t normals;   /* vec<vec3> */
    ecs_vec_t indices;   /* vec<uint16> */
});

#endif
