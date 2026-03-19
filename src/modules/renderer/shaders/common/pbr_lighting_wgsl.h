#ifndef FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL \
    "fn computePbrLighting(\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic_raw : f32,\n" \
    "  roughness_raw : f32,\n" \
    "  emissive_strength : f32,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  normal : vec3<f32>\n" \
    ") -> vec3<f32> {\n" \
    "  let metallic = clamp(metallic_raw, 0.0, 1.0);\n" \
    "  let roughness = clamp(roughness_raw, 0.0, 1.0);\n" \
    "  let direct_roughness = clamp(roughness, 0.045, 1.0);\n" \
    "  let n = normalize(normal);\n" \
    "  let light = getLightDirection();\n" \
    "  let v = getViewDirection(world_pos);\n" \
    "  let h = getHalfVector(v, light.dir);\n" \
    "  let shadow_result = computeShadow(world_pos);\n" \
    "  let shadow = shadow_result.shadow;\n" \
    "  let direct = computeDirectLighting(\n" \
    "    n, v, light.dir, h,\n" \
    "    albedo, metallic, direct_roughness,\n" \
    "    light.enabled) * shadow;\n" \
    "  let ambient = computeAmbientLighting(albedo, metallic);\n" \
    "  let f0 = computeF0(albedo, metallic);\n" \
    "  let ndotv = saturate(dot(n, v));\n" \
    "  let r = reflect(-v, n);\n" \
    "  let max_mip = max(f32(textureNumLevels(ibl_prefiltered_env)) - 1.0, 0.0);\n" \
    "  let lod = roughness * max_mip;\n" \
    "  let prefiltered_color = textureSampleLevel(ibl_prefiltered_env, ibl_sampler, r, lod).rgb;\n" \
    "  let brdf = textureSample(ibl_brdf_lut, ibl_sampler, vec2<f32>(ndotv, roughness)).rg;\n" \
    "  let specular_ibl = prefiltered_color * computeSplitSumSpecularTerm(f0, brdf);\n" \
    "  let emissive = albedo * max(emissive_strength, 0.0);\n" \
    "  return ambient + direct + specular_ibl + emissive;\n" \
    "}\n"

#endif
