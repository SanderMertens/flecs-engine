#ifndef FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL \
    "@group(1) @binding(3) var shadow_map : texture_depth_2d_array;\n" \
    "@group(1) @binding(4) var shadow_sampler : sampler_comparison;\n" \
    "struct ShadowResult {\n" \
    "  shadow : f32,\n" \
    "  debug_color : vec3<f32>\n" \
    "};\n" \
    "fn cascadeDebugColor(cascade : i32) -> vec3<f32> {\n" \
    "  if (cascade == 0) { return vec3<f32>(1.0, 0.2, 0.2); }\n" \
    "  if (cascade == 1) { return vec3<f32>(0.2, 1.0, 0.2); }\n" \
    "  if (cascade == 2) { return vec3<f32>(0.2, 0.2, 1.0); }\n" \
    "  return vec3<f32>(1.0, 1.0, 0.2);\n" \
    "}\n" \
    "fn sampleShadowCascade(world_pos : vec3<f32>, cascade : i32) -> f32 {\n" \
    "  let light_clip = uniforms.light_vp[cascade] * vec4<f32>(world_pos, 1.0);\n" \
    "  let light_ndc = light_clip.xyz / light_clip.w;\n" \
    "  let shadow_uv = vec2<f32>(\n" \
    "    light_ndc.x * 0.5 + 0.5,\n" \
    "    light_ndc.y * -0.5 + 0.5\n" \
    "  );\n" \
    "  if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||\n" \
    "      shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||\n" \
    "      light_ndc.z < 0.0 || light_ndc.z > 1.0) {\n" \
    "    return -1.0;\n" \
    "  }\n" \
    "  let scale = uniforms.shadow_cascade_scales[cascade];\n" \
    "  let scaled_uv = shadow_uv * scale;\n" \
    "  let current_depth = light_ndc.z - uniforms.shadow_info.y;\n" \
    "  let texel_size = 1.0 / vec2<f32>(textureDimensions(shadow_map));\n" \
    "  var shadow = 0.0;\n" \
    "  for (var x = -1; x <= 1; x++) {\n" \
    "    for (var y = -1; y <= 1; y++) {\n" \
    "      let offset = vec2<f32>(f32(x), f32(y)) * texel_size;\n" \
    "      shadow += textureSampleCompareLevel(\n" \
    "        shadow_map, shadow_sampler,\n" \
    "        scaled_uv + offset, cascade, current_depth);\n" \
    "    }\n" \
    "  }\n" \
    "  return shadow / 9.0;\n" \
    "}\n" \
    "fn computeShadow(world_pos : vec3<f32>) -> ShadowResult {\n" \
    "  var result : ShadowResult;\n" \
    "  let clip = uniforms.vp * vec4<f32>(world_pos, 1.0);\n" \
    "  let view_depth = clip.w;\n" \
    "  var cascade : i32 = 0;\n" \
    "  if (view_depth > uniforms.cascade_splits.z) { cascade = 3; }\n" \
    "  else if (view_depth > uniforms.cascade_splits.y) { cascade = 2; }\n" \
    "  else if (view_depth > uniforms.cascade_splits.x) { cascade = 1; }\n" \
    "  if (view_depth > uniforms.cascade_splits.w) {\n" \
    "    result.shadow = 1.0;\n" \
    "    result.debug_color = vec3<f32>(1.0, 1.0, 1.0);\n" \
    "    return result;\n" \
    "  }\n" \
    "  result.debug_color = cascadeDebugColor(cascade);\n" \
    "  var shadow = sampleShadowCascade(world_pos, cascade);\n" \
    "  if (shadow < 0.0 && cascade < 3) {\n" \
    "    shadow = sampleShadowCascade(world_pos, cascade + 1);\n" \
    "  }\n" \
    "  if (shadow < 0.0 && cascade > 0) {\n" \
    "    shadow = sampleShadowCascade(world_pos, cascade - 1);\n" \
    "  }\n" \
    "  if (shadow < 0.0) { shadow = 1.0; }\n" \
    "  result.shadow = shadow;\n" \
    "  return result;\n" \
    "}\n"

#endif
