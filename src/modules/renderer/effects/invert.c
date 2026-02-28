#include "../renderer.h"
#include "flecs_engine.h"

static const char *kShaderSource =
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>\n"
    "};\n"
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
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var tony_lut : texture_3d<f32>;\n"
    "@group(0) @binding(3) var tony_lut_sampler : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  return vec4<f32>(vec3<f32>(1.0) - src.rgb, src.a);\n"
    "}\n";

static ecs_entity_t flecsRenderEffect_invert_shader(
    ecs_world_t *world)
{
    return flecsEngineEnsureShader(world, "InvertPostShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static bool flecsRenderEffect_invert_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)world;
    (void)engine;
    (void)effect;
    (void)impl;
    (void)layout_entries;

    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);
    return true;
}

static bool flecsRenderEffect_invert_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)world;
    (void)engine;
    (void)effect;
    (void)impl;
    (void)entries;

    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);
    return true;
}

ecs_entity_t flecsEngine_createEffect_invert(
    ecs_world_t *world,
    int32_t input)
{
    ecs_entity_t effect = ecs_new(world);
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsRenderEffect_invert_shader(world),
        .input = input,
        .setup_callback = flecsRenderEffect_invert_setup,
        .bind_callback = flecsRenderEffect_invert_bind
    });

    return effect;
}
