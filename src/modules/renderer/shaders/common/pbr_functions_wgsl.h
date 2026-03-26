#ifndef FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL \
    "const PI : f32 = 3.141592653589793;\n" \
    "fn saturate(v : f32) -> f32 {\n" \
    "  return clamp(v, 0.0, 1.0);\n" \
    "}\n" \
    "fn computeSplitSumSpecularTerm(f0 : vec3<f32>, brdf : vec2<f32>) -> vec3<f32> {\n" \
    "  return f0 * brdf.x + vec3<f32>(brdf.y);\n" \
    "}\n" \
    "fn fresnelSchlick(cos_theta : f32, f0 : vec3<f32>) -> vec3<f32> {\n" \
    "  let x = 1.0 - cos_theta;\n" \
    "  let x2 = x * x;\n" \
    "  return f0 + (vec3<f32>(1.0) - f0) * (x2 * x2 * x);\n" \
    "}\n" \
    "fn distributionGGX(n : vec3<f32>, h : vec3<f32>, roughness : f32) -> f32 {\n" \
    "  let a = roughness * roughness;\n" \
    "  let a2 = a * a;\n" \
    "  let ndoth = max(dot(n, h), 0.0);\n" \
    "  let ndoth2 = ndoth * ndoth;\n" \
    "  let denom = ndoth2 * (a2 - 1.0) + 1.0;\n" \
    "  return a2 / max(PI * denom * denom, 1e-4);\n" \
    "}\n" \
    "fn geometrySchlickGGX(ndotv : f32, roughness : f32) -> f32 {\n" \
    "  let r = roughness + 1.0;\n" \
    "  let k = (r * r) / 8.0;\n" \
    "  return ndotv / max(ndotv * (1.0 - k) + k, 1e-4);\n" \
    "}\n" \
    "fn geometrySmith(\n" \
    "  ggx_v : f32,\n" \
    "  ndotl : f32,\n" \
    "  roughness : f32) -> f32 {\n" \
    "  let ggx_l = geometrySchlickGGX(ndotl, roughness);\n" \
    "  return ggx_v * ggx_l;\n" \
    "}\n" \
    "struct DirectionSample {\n" \
    "  dir : vec3<f32>,\n" \
    "  enabled : bool\n" \
    "};\n" \
    "fn safeNormalize(v : vec3<f32>, fallback : vec3<f32>) -> vec3<f32> {\n" \
    "  let len2 = dot(v, v);\n" \
    "  return normalize(select(fallback, v, len2 > 1e-6));\n" \
    "}\n" \
    "fn getLightDirection() -> DirectionSample {\n" \
    "  let light_vec = -uniforms.light_ray_dir.xyz;\n" \
    "  let light_enabled = dot(light_vec, light_vec) > 1e-6;\n" \
    "  return DirectionSample(safeNormalize(light_vec, vec3<f32>(0.0, 1.0, 0.0)), light_enabled);\n" \
    "}\n" \
    "fn getViewDirection(world_pos : vec3<f32>) -> vec3<f32> {\n" \
    "  let view_vec = uniforms.camera_pos.xyz - world_pos;\n" \
    "  return safeNormalize(view_vec, vec3<f32>(0.0, 0.0, 1.0));\n" \
    "}\n" \
    "fn getHalfVector(v : vec3<f32>, l : vec3<f32>) -> vec3<f32> {\n" \
    "  return safeNormalize(v + l, v);\n" \
    "}\n" \
    "fn computeF0(albedo : vec3<f32>, metallic : f32) -> vec3<f32> {\n" \
    "  return mix(vec3<f32>(0.04), albedo, metallic);\n" \
    "}\n" \
    "fn computeDiffuse(albedo : vec3<f32>, metallic : f32, f : vec3<f32>) -> vec3<f32> {\n" \
    "  let kd = (vec3<f32>(1.0) - f) * (1.0 - metallic);\n" \
    "  return kd * albedo / PI;\n" \
    "}\n" \
    "fn computeSpecular(\n" \
    "  n : vec3<f32>,\n" \
    "  ndotv : f32,\n" \
    "  ggx_v : f32,\n" \
    "  l : vec3<f32>,\n" \
    "  h : vec3<f32>,\n" \
    "  roughness : f32,\n" \
    "  f : vec3<f32>) -> vec3<f32> {\n" \
    "  let ndotl = max(dot(n, l), 0.0);\n" \
    "  let d = distributionGGX(n, h, roughness);\n" \
    "  let g = geometrySmith(ggx_v, ndotl, roughness);\n" \
    "  let spec_num = d * g * f;\n" \
    "  let spec_denom = max(4.0 * ndotv * ndotl, 1e-4);\n" \
    "  return spec_num / spec_denom;\n" \
    "}\n" \
    "fn computeDirectLighting(\n" \
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  l : vec3<f32>,\n" \
    "  h : vec3<f32>,\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  f0 : vec3<f32>,\n" \
    "  ndotv : f32,\n" \
    "  ggx_v : f32,\n" \
    "  light_active : bool) -> vec3<f32> {\n" \
    "  if (!light_active) {\n" \
    "    return vec3<f32>(0.0);\n" \
    "  }\n" \
    "  let ndotl = max(dot(n, l), 0.0);\n" \
    "  if (ndotl <= 0.0) {\n" \
    "    return vec3<f32>(0.0);\n" \
    "  }\n" \
    "  let f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n" \
    "  let diffuse = computeDiffuse(albedo, metallic, f);\n" \
    "  let specular = computeSpecular(n, ndotv, ggx_v, l, h, roughness, f);\n" \
    "  return (diffuse + specular) * uniforms.light_color.rgb * ndotl;\n" \
    "}\n" \
    "fn computeClusterLighting(\n" \
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  f0 : vec3<f32>,\n" \
    "  ndotv : f32,\n" \
    "  ggx_v : f32,\n" \
    "  cluster_idx : u32) -> vec3<f32> {\n" \
    "  var result = vec3<f32>(0.0);\n" \
    "  let entry = cluster_grid[cluster_idx];\n" \
    "  for (var j = 0u; j < entry.light_count; j++) {\n" \
    "    let i = light_indices[entry.light_offset + j];\n" \
    "    let light_pos = lights[i].position.xyz;\n" \
    "    let light_range = lights[i].position.w;\n" \
    "    let light_dir = lights[i].direction.xyz;\n" \
    "    let outer_cos = lights[i].direction.w;\n" \
    "    let light_color = lights[i].color.rgb;\n" \
    "    let inner_cos = lights[i].color.w;\n" \
    "    let to_light = light_pos - world_pos;\n" \
    "    let dist = length(to_light);\n" \
    "    if (dist > light_range || dist < 0.001) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let l = to_light / dist;\n" \
    "    var spot_effect = 1.0;\n" \
    "    if (outer_cos > -1.5) {\n" \
    "      let theta = dot(l, -light_dir);\n" \
    "      if (theta < outer_cos) {\n" \
    "        continue;\n" \
    "      }\n" \
    "      spot_effect = clamp((theta - outer_cos) / max(inner_cos - outer_cos, 1e-4), 0.0, 1.0);\n" \
    "    }\n" \
    "    let h = getHalfVector(v, l);\n" \
    "    let ndotl = max(dot(n, l), 0.0);\n" \
    "    if (ndotl <= 0.0) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let ratio = dist / light_range;\n" \
    "    let r2 = ratio * ratio;\n" \
    "    let attenuation = clamp(1.0 - r2 * r2, 0.0, 1.0) / (dist * dist + 1.0);\n" \
    "    let f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n" \
    "    let diffuse = computeDiffuse(albedo, metallic, f);\n" \
    "    let specular = computeSpecular(n, ndotv, ggx_v, l, h, roughness, f);\n" \
    "    result += (diffuse + specular) * light_color * ndotl * attenuation * spot_effect;\n" \
    "  }\n" \
    "  return result;\n" \
    "}\n"

#endif
