#include "../../renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsExponentialHeightFog);
ECS_COMPONENT_DECLARE(FlecsExponentialHeightFogImpl);

static float flecsFogChannelToFloat(
    uint8_t value)
{
    return (float)value / 255.0f;
}

typedef struct FlecsExponentialHeightFogUniform {
    mat4 inv_vp;
    float camera_pos[4];
    float fog_color_density[4];
    float fog_params[4];
} FlecsExponentialHeightFogUniform;

static const char *kShaderSource =
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>\n"
    "};\n"
    "struct FogUniforms {\n"
    "  inv_vp : mat4x4<f32>,\n"
    "  camera_pos : vec4<f32>,\n"
    "  fog_color_density : vec4<f32>,\n"
    "  fog_params : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> uniforms : FogUniforms;\n"
    "@vertex fn vs_main(@builtin(vertex_index) vid : u32) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  var pos = array<vec2<f32>, 3>(\n"
    "      vec2<f32>(-1.0, -1.0),\n"
    "      vec2<f32>(3.0, -1.0),\n"
    "      vec2<f32>(-1.0, 3.0));\n"
    "  let p = pos[vid];\n"
    "  out.pos = vec4<f32>(p, 0.0, 1.0);\n"
    "  out.uv = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);\n"
    "  return out;\n"
    "}\n"
    "fn reconstruct_world_pos(uv : vec2<f32>, depth : f32) -> vec3<f32> {\n"
    "  let ndc = vec4<f32>(\n"
    "    uv.x * 2.0 - 1.0,\n"
    "    (1.0 - uv.y) * 2.0 - 1.0,\n"
    "    depth,\n"
    "    1.0);\n"
    "  let world_h = uniforms.inv_vp * ndc;\n"
    "  if (abs(world_h.w) > 1e-6) {\n"
    "    return world_h.xyz / world_h.w;\n"
    "  }\n"
    "  return world_h.xyz;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.999999));\n"
    "  let texel = vec2<i32>(clamped_uv * dims_f);\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  if (depth >= 0.999999) {\n"
    "    return src;\n"
    "  }\n"
    "  let world_pos = reconstruct_world_pos(in.uv, depth);\n"
    "  let ray = world_pos - uniforms.camera_pos.xyz;\n"
    "  let distance = length(ray);\n"
    "  if (distance <= 1e-6) {\n"
    "    return src;\n"
    "  }\n"
    "  let density = max(uniforms.fog_color_density.w, 0.0);\n"
    "  let falloff = max(uniforms.fog_params.x, 1e-6);\n"
    "  let base_height = uniforms.fog_params.y;\n"
    "  let max_opacity = clamp(uniforms.fog_params.z, 0.0, 1.0);\n"
    "  let camera_height = uniforms.camera_pos.y - base_height;\n"
    "  let world_height = world_pos.y - base_height;\n"
    "  let e0 = exp(clamp(-falloff * camera_height, -80.0, 80.0));\n"
    "  let e1 = exp(clamp(-falloff * world_height, -80.0, 80.0));\n"
    "  let dy = world_pos.y - uniforms.camera_pos.y;\n"
    "  var integral = distance * sqrt(max(e0 * e1, 0.0));\n"
    "  if (abs(dy) > 1e-5) {\n"
    "    integral = distance * (e0 - e1) / (falloff * dy);\n"
    "  }\n"
    "  let optical_depth = max(0.0, density * max(integral, 0.0));\n"
    "  let fog_factor = min(max_opacity, 1.0 - exp(clamp(-optical_depth, -80.0, 0.0)));\n"
    "  let fogged = mix(src.rgb, uniforms.fog_color_density.rgb, fog_factor);\n"
    "  return vec4<f32>(fogged, src.a);\n"
    "}\n";

static ecs_entity_t flecsRenderEffect_exponentialHeightFog_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "ExponentialHeightFogShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsExponentialHeightFogReleaseResources(
    FlecsExponentialHeightFogImpl *impl)
{
    if (impl->uniform_buffer) {
        wgpuBufferRelease(impl->uniform_buffer);
        impl->uniform_buffer = NULL;
    }
}

ECS_DTOR(FlecsExponentialHeightFogImpl, ptr, {
    flecsExponentialHeightFogReleaseResources(ptr);
})

