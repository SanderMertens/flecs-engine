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
    float emissive_strength;
    flecs_rgba_t emissive_color;
} FlecsGpuMaterial;

typedef struct {
    FlecsRgba *color;
    ecs_size_t color_count;
    FlecsPbrMaterial *material;
    ecs_size_t material_count;
    FlecsEmissive *emissive;
    ecs_size_t emissive_count;
} FlecsDefaultAttrCache;

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
    FlecsGpuPointLight *cpu_point_lights;
    int32_t point_light_count;
    int32_t point_light_capacity;
    WGPUBuffer point_light_buffer;

    FlecsGpuSpotLight *cpu_spot_lights;
    int32_t spot_light_count;
    int32_t spot_light_capacity;
    WGPUBuffer spot_light_buffer;

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
    float shadow_frustum_planes[FLECS_ENGINE_SHADOW_CASCADE_COUNT][6][4];
    int32_t shadow_frustum_count;
    bool frustum_valid;
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
    WGPUBindGroup ibl_shadow_bind_group;
    uint32_t scene_bind_version;
    uint32_t ibl_prefilter_mip_count;
} FlecsHdriImpl;

extern ECS_COMPONENT_DECLARE(FlecsHdriImpl);

typedef struct {
    WGPUBuffer vertex_buffer;    /* vec<FlecsLitVertex> */
    WGPUBuffer vertex_uv_buffer; /* vec<FlecsLitVertexUv> (only if mesh has UVs) */
    WGPUBuffer index_buffer;     /* vec<uint32_t> */
    int32_t vertex_count;
    int32_t index_count;
    bool has_uvs;
    float aabb_min[3];           /* local-space AABB minimum */
    float aabb_max[3];           /* local-space AABB maximum */
} FlecsMesh3Impl;

extern ECS_COMPONENT_DECLARE(FlecsMesh3Impl);

typedef struct {
    WGPUTexture texture;
    WGPUTextureView view;
} FlecsTextureImpl;

extern ECS_COMPONENT_DECLARE(FlecsTextureImpl);

typedef struct {
    WGPUShaderModule shader_module;
} FlecsShaderImpl;

extern ECS_COMPONENT_DECLARE(FlecsShaderImpl);

typedef struct {
    WGPUTexture *effect_target_textures;
    WGPUTextureView *effect_target_views;
    int32_t effect_target_count;
    uint32_t effect_target_width;
    uint32_t effect_target_height;
    WGPUTextureFormat effect_target_format;
    WGPUBindGroup passthrough_bind_group;
} FlecsRenderViewImpl;

extern ECS_COMPONENT_DECLARE(FlecsRenderViewImpl);

typedef struct {
    WGPUBindGroupLayout bind_layout;
    WGPUBindGroup bind_group;
    WGPUBindGroup bind_group_materials;
    WGPURenderPipeline pipeline_hdr;
    WGPURenderPipeline pipeline_shadow;
    WGPUBuffer uniform_buffers[FLECS_ENGINE_UNIFORMS_MAX];
    uint64_t material_buffer_size;
    uint8_t uniform_count;
    bool uses_material;
    bool uses_ibl;
    bool uses_shadow;
    bool uses_cluster;
    bool uses_textures;
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
    WGPUBuffer uniform_buffer;
} FlecsExponentialHeightFogImpl;

extern ECS_COMPONENT_DECLARE(FlecsExponentialHeightFogImpl);

typedef struct {
    WGPUBuffer uniform_buffer;
    WGPUTexture blur_intermediate_texture;
    WGPUTextureView blur_intermediate_view;
    uint32_t blur_texture_width;
    uint32_t blur_texture_height;
    WGPUBindGroupLayout blur_bind_layout;
    WGPURenderPipeline blur_pipeline_surface;
    WGPURenderPipeline blur_pipeline_hdr;
} FlecsSSAOImpl;

extern ECS_COMPONENT_DECLARE(FlecsSSAOImpl);

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

typedef struct FlecsEngineSurface {
    WGPUTextureView view_texture;
    WGPUTexture surface_texture;
    bool owns_view_texture;
    WGPUSurfaceGetCurrentTextureStatus surface_status;
    WGPUBuffer readback_buffer;
    uint32_t readback_bytes_per_row;
    uint64_t readback_buffer_size;
} FlecsEngineSurface;

typedef struct FlecsEngineSurfaceInterface {
    int (*init_instance)(
        FlecsEngineImpl *impl,
        const void *config);
    int (*configure_target)(
        FlecsEngineImpl *impl);
    int (*prepare_frame)(
        ecs_world_t *world,
        FlecsEngineImpl *impl);
    int (*acquire_frame)(
        FlecsEngineImpl *impl,
        FlecsEngineSurface *target);
    int (*encode_frame)(
        FlecsEngineImpl *impl,
        WGPUCommandEncoder encoder,
        FlecsEngineSurface *target);
    int (*submit_frame)(
        ecs_world_t *world,
        FlecsEngineImpl *impl,
        const FlecsEngineSurface *target);
    void (*on_frame_failed)(
        ecs_world_t *world,
        FlecsEngineImpl *impl);
    void (*cleanup)(
        FlecsEngineImpl *impl,
        bool terminate_runtime);
} FlecsEngineSurfaceInterface;

typedef struct FlecsEngineOutputDesc {
    const FlecsEngineSurfaceInterface *ops;
    const void *config;
    int32_t width;
    int32_t height;
    int32_t resolution_scale;
    bool msaa;
} FlecsEngineOutputDesc;

#endif
