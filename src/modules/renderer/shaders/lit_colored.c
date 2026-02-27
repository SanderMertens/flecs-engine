#include "shaders.h"
#include "../renderer.h"

static const char *kShaderSource =
    "struct Uniforms { vp : mat4x4<f32> }\n"
    "@group(0) @binding(0) var<uniform> uniforms : Uniforms;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) nrm : vec3<f32>,\n"
    "  @location(2) m0 : vec4<f32>,\n"
    "  @location(3) m1 : vec4<f32>,\n"
    "  @location(4) m2 : vec4<f32>,\n"
    "  @location(5) m3 : vec4<f32>,\n"
    "  @location(6) color : vec4<f32>,\n"
    "  @location(7) size : vec3<f32>\n"
    "};\n"
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) color : vec4<f32>,\n"
    "  @location(1) normal : vec3<f32>\n"
    "};\n"
    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  let sm0 = input.m0 * input.size.x;\n"
    "  let sm1 = input.m1 * input.size.y;\n"
    "  let sm2 = input.m2 * input.size.z;\n"
    "  let model = mat4x4<f32>(sm0, sm1, sm2, input.m3);\n"
    "  let model3 = mat3x3<f32>(sm0.xyz, sm1.xyz, sm2.xyz);\n"
    "  let c0 = model3[0];\n"
    "  let c1 = model3[1];\n"
    "  let c2 = model3[2];\n"
    "  let normal_matrix = mat3x3<f32>(\n"
    "    cross(c1, c2),\n"
    "    cross(c2, c0),\n"
    "    cross(c0, c1)\n"
    "  );\n"
    "  let world_pos = model * vec4<f32>(input.pos, 1.0);\n"
    "  out.pos = uniforms.vp * world_pos;\n"
    "  out.normal = normalize(normal_matrix * input.nrm);\n"
    "  out.color = input.color;\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let light = normalize(vec3<f32>(0.4, 0.8, 0.2));\n"
    "  let diffuse = max(dot(normalize(input.normal), light), 0.0);\n"
    "  return vec4<f32>(input.color.rgb * diffuse, input.color.a);\n"
    "}\n";

ecs_entity_t flecsEngineShader_litColored(
    ecs_world_t *world)
{
    return flecsEngineEnsureShader(world, "LitColoredhader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