ECS_MOVE(FlecsExponentialHeightFogImpl, dst, src, {
    flecsExponentialHeightFogReleaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsExponentialHeightFogFillUniform(
    const ecs_world_t *world,
    ecs_entity_t effect_entity,
    const FlecsExponentialHeightFog *fog,
    FlecsExponentialHeightFogUniform *uniform)
{
    glm_mat4_identity(uniform->inv_vp);

    uniform->camera_pos[0] = 0.0f;
    uniform->camera_pos[1] = 0.0f;
    uniform->camera_pos[2] = 0.0f;
    uniform->camera_pos[3] = 1.0f;

    uniform->fog_color_density[0] = flecsFogChannelToFloat(fog->color.r);
    uniform->fog_color_density[1] = flecsFogChannelToFloat(fog->color.g);
    uniform->fog_color_density[2] = flecsFogChannelToFloat(fog->color.b);
    uniform->fog_color_density[3] = fog->density;

    uniform->fog_params[0] = fog->falloff;
    uniform->fog_params[1] = fog->base_height;
    uniform->fog_params[2] = fog->max_opacity;
    uniform->fog_params[3] = 0.0f;

    ecs_entity_t view_entity = ecs_get_target(world, effect_entity, EcsChildOf, 0);
    if (!view_entity) {
        return;
    }

    const FlecsRenderView *view = ecs_get(world, view_entity, FlecsRenderView);
    if (!view || !view->camera) {
        return;
    }

    const FlecsCameraImpl *camera = ecs_get(world, view->camera, FlecsCameraImpl);
    if (!camera) {
        return;
    }

    mat4 mvp;
    glm_mat4_copy((vec4*)camera->mvp, mvp);
    glm_mat4_inv(mvp, uniform->inv_vp);

    const FlecsWorldTransform3 *camera_transform = ecs_get(
        world, view->camera, FlecsWorldTransform3);
    if (camera_transform) {
        uniform->camera_pos[0] = camera_transform->m[3][0];
        uniform->camera_pos[1] = camera_transform->m[3][1];
        uniform->camera_pos[2] = camera_transform->m[3][2];
    }
}

static bool flecsRenderEffect_exponentialHeightFog_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)world;
    (void)effect_entity;
    (void)effect;
    (void)effect_impl;

    ecs_assert(layout_entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    FlecsExponentialHeightFogImpl fog_impl = {0};

    WGPUBufferDescriptor uniform_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(FlecsExponentialHeightFogUniform)
    };

    fog_impl.uniform_buffer = wgpuDeviceCreateBuffer(engine->device, &uniform_desc);
    if (!fog_impl.uniform_buffer) {
        return false;
    }

    layout_entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = false
        }
    };

    layout_entries[3] = (WGPUBindGroupLayoutEntry){
        .binding = 3,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(FlecsExponentialHeightFogUniform)
        }
    };

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsExponentialHeightFogImpl, &fog_impl);

    *entry_count = 4;
    return true;
}

static bool flecsRenderEffect_exponentialHeightFog_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)effect;
    (void)impl;

    ecs_assert(entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    if (!engine->depth_texture_view) {
        return false;
    }

    const FlecsExponentialHeightFog *fog = ecs_get(
        world, effect_entity, FlecsExponentialHeightFog);
    const FlecsExponentialHeightFogImpl *fog_impl = ecs_get(
        world, effect_entity, FlecsExponentialHeightFogImpl);
    if (!fog || !fog_impl || !fog_impl->uniform_buffer) {
        return false;
    }

    FlecsExponentialHeightFogUniform uniform = {0};
    flecsExponentialHeightFogFillUniform(world, effect_entity, fog, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        fog_impl->uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2,
        .textureView = engine->depth_texture_view
    };

    entries[3] = (WGPUBindGroupEntry){
        .binding = 3,
        .buffer = fog_impl->uniform_buffer,
        .offset = 0,
        .size = sizeof(FlecsExponentialHeightFogUniform)
    };

    *entry_count = 4;
    return true;
}

FlecsExponentialHeightFog flecsEngine_exponentialHeightFogSettingsDefault(void)
{
    return (FlecsExponentialHeightFog){
        .density = 0.1f,
        .falloff = 0.3f,
        .base_height = 0.0f,
        .max_opacity = 1.0f,
        .color = {191, 158, 140, 255}
    };
}

ecs_entity_t flecsEngine_createEffect_exponentialHeightFog(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsExponentialHeightFog *settings)
{
    ecs_entity_t effect = ecs_entity(world, { .parent = parent, .name = name });

    FlecsExponentialHeightFog fog = settings
        ? *settings
        : flecsEngine_exponentialHeightFogSettingsDefault();

    ecs_set_ptr(world, effect, FlecsExponentialHeightFog, &fog);
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsRenderEffect_exponentialHeightFog_shader(world),
        .input = input,
        .setup_callback = flecsRenderEffect_exponentialHeightFog_setup,
        .bind_callback = flecsRenderEffect_exponentialHeightFog_bind
    });

    return effect;
}

void flecsEngine_exponentialHeightFog_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsExponentialHeightFog);
    ECS_COMPONENT_DEFINE(world, FlecsExponentialHeightFogImpl);

    ecs_set_hooks(world, FlecsExponentialHeightFogImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsExponentialHeightFogImpl),
        .dtor = ecs_dtor(FlecsExponentialHeightFogImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsExponentialHeightFog),
        .members = {
            { .name = "density", .type = ecs_id(ecs_f32_t) },
            { .name = "falloff", .type = ecs_id(ecs_f32_t) },
            { .name = "base_height", .type = ecs_id(ecs_f32_t) },
            { .name = "max_opacity", .type = ecs_id(ecs_f32_t) },
            { .name = "color", .type = ecs_id(flecs_rgba_t) }
        }
    });
}
