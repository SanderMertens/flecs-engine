#include "../../types.h"

#ifndef FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_RENDERER_IMPL_IMPL
#define ECS_META_IMPL EXTERN
#endif

struct FlecsRenderBatch;
struct FlecsRenderEffect;

typedef void (*flecs_render_batch_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
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

ecs_entity_t flecsEngine_shader_ensure(
    ecs_world_t *world,
    const char *name,
    const FlecsShader *shader);

void flecsEngineRenderViews(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder);

void flecsEngineUploadMaterialBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngineReleaseMaterialBuffer(
    FlecsEngineImpl *impl);

bool flecsEngineInitIblResources(
    FlecsEngineImpl *engine,
    FlecHdriImpl *ibl,
    const char *hdri_path);

WGPUBindGroupLayout flecsEngineEnsureIblBindLayout(
    FlecsEngineImpl *impl);

void flecsEngineReleaseIblResources(
    FlecsEngineImpl *impl);

void flecsEngineReleaseAllIblRuntimeResources(
    ecs_world_t *world);

void flecsEngineReleaseEffectTargets(
    FlecsEngineImpl *impl);

WGPUColor flecsEngineGetClearColor(
    const FlecsEngineImpl *impl);

void flecsEngineGetClearColorVec4(
    const FlecsEngineImpl *impl,
    float out[4]);

void flecsEngineRenderViewsWithEffects(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder);

void flecsEngineRenderView(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    const WGPURenderPassEncoder pass,
    ecs_entity_t view_entity,
    const FlecsRenderView *view);

void flecsEngineRenderBatch(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    ecs_entity_t batch_entity);

void flecsEngineRenderEffect(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
    const WGPURenderPassEncoder pass,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *effect_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat output_format);

const FlecsShaderImpl* flecsEngine_shader_ensureImpl(
    ecs_world_t *world,
    ecs_entity_t shader_entity);

void FlecsEngineRendererImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

#endif
