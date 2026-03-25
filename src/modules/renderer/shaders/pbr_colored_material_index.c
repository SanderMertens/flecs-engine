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
    "struct GpuMaterial {\n"
    "  color : u32,\n"
    "  metallic : f32,\n"
    "  roughness : f32,\n"
    "  emissive_strength : f32,\n"
    "  emissive_color : u32\n"
    "};\n"
    "@group(0) @binding(1) var<storage, read> materials : array<GpuMaterial>;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) nrm : vec3<f32>,\n"
    "  @location(2) m0 : vec3<f32>,\n"
    "  @location(3) m1 : vec3<f32>,\n"
    "  @location(4) m2 : vec3<f32>,\n"
    "  @location(5) m3 : vec3<f32>,\n"
    "  @location(6) material_id : u32\n"
    "};\n"
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) normal : vec3<f32>,\n"
    "  @location(1) world_pos : vec3<f32>,\n"
    "  @location(2) @interpolate(flat) material_id : u32\n"
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
    "  out.material_id = input.material_id;\n"
    "  return out;\n"
    "}\n"
    FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL
    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let material = materials[input.material_id];\n"
    "  let color = unpack4x8unorm(material.color);\n"
    "  let em_color = unpack4x8unorm(material.emissive_color).rgb;\n"
    "  let has_em_color = dot(em_color, em_color) > 0.0;\n"
    "  let em_base = select(color.rgb, em_color, has_em_color);\n"
    "  let emissive = em_base * max(material.emissive_strength, 0.0);\n"
    "  let lit = computePbrLighting(\n"
    "    color.rgb,\n"
    "    material.metallic,\n"
    "    material.roughness,\n"
    "    emissive,\n"
    "    input.world_pos,\n"
    "    input.normal,\n"
    "    input.pos);\n"
    "  return vec4<f32>(lit, color.a);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_pbrColoredMaterialIndex(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "PbrColoredMaterialIndexShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
