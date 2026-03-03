#include "../../renderer.h"
#include "flecs_engine.h"

ecs_entity_t flecsEngine_createBatchSet_primitiveShapes(
    ecs_world_t *world)
{
    ecs_entity_t batch_set_entity = ecs_new(world);
    FlecsRenderBatchSet *batch_set = ecs_ensure(
        world, batch_set_entity, FlecsRenderBatchSet);

    ecs_vec_append_t(NULL, &batch_set->batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_boxes(world);
    ecs_vec_append_t(NULL, &batch_set->batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_quads(world);
    ecs_vec_append_t(NULL, &batch_set->batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangles(world);
    ecs_vec_append_t(NULL, &batch_set->batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangles(world);
    ecs_vec_append_t(NULL, &batch_set->batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangle_prisms(world);
    ecs_vec_append_t(NULL, &batch_set->batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangle_prisms(world);
    ecs_vec_append_t(NULL, &batch_set->batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_mesh(world);

    ecs_modified(world, batch_set_entity, FlecsRenderBatchSet);
    return batch_set_entity;
}
