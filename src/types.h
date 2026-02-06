#ifndef FLECS_ENGINE_TYPES_H
#define FLECS_ENGINE_TYPES_H

#include "flecs_engine.h"
#include "vendor.h"

#define FLECS_ENGINE_UNIFORMS_MAX (8)
#define FLECS_ENGINE_INSTANCE_TYPES_MAX (8)

typedef struct {
    GLFWwindow *window;
    int32_t width;
    int32_t height;

    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTexture depth_texture;
    WGPUTextureView depth_texture_view;

    WGPUSurfaceConfiguration surface_config;

    ecs_query_t *view_query;
} FlecsEngineImpl;

extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);

typedef struct {
    WGPUBuffer vertex_buffer; /* vec<FlecsLitVertex> */
    WGPUBuffer index_buffer;  /* vec<uint16_t> */
    int32_t vertex_count;
    int32_t index_count;
} FlecsMesh3Impl;

extern ECS_COMPONENT_DECLARE(FlecsMesh3Impl);

typedef struct {
    WGPUShaderModule shader;
    WGPUBindGroupLayout bind_layout;
    WGPUBindGroup bind_group;
    WGPURenderPipeline pipeline;
    WGPUBuffer uniform_buffers[FLECS_ENGINE_UNIFORMS_MAX];
    uint8_t uniform_count;
} FlecsRenderBatchImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);

typedef struct {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 mvp;
} FlecsCameraImpl;

extern ECS_COMPONENT_DECLARE(FlecsCameraImpl);

#endif
