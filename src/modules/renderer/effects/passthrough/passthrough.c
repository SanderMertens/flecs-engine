#include "../../renderer.h"
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
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  return textureSample(input_texture, input_sampler, in.uv);\n"
    "}\n";

static ecs_entity_t flecsRenderEffect_passthrough_shader(
    ecs_world_t *world)
{
    return flecsEngineEnsureShader(world, "PassthroughPostShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

ecs_entity_t flecsEngine_createEffect_passthrough(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input)
{
    ecs_entity_t effect = ecs_entity(world, { .parent = parent, .name = name });
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsRenderEffect_passthrough_shader(world),
        .input = input
    });

    return effect;
}
