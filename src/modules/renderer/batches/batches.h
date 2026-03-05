#ifndef FLECS_ENGINE_BATCHES_H
#define FLECS_ENGINE_BATCHES_H

#include "../renderer.h"

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    WGPUBuffer instance_pbr;
    WGPUBuffer instance_emissive;
    WGPUBuffer instance_material_id;
    FlecsInstanceTransform *cpu_transforms;
    int32_t count;
    int32_t capacity;
    FlecsMaterialId *cpu_material_ids;
    int32_t material_id_capacity;
    FlecsMesh3Impl mesh;
} flecsEngine_batch_t;

void flecsEngine_batch_init(
    flecsEngine_batch_t *ctx,
    const FlecsMesh3Impl *mesh);

void flecsEngine_batch_fini(
    flecsEngine_batch_t *ctx);

void flecsEngine_batch_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t count);

void flecsEngine_batch_draw(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx);

void flecsEngine_batch_drawMaterialIndex(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx);

void flecsEngine_batch_uploadInstances(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t offset,
    const FlecsRgba *colors,
    const FlecsPbrMaterial *materials,
    const FlecsEmissive *emissives,
    int32_t count);

void flecsEngine_batch_uploadMaterialIds(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t offset,
    const FlecsMaterialId *material_ids,
    int32_t count);

void flecsEngine_packInstanceTransform(
    FlecsInstanceTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z);

ecs_entity_t flecsEngine_createBatch_mesh(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_right_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_right_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_skybox(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_boxes_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_quads_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_triangles_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_right_triangles_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_triangle_prisms_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);
ecs_entity_t flecsEngine_createBatch_right_triangle_prisms_matIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

#endif
