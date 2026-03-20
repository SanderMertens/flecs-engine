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
    "  return f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - cos_theta, 5.0);\n" \
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
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  l : vec3<f32>,\n" \
    "  roughness : f32) -> f32 {\n" \
    "  let ndotv = max(dot(n, v), 0.0);\n" \
    "  let ndotl = max(dot(n, l), 0.0);\n" \
    "  let ggx_v = geometrySchlickGGX(ndotv, roughness);\n" \
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
    "  v : vec3<f32>,\n" \
    "  l : vec3<f32>,\n" \
    "  h : vec3<f32>,\n" \
    "  roughness : f32,\n" \
    "  f : vec3<f32>) -> vec3<f32> {\n" \
    "  let ndotv = max(dot(n, v), 0.0);\n" \
    "  let ndotl = max(dot(n, l), 0.0);\n" \
    "  let d = distributionGGX(n, h, roughness);\n" \
    "  let g = geometrySmith(n, v, l, roughness);\n" \
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
    "  light_active : bool) -> vec3<f32> {\n" \
    "  if (!light_active) {\n" \
    "    return vec3<f32>(0.0);\n" \
    "  }\n" \
    "  let ndotl = max(dot(n, l), 0.0);\n" \
    "  if (ndotl <= 0.0) {\n" \
    "    return vec3<f32>(0.0);\n" \
    "  }\n" \
    "  let f0 = computeF0(albedo, metallic);\n" \
    "  let f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n" \
    "  let diffuse = computeDiffuse(albedo, metallic, f);\n" \
    "  let specular = computeSpecular(n, v, l, h, roughness, f);\n" \
    "  return (diffuse + specular) * uniforms.light_color.rgb * ndotl;\n" \
    "}\n" \
    "fn computeAmbientLighting(albedo : vec3<f32>, metallic : f32) -> vec3<f32> {\n" \
    "  return albedo * 0.03 * (1.0 - metallic);\n" \
    "}\n" \
    "fn computePointLighting(\n" \
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32) -> vec3<f32> {\n" \
    "  var result = vec3<f32>(0.0);\n" \
    "  let count = i32(uniforms.point_light_info.x);\n" \
    "  for (var i = 0; i < count; i++) {\n" \
    "    let light_pos = uniforms.point_lights[i].position.xyz;\n" \
    "    let light_range = uniforms.point_lights[i].position.w;\n" \
    "    let light_color = uniforms.point_lights[i].color.rgb;\n" \
    "    let to_light = light_pos - world_pos;\n" \
    "    let dist = length(to_light);\n" \
    "    if (dist > light_range || dist < 0.001) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let l = to_light / dist;\n" \
    "    let h = getHalfVector(v, l);\n" \
    "    let ndotl = max(dot(n, l), 0.0);\n" \
    "    if (ndotl <= 0.0) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let attenuation = clamp(1.0 - pow(dist / light_range, 4.0), 0.0, 1.0) / (dist * dist + 1.0);\n" \
    "    let f0 = computeF0(albedo, metallic);\n" \
    "    let f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n" \
    "    let diffuse = computeDiffuse(albedo, metallic, f);\n" \
    "    let specular = computeSpecular(n, v, l, h, roughness, f);\n" \
    "    result += (diffuse + specular) * light_color * ndotl * attenuation;\n" \
    "  }\n" \
    "  return result;\n" \
    "}\n" \
    "fn computeSpotLighting(\n" \
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32) -> vec3<f32> {\n" \
    "  var result = vec3<f32>(0.0);\n" \
    "  let count = i32(uniforms.spot_light_info.x);\n" \
    "  for (var i = 0; i < count; i++) {\n" \
    "    let light_pos = uniforms.spot_lights[i].position.xyz;\n" \
    "    let light_range = uniforms.spot_lights[i].position.w;\n" \
    "    let light_dir = uniforms.spot_lights[i].direction.xyz;\n" \
    "    let outer_cos = uniforms.spot_lights[i].direction.w;\n" \
    "    let light_color = uniforms.spot_lights[i].color.rgb;\n" \
    "    let inner_cos = uniforms.spot_lights[i].color.w;\n" \
    "    let to_light = light_pos - world_pos;\n" \
    "    let dist = length(to_light);\n" \
    "    if (dist > light_range || dist < 0.001) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let l = to_light / dist;\n" \
    "    let theta = dot(l, -light_dir);\n" \
    "    if (theta < outer_cos) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let h = getHalfVector(v, l);\n" \
    "    let ndotl = max(dot(n, l), 0.0);\n" \
    "    if (ndotl <= 0.0) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let spot_effect = clamp((theta - outer_cos) / max(inner_cos - outer_cos, 1e-4), 0.0, 1.0);\n" \
    "    let attenuation = clamp(1.0 - pow(dist / light_range, 4.0), 0.0, 1.0) / (dist * dist + 1.0);\n" \
    "    let f0 = computeF0(albedo, metallic);\n" \
    "    let f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n" \
    "    let diffuse = computeDiffuse(albedo, metallic, f);\n" \
    "    let specular = computeSpecular(n, v, l, h, roughness, f);\n" \
    "    result += (diffuse + specular) * light_color * ndotl * attenuation * spot_effect;\n" \
    "  }\n" \
    "  return result;\n" \
    "}\n"

#endif
