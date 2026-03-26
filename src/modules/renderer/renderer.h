#include "../../private.h"
#include "shaders/shaders.h"

#ifndef FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_RENDERER_IMPL_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT 4096

struct FlecsRenderBatch;
struct FlecsRenderEffect;
extern ECS_TAG_DECLARE(FlecsSkyboxBatch);
extern ECS_TAG_DECLARE(FlecsTransparentBatch);

typedef void (*flecs_render_batch_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const struct FlecsRenderBatch *batch);

typedef void (*flecs_render_batch_extract_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const struct FlecsRenderBatch *batch);

typedef bool (*flecs_render_effect_setup_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const struct FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count);

typedef bool (*flecs_render_effect_bind_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const struct FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count);

typedef bool (*flecs_render_effect_render_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    ecs_entity_t effect_entity,
    const struct FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *effect_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat input_format,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op);

// Render entities matching a query with specified shader
ECS_STRUCT(FlecsRenderBatch, {
    ecs_entity_t shader;
    ecs_query_t *query;
    ecs_entity_t vertex_type;
    ecs_entity_t instance_types[FLECS_ENGINE_INSTANCE_TYPES_MAX];
    ecs_entity_t uniforms[FLECS_ENGINE_UNIFORMS_MAX];
ECS_PRIVATE
    flecs_render_batch_extract_callback extract_callback;
    flecs_render_batch_callback callback;
    void *ctx;
    void (*free_ctx)(void *ctx);
});

// Fullscreen post-process effect. Input uses chain indexing:
// 0 = batches framebuffer, k > 0 = output of effect[k - 1].
ECS_STRUCT(FlecsRenderEffect, {
    ecs_entity_t shader;
    int32_t input;
ECS_PRIVATE
    flecs_render_effect_setup_callback setup_callback;
    flecs_render_effect_bind_callback bind_callback;
    flecs_render_effect_render_callback render_callback;
    void *ctx;
    void (*free_ctx)(void *ctx);
});

int flecsEngine_initPassthrough(
    FlecsEngineImpl *impl);

int flecsEngine_initDepthResolve(
    FlecsEngineImpl *impl);

void flecsEngine_depthResolve(
    const FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder);

void flecsEngine_releaseMsaaResources(
    FlecsEngineImpl *impl);

int flecsEngine_initRenderer(
    ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_shader_register(
    ecs_world_t *world);

void flecsEngine_renderBatch_register(
    ecs_world_t *world);

void flecsEngine_renderEffect_register(
    ecs_world_t *world);

void flecsEngine_batchSets_register(
    ecs_world_t *world);

void flecsEngine_renderView_register(
    ecs_world_t *world);

void flecsEngine_ibl_register(
    ecs_world_t *world);

void flecsEngine_tonyMcMapFace_register(
    ecs_world_t *world);

void flecsEngine_bloom_register(
    ecs_world_t *world);

void flecsEngine_exponentialHeightFog_register(
    ecs_world_t *world);

void flecsEngine_ssao_register(
    ecs_world_t *world);

ecs_entity_t flecsEngine_shader_ensure(
    ecs_world_t *world,
    const char *name,
    const FlecsShader *shader);

void flecsEngine_ibl_ensureSkyBackground(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const flecs_engine_background_t *background);

void flecsEngine_renderView_extractAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_renderView_renderAll(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder);

void flecsEngine_material_uploadBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_material_releaseBuffer(
    FlecsEngineImpl *impl);

bool flecsEngine_ibl_initResources(
    FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl,
    const char *hdri_path,
    const flecs_rgba_t *sky_color,
    const flecs_rgba_t *ground_color,
    const flecs_rgba_t *haze_color,
    const flecs_rgba_t *horizon_color,
    uint32_t filter_sample_count,
    uint32_t lut_sample_count);

WGPUBindGroupLayout flecsEngine_ibl_ensureBindLayout(
    FlecsEngineImpl *impl);

bool flecsEngine_ibl_createRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl);

void flecsEngine_ibl_releaseResources(
    FlecsEngineImpl *impl);

void flecsEngine_renderBatch_render(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_extract(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    ecs_entity_t batch_entity);

void flecsEngine_renderView_renderBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    const FlecsRenderViewImpl *viewImpl,
    WGPUCommandEncoder encoder);

void flecsEngine_renderView_extractBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view);

void flecsEngine_renderEffect_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
    const WGPURenderPassEncoder pass,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *effect_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat output_format);

void flecsEngine_renderView_renderEffects(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *viewImpl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder);

const FlecsShaderImpl* flecsEngine_shader_ensureImpl(
    ecs_world_t *world,
    ecs_entity_t shader_entity);

