#ifndef FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL \
    "struct PointLight {\n" \
    "  position : vec4<f32>,\n" \
    "  color : vec4<f32>\n" \
    "};\n" \
    "struct SpotLight {\n" \
    "  position : vec4<f32>,\n" \
    "  direction : vec4<f32>,\n" \
    "  color : vec4<f32>\n" \
    "};\n" \
    "struct Uniforms {\n" \
    "  vp : mat4x4<f32>,\n" \
    "  inv_vp : mat4x4<f32>,\n" \
    "  light_vp : array<mat4x4<f32>, 4>,\n" \
    "  cascade_splits : vec4<f32>,\n" \
    "  clear_color : vec4<f32>,\n" \
    "  light_ray_dir : vec4<f32>,\n" \
    "  light_color : vec4<f32>,\n" \
    "  camera_pos : vec4<f32>,\n" \
    "  point_light_info : vec4<f32>,\n" \
    "  point_lights : array<PointLight, 32>,\n" \
    "  spot_light_info : vec4<f32>,\n" \
    "  spot_lights : array<SpotLight, 32>\n" \
    "}\n" \
    "@group(0) @binding(0) var<uniform> uniforms : Uniforms;\n"

#endif
