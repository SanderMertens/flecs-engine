#include "shaders.h"
#include "../renderer.h"
#include "common/uniforms_wgsl.h"
#include "common/shared_vertex_wgsl.h"
#include "common/cluster_wgsl.h"
#include "common/pbr_functions_wgsl.h"
#include "common/shadow_wgsl.h"
#include "common/pbr_lighting_textured_wgsl.h"

static const char *kShaderSource =
    FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL
    "@group(1) @binding(0) var ibl_prefiltered_env : texture_cube<f32>;\n"
    "@group(1) @binding(1) var ibl_sampler : sampler;\n"
    "@group(1) @binding(2) var ibl_brdf_lut : texture_2d<f32>;\n"
    FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL

    FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL

    /* PBR texture bindings at group 2 */
    "@group(2) @binding(0) var albedo_tex : texture_2d<f32>;\n"
    "@group(2) @binding(1) var emissive_tex : texture_2d<f32>;\n"
    "@group(2) @binding(2) var roughness_tex : texture_2d<f32>;\n"
    "@group(2) @binding(3) var normal_tex : texture_2d<f32>;\n"
    "@group(2) @binding(4) var tex_sampler : sampler;\n"

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
    "  @location(2) uv : vec2<f32>,\n"
    "  @location(3) m0 : vec3<f32>,\n"
    "  @location(4) m1 : vec3<f32>,\n"
    "  @location(5) m2 : vec3<f32>,\n"
    "  @location(6) m3 : vec3<f32>,\n"
    "  @location(7) material_id : u32\n"
    "};\n"

    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) normal : vec3<f32>,\n"
    "  @location(1) world_pos : vec3<f32>,\n"
    "  @location(2) uv : vec2<f32>,\n"
    "  @location(3) @interpolate(flat) material_id : u32\n"
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
    "  out.uv = input.uv;\n"
    "  out.material_id = input.material_id;\n"
    "  return out;\n"
    "}\n"

    FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_TEXTURED_WGSL

    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let material = materials[input.material_id];\n"
    "  let base_color = textureSample(albedo_tex, tex_sampler, input.uv);\n"
    "  let mat_color = unpack4x8unorm(material.color);\n"
    "  let albedo = base_color.rgb * mat_color.rgb;\n"
    "\n"
    "  let mr = textureSample(roughness_tex, tex_sampler, input.uv);\n"
    "  let roughness = mr.g * material.roughness;\n"
    "  let metallic = mr.b * material.metallic;\n"
    "\n"
    "  let emissive_sample = textureSample(emissive_tex, tex_sampler, input.uv).rgb;\n"
    "  let em_color = unpack4x8unorm(material.emissive_color).rgb;\n"
    "  let emissive = emissive_sample * em_color * max(material.emissive_strength, 0.0);\n"
    "\n"
    "  let normal_sample = textureSample(normal_tex, tex_sampler, input.uv).rgb;\n"
    "  let tangent_normal = normal_sample * 2.0 - 1.0;\n"
    "\n"
    "  let N = normalize(input.normal);\n"
    "  let dp1 = dpdx(input.world_pos);\n"
    "  let dp2 = dpdy(input.world_pos);\n"
    "  let duv1 = dpdx(input.uv);\n"
    "  let duv2 = dpdy(input.uv);\n"
    "  let dp2perp = cross(dp2, N);\n"
    "  let dp1perp = cross(N, dp1);\n"
    "  let T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
    "  let B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
    "  let invmax = inverseSqrt(max(dot(T, T), dot(B, B)));\n"
    "  let tbn = mat3x3<f32>(T * invmax, B * invmax, N);\n"
    "  let mapped_normal = normalize(tbn * tangent_normal);\n"
    "\n"
    "  let lit = computePbrLightingTextured(\n"
    "    albedo,\n"
    "    metallic,\n"
    "    roughness,\n"
    "    emissive,\n"
    "    input.world_pos,\n"
    "    mapped_normal,\n"
    "    input.pos);\n"
    "  return vec4<f32>(lit, base_color.a);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_pbrTextured(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "PbrTexturedShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
