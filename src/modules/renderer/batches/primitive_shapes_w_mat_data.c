#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

ecs_entity_t flecsEngine_createBatch_primitive(
    ecs_world_t *world, 
    ecs_entity_t parent, 
    const char *name, 
    const FlecsMesh3Impl *mesh, 
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrColored(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = component, .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional  },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsLitVertex),
        .instance_types = {
            ecs_id(FlecsInstanceTransform),
            ecs_id(FlecsRgba),
            ecs_id(FlecsPbrMaterial),
            ecs_id(FlecsEmissive)
        },
        .uniforms = {
            ecs_id(FlecsUniform)
        },
        .extract_callback = flecsEngine_primitive_extract,
        .callback = flecsEngine_primitive_render,
        .ctx = flecsEngine_batch_create(world, mesh, 0, true, component, scale_callback),
        .free_ctx = flecsEngine_batch_delete
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name, 
        flecsEngine_box_getAsset(world), ecs_id(FlecsBox),
        flecsEngine_box_scale);
}

ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name, 
        flecsEngine_quad_getAsset(world), ecs_id(FlecsQuad),
        flecsEngine_quad_scale);
}

ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name, 
        flecsEngine_triangle_getAsset(world), ecs_id(FlecsTriangle),
        flecsEngine_triangle_scale);
}

ecs_entity_t flecsEngine_createBatch_right_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name, 
        flecsEngine_rightTriangle_getAsset(world), ecs_id(FlecsRightTriangle),
        flecsEngine_right_triangle_scale);
}

ecs_entity_t flecsEngine_createBatch_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name, 
        flecsEngine_trianglePrism_getAsset(world), ecs_id(FlecsTrianglePrism),
        flecsEngine_triangle_prism_scale);
}

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name, 
        flecsEngine_rightTrianglePrism_getAsset(world), ecs_id(FlecsRightTrianglePrism),
        flecsEngine_right_triangle_prism_scale);
}
