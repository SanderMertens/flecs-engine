#include "../renderer.h"
#include "batches.h"
#include "flecs_engine.h"

ecs_entity_t flecsEngine_createBatchSet_geometry_materialData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch_set_entity = ecs_entity(world, { .parent = parent, .name = name });
    FlecsRenderBatchSet batch_set = *ecs_ensure(
        world, batch_set_entity, FlecsRenderBatchSet);

    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_boxes(world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_quads(world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangles(world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangles(world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangle_prisms(world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangle_prisms(
            world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_bevel_boxes(world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_mesh_materialData(world, batch_set_entity, NULL);

    ecs_set_ptr(world, batch_set_entity, FlecsRenderBatchSet, &batch_set);
    return batch_set_entity;
}

ecs_entity_t flecsEngine_createBatchSet_geometry_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch_set_entity = ecs_entity(world, { .parent = parent, .name = name });
    FlecsRenderBatchSet batch_set = *ecs_ensure(
        world, batch_set_entity, FlecsRenderBatchSet);

    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_boxes_materialIndex(world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_quads_materialIndex(world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangles_materialIndex(
            world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangles_materialIndex(
            world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangle_prisms_materialIndex(
            world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangle_prisms_materialIndex(
            world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_bevel_boxes_materialIndex(
            world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_mesh_materialIndex(world, batch_set_entity, NULL);

    ecs_set_ptr(world, batch_set_entity, FlecsRenderBatchSet, &batch_set);
    return batch_set_entity;
}

ecs_entity_t flecsEngine_createBatchSet_geometry(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch_set_entity = ecs_entity(world, { .parent = parent, .name = name });
    FlecsRenderBatchSet batch_set = *ecs_ensure(
        world, batch_set_entity, FlecsRenderBatchSet);

    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatchSet_geometry_materialData(
            world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatchSet_geometry_materialIndex(
            world, batch_set_entity, NULL);
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_textured_mesh(
            world, batch_set_entity, NULL);

    ecs_set_ptr(world, batch_set_entity, FlecsRenderBatchSet, &batch_set);
    return batch_set_entity;
}
