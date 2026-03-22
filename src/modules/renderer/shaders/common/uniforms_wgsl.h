#ifndef FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL \
    "struct Uniforms {\n" \
    "  vp : mat4x4<f32>,\n" \
    "  inv_vp : mat4x4<f32>,\n" \
    "  light_vp : array<mat4x4<f32>, 4>,\n" \
    "  cascade_splits : vec4<f32>,\n" \
    "  sky_color : vec4<f32>,\n" \
    "  light_ray_dir : vec4<f32>,\n" \
    "  light_color : vec4<f32>,\n" \
    "  camera_pos : vec4<f32>,\n" \
    "  shadow_info : vec4<f32>,\n" \
    "  shadow_cascade_scales : vec4<f32>,\n" \
    "  ambient_light : vec4<f32>\n" \
    "}\n" \
    "@group(0) @binding(0) var<uniform> uniforms : Uniforms;\n"

#endif
