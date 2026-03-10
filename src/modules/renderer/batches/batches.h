#ifndef FLECS_ENGINE_BATCHES_H
#define FLECS_ENGINE_BATCHES_H

#include "../renderer.h"

typedef void (*flecsEngine_primitive_scale_t)(
    const void *value,
    float *out);

typedef struct {
    WGPUBuffer instance_transform;
    WGPUBuffer instance_color;
    WGPUBuffer instance_pbr;
    WGPUBuffer instance_emissive;
    WGPUBuffer instance_material_id;
    FlecsInstanceTransform *cpu_transforms;
    FlecsRgba *cpu_colors;
    FlecsPbrMaterial *cpu_pbr_materials;
    FlecsEmissive *cpu_emissives;
    FlecsMaterialId *cpu_material_ids;
    int32_t count;
    int32_t capacity;
    FlecsMesh3Impl mesh;

    ecs_entity_t component;
    ecs_size_t component_size;
    flecsEngine_primitive_scale_t scale_callback;

    uint64_t group_id;
    bool owns_material_data;
} flecsEngine_batch_t;

void flecsEngine_batch_init(
    flecsEngine_batch_t* batch,
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    bool owns_material_data,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback);

flecsEngine_batch_t* flecsEngine_batch_create(
    ecs_world_t *world,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id,
    bool owns_material_data,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback);

void flecsEngine_batch_fini(
    flecsEngine_batch_t *ptr);

void flecsEngine_batch_delete(
    void *ptr);

void flecsEngine_batch_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *ctx,
    int32_t count);

void flecsEngine_batch_prepareInstances(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_t *ctx);

void flecsEngine_batch_draw(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *ctx);

void flecsEngine_batch_transformInstance(
    FlecsInstanceTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z);

void flecsEngine_box_scale(
    const void *ptr, 
    float *scale);

void flecsEngine_quad_scale(
    const void *ptr, 
    float *scale);

void flecsEngine_triangle_scale(
    const void *ptr, 
    float *scale);

void flecsEngine_right_triangle_scale(
    const void *ptr, 
    float *scale);

void flecsEngine_triangle_prism_scale(
    const void *ptr, 
    float *scale);

void flecsEngine_right_triangle_prism_scale(
    const void *ptr,
    float *scale);
    
ecs_entity_t flecsEngine_createBatch_mesh_materialData(
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

ecs_entity_t flecsEngine_createBatch_mesh_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_quads_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

#endif
