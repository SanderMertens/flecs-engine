#ifndef FLECS_ENGINE_TYPES_ENGINE_STATE_H
#define FLECS_ENGINE_TYPES_ENGINE_STATE_H

#include "flecs_engine.h"
#include "../vendor.h"
#include "gpu_types.h"

#define FLECS_ENGINE_UNIFORMS_MAX (8)
#define FLECS_ENGINE_INSTANCE_TYPES_MAX (8)

struct FlecsEngineSurfaceInterface;

typedef struct {
    WGPUTexture texture;
    WGPUTextureView texture_view;
    WGPUTextureView layer_views[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    uint32_t map_size;
    uint32_t cascade_sizes[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    WGPUShaderModule shader_module;
    WGPUBuffer vp_buffers[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    WGPUBindGroupLayout pass_bind_layout;
    WGPUBindGroup pass_bind_groups[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    int current_cascade;
    WGPUSampler sampler;
    mat4 current_light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    float cascade_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    bool in_pass;
} flecs_engine_shadow_t;

typedef struct {
    FlecsGpuLight *cpu_lights;
    int32_t light_count;
    int32_t light_capacity;
    WGPUBuffer light_buffer;

    uint32_t *cpu_cluster_indices;
    int32_t cluster_index_capacity;
    WGPUBuffer cluster_info_buffer;
    WGPUBuffer cluster_grid_buffer;
    WGPUBuffer cluster_index_buffer;
    bool cluster_bind_group_dirty;

    ecs_query_t *point_light_query;
    ecs_query_t *spot_light_query;
} flecs_engine_lighting_t;

typedef struct {
    WGPUBuffer buffer;
    FlecsGpuMaterial *cpu_materials;
    uint32_t buffer_capacity;
    uint32_t count;
    ecs_query_t *query;

    WGPUBindGroupLayout pbr_texture_bind_layout;
    WGPUSampler pbr_sampler;
    WGPUTexture fallback_white_tex;
    WGPUTextureView fallback_white_view;
    WGPUTexture fallback_black_tex;
    WGPUTextureView fallback_black_view;
    WGPUTexture fallback_normal_tex;
    WGPUTextureView fallback_normal_view;
    uint32_t next_id;
    uint32_t last_id;
} flecs_engine_materials_t;

typedef struct {
    WGPUTexture depth_texture;
    WGPUTextureView depth_texture_view;
    uint32_t depth_texture_width;
    uint32_t depth_texture_height;

    WGPUTexture msaa_color_texture;
    WGPUTextureView msaa_color_texture_view;
    WGPUTexture msaa_depth_texture;
    WGPUTextureView msaa_depth_texture_view;
    uint32_t msaa_texture_width;
    uint32_t msaa_texture_height;
    int32_t msaa_texture_sample_count;
    WGPUTextureFormat msaa_color_format;

    /* Passthrough effect for blitting batch output to screen when no
     * effects are enabled. Built directly during renderer init. */
    WGPURenderPipeline passthrough_pipeline;
    WGPUBindGroupLayout passthrough_bind_layout;
    WGPUSampler passthrough_sampler;

    /* Depth resolve pass: resolves MSAA depth into the 1-sample depth
     * texture so that post-process effects (SSAO, fog, …) can read it. */
    WGPURenderPipeline depth_resolve_pipeline;
    WGPUBindGroupLayout depth_resolve_bind_layout;
} flecs_engine_depth_t;

typedef struct {
    GLFWwindow *window;
    int32_t width;
    int32_t height;
    int32_t actual_width;
    int32_t actual_height;
    int32_t resolution_scale;
    int32_t sample_count; /* 1 or 4 (derived from msaa bool) */
    bool vsync;
    const struct FlecsEngineSurfaceInterface *surface_impl;
    bool output_done;
    const char *frame_output_path;

    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    WGPUTexture frame_output_texture;
    WGPUTextureView frame_output_texture_view;

    WGPUSurfaceConfiguration surface_config;
    WGPUTextureFormat hdr_color_format;

    ecs_entity_t sky_background_hdri;
    ecs_entity_t black_hdri;
    flecs_engine_background_t sky_bg_colors;
    WGPUBindGroupLayout ibl_shadow_bind_layout;
    uint32_t scene_bind_version;

    ecs_query_t *view_query;
    WGPURenderPipeline last_pipeline;
    float camera_pos[3];

    flecs_engine_shadow_t shadow;
    flecs_engine_lighting_t lighting;
    flecs_engine_materials_t materials;
    flecs_engine_depth_t depth;

    FlecsDefaultAttrCache *default_attr_cache;

    /* Frustum culling state (computed once per frame during extract) */
    float frustum_planes[6][4];
    float shadow_frustum_planes[6][4];
    bool frustum_valid;
    bool shadow_frustum_valid;
} FlecsEngineImpl;

extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);

#endif
