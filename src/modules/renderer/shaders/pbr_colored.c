#include "shaders.h"
#include "../renderer.h"
#include "common/uniforms_wgsl.h"
#include "common/shared_vertex_wgsl.h"
#include "common/cluster_wgsl.h"
#include "common/pbr_functions_wgsl.h"
#include "common/shadow_wgsl.h"
#include "common/pbr_lighting_wgsl.h"

static const char *kShaderSource =
    FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL
    "@group(1) @binding(0) var ibl_prefiltered_env : texture_cube<f32>;\n"
    "@group(1) @binding(1) var ibl_sampler : sampler;\n"
    "@group(1) @binding(2) var ibl_brdf_lut : texture_2d<f32>;\n"
    FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) nrm : vec3<f32>,\n"
    "  @location(2) m0 : vec3<f32>,\n"
    "  @location(3) m1 : vec3<f32>,\n"
    "  @location(4) m2 : vec3<f32>,\n"
    "  @location(5) m3 : vec3<f32>,\n"
    "  @location(6) color : vec4<f32>,\n"
    "  @location(7) metallic : f32,\n"
    "  @location(8) roughness : f32,\n"
    "  @location(9) emissive_strength : f32\n"
    "};\n"
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) color : vec4<f32>,\n"
    "  @location(1) normal : vec3<f32>,\n"
    "  @location(2) world_pos : vec3<f32>,\n"
    "  @location(3) metallic : f32,\n"
    "  @location(4) roughness : f32,\n"
    "  @location(5) emissive_strength : f32\n"
    "};\n"
    FLECS_ENGINE_SHADER_COMMON_SHARED_VERTEX_WGSL
    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  let vertex = buildPbrVertexState(\n"
    "    input.pos,\n"
    "    input.nrm,\n"
    "    input.m0,\n"
    "    input.m1,\n"
    "    input.m2,\n"
    "    input.m3);\n"
    "  out.pos = vertex.clip_pos;\n"
    "  out.normal = vertex.world_normal;\n"
    "  out.world_pos = vertex.world_pos;\n"
    "  out.metallic = input.metallic;\n"
    "  out.roughness = input.roughness;\n"
    "  out.emissive_strength = input.emissive_strength;\n"
    "  out.color = input.color;\n"
    "  return out;\n"
    "}\n"
    FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL
    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let lit = computePbrLighting(\n"
    "    input.color.rgb,\n"
    "    input.metallic,\n"
    "    input.roughness,\n"
    "    input.emissive_strength,\n"
    "    input.world_pos,\n"
    "    input.normal,\n"
    "    input.pos);\n"
    "  return vec4<f32>(lit, input.color.a);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_pbrColored(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "PbrColoredShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