FlecsDefaultAttrCache* flecsEngine_defaultAttrCache_create(void);

void flecsEngine_defaultAttrCache_free(
    FlecsDefaultAttrCache *ptr);

FlecsPbrMaterial* flecsEngine_defaultAttrCache_getMaterial(
    const FlecsEngineImpl *engine,
    int32_t count);

FlecsEmissive* flecsEngine_defaultAttrCache_getEmissive(
    const FlecsEngineImpl *engine,
    int32_t count);

FlecsRgba* flecsEngine_defaultAttrCache_getColor(
    const FlecsEngineImpl *engine,
    int32_t count);

void flecsEngine_setupLights(
    const ecs_world_t *world,
    FlecsEngineImpl *engine);

int flecsEngine_cluster_init(
    FlecsEngineImpl *impl);

void flecsEngine_cluster_cleanup(
    FlecsEngineImpl *impl);

bool flecsEngine_cluster_ensureLights(
    FlecsEngineImpl *engine, int32_t needed);

void flecsEngine_cluster_build(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view);

int flecsEngine_shadow_init(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t shadow_map_size);

void flecsEngine_shadow_cleanup(
    FlecsEngineImpl *impl);

int flecsEngine_shadow_ensureSize(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t shadow_map_size);

void flecsEngine_shadow_computeCascades(
    const ecs_world_t *world,
    const FlecsRenderView *view,
    uint32_t shadow_map_size,
    const uint32_t cascade_sizes[FLECS_ENGINE_SHADOW_CASCADE_COUNT],
    float max_range,
    mat4 out_light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT],
    float out_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT]);

void flecsEngine_renderBatch_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    ecs_entity_t batch_entity);

void flecsEngine_renderView_renderShadow(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    WGPUCommandEncoder encoder);

/* Shared ECS_DTOR + ECS_MOVE pair for types whose cleanup is a single
 * release function.  The destructor calls release_fn(ptr), and the move
 * hook releases the destination, shallow-copies the source, then zeroes
 * the source so its resources are not double-freed. */
#define FLECS_ENGINE_IMPL_HOOKS(Type, release_fn) \
    ECS_DTOR(Type, ptr, { release_fn(ptr); }) \
    ECS_MOVE(Type, dst, src, { \
        release_fn(dst); *dst = *src; ecs_os_zeromem(src); \
    })

/* Shared fullscreen-triangle vertex shader used by all post-process effects.
 * Produces a single triangle covering clip space with correct UVs. */
#define FLECS_ENGINE_FULLSCREEN_VS_WGSL \
    "struct VertexOutput {\n" \
    "  @builtin(position) pos : vec4<f32>,\n" \
    "  @location(0) uv : vec2<f32>\n" \
    "};\n" \
    "@vertex fn vs_main(@builtin(vertex_index) vid : u32) -> VertexOutput {\n" \
    "  var out : VertexOutput;\n" \
    "  var pos = array<vec2<f32>, 3>(\n" \
    "      vec2<f32>(-1.0, -1.0),\n" \
    "      vec2<f32>(3.0, -1.0),\n" \
    "      vec2<f32>(-1.0, 3.0));\n" \
    "  let p = pos[vid];\n" \
    "  out.pos = vec4<f32>(p, 0.0, 1.0);\n" \
    "  out.uv = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);\n" \
    "  return out;\n" \
    "}\n"

/* Create a WGPUShaderModule from a WGSL source string. */
WGPUShaderModule flecsEngine_createShaderModule(
    WGPUDevice device,
    const char *wgsl_source);

WGPUBindGroupLayout flecsEngine_pbr_texture_ensureBindLayout(
    FlecsEngineImpl *impl);

WGPUTexture flecsEngine_texture_loadFile(
    WGPUDevice device,
    WGPUQueue queue,
    const char *path);

WGPUTexture flecsEngine_texture_create1x1(
    WGPUDevice device,
    WGPUQueue queue,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a);

bool flecsEngine_pbr_texture_createBindGroup(
    FlecsEngineImpl *engine,
    WGPUTextureView albedo_view,
    WGPUTextureView emissive_view,
    WGPUTextureView roughness_view,
    WGPUTextureView normal_view,
    WGPUBindGroup *out_bind_group);

ecs_entity_t flecsEngine_createBatch_textured_mesh(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_transparent(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

void flecsEngine_texture_onSet(
    ecs_iter_t *it);

void flecsEngine_pbrTextures_onSet(
    ecs_iter_t *it);

// Import renderer module
void FlecsEngineRendererImport(
    ecs_world_t *world);


#undef ECS_META_IMPL

#endif
