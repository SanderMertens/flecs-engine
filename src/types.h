#ifndef FLECS_ENGINE_TYPES_H
#define FLECS_ENGINE_TYPES_H

#include "flecs_engine.h"
#include "vendor.h"

#define FLECS_ENGINE_UNIFORMS_MAX (8)
#define FLECS_ENGINE_INSTANCE_TYPES_MAX (8)

struct FlecsEngineSurfaceInterface;

typedef struct {
    flecs_rgba_t color;
    float metallic;
    float roughness;
    float _pad;
} FlecsGpuMaterial;

typedef struct {
    GLFWwindow *window;
    int32_t width;
    int32_t height;
    const struct FlecsEngineSurfaceInterface *surface_impl;
    bool output_done;
    const char *frame_output_path;
    flecs_rgba_t clear_color;

    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    WGPUTexture frame_output_texture;
    WGPUTextureView frame_output_texture_view;

    WGPUTexture depth_texture;
    WGPUTextureView depth_texture_view;
    uint32_t depth_texture_width;
    uint32_t depth_texture_height;

    WGPUSurfaceConfiguration surface_config;
    WGPUTextureFormat hdr_color_format;

    ecs_entity_t fallback_hdri;
    WGPUBindGroupLayout ibl_bind_layout;

    /* Reusable intermediate color targets for post-processing passes. */
    WGPUTexture *effect_target_textures;
    WGPUTextureView *effect_target_views;
    int32_t effect_target_count;
    uint32_t effect_target_width;
    uint32_t effect_target_height;
    WGPUTextureFormat effect_target_format;

    WGPUBuffer material_buffer;
    FlecsGpuMaterial *cpu_materials;
    uint32_t material_buffer_capacity;
    uint32_t material_count;

    ecs_query_t *view_query;
    ecs_query_t *material_query;
    uint32_t last_material_id;
    WGPURenderPipeline last_pipeline;
} FlecsEngineImpl;

extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);

typedef struct {
    WGPUTexture ibl_equirect_texture;
    WGPUTextureView ibl_equirect_texture_view;
    WGPUTexture ibl_prefiltered_cubemap;
    WGPUTextureView ibl_prefiltered_cubemap_view;
    WGPUTexture ibl_brdf_lut_texture;
    WGPUTextureView ibl_brdf_lut_texture_view;
    WGPUSampler ibl_sampler;
    WGPUBindGroup ibl_bind_group;
    uint32_t ibl_prefilter_mip_count;
} FlecHdriImpl;

extern ECS_COMPONENT_DECLARE(FlecHdriImpl);

typedef struct {
    WGPUBuffer vertex_buffer; /* vec<FlecsLitVertex> */
    WGPUBuffer index_buffer;  /* vec<uint16_t> */
    int32_t vertex_count;
    int32_t index_count;
} FlecsMesh3Impl;

extern ECS_COMPONENT_DECLARE(FlecsMesh3Impl);

typedef struct {
    WGPUShaderModule shader_module;
} FlecsShaderImpl;

extern ECS_COMPONENT_DECLARE(FlecsShaderImpl);

typedef struct {
    WGPUBindGroupLayout bind_layout;
    WGPUBindGroup bind_group;
    WGPUBindGroup bind_group_materials;
    WGPURenderPipeline pipeline_hdr;
    WGPUBuffer uniform_buffers[FLECS_ENGINE_UNIFORMS_MAX];
    uint64_t material_buffer_size;
    uint8_t uniform_count;
    bool uses_material;
    bool uses_ibl;
} FlecsRenderBatchImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);

typedef struct {
    WGPUBindGroupLayout bind_layout;
    WGPURenderPipeline pipeline_surface;
    WGPURenderPipeline pipeline_hdr;
    WGPUSampler input_sampler;
} FlecsRenderEffectImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderEffectImpl);

typedef struct {
    WGPUTexture tony_lut_texture;
    WGPUTextureView tony_lut_texture_view;
    WGPUSampler tony_lut_sampler;
} FlecsTonyImpl;

extern ECS_COMPONENT_DECLARE(FlecsTonyImpl);

typedef struct {
    WGPUBindGroupLayout bind_layout;
    WGPURenderPipeline downsample_first_pipeline;
    WGPURenderPipeline downsample_pipeline;
    WGPURenderPipeline upsample_pipeline;
    WGPURenderPipeline upsample_final_surface_pipeline;
    WGPURenderPipeline upsample_final_hdr_pipeline;
    WGPUSampler sampler;
    WGPUBuffer uniform_buffer;
    WGPUTexture texture;
    WGPUTextureView *mip_views;
    uint32_t mip_count;
    uint32_t texture_width;
    uint32_t texture_height;
    WGPUTextureFormat texture_format;
} FlecsBloomImpl;

extern ECS_COMPONENT_DECLARE(FlecsBloomImpl);

typedef struct {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 mvp;
} FlecsCameraImpl;

extern ECS_COMPONENT_DECLARE(FlecsCameraImpl);

#endif
