/*
 * Gamma correction effect for emscripten.
 *
 * Browser WebGPU canvas only supports bgra8unorm (not sRGB). The canvas
 * compositor expects sRGB-encoded values, but the render pipeline outputs
 * linear values. This effect applies linear->sRGB conversion (pow 1/2.2).
 *
 * On native platforms the surface already uses an sRGB format, so this
 * effect should be disabled.
 */

#include "../../renderer.h"
#include "flecs_engine.h"

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let c = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let gamma = vec3<f32>(1.0 / 2.2);\n"
    "  return vec4<f32>(pow(c.rgb, gamma), c.a);\n"
    "}\n";

static ecs_entity_t flecsEngine_gammaCorrect_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "GammaCorrectPostShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

ecs_entity_t flecsEngine_createEffect_gammaCorrect(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input)
{
    ecs_entity_t effect = ecs_entity(world, { .parent = parent, .name = name });
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsEngine_gammaCorrect_shader(world),
        .input = input
    });

    return effect;
}
