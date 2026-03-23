#ifndef FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL_H

/* Cluster struct definitions (no bind group dependency) */
#define FLECS_ENGINE_SHADER_COMMON_CLUSTER_TYPES_WGSL \
    "struct PointLight {\n" \
    "  position : vec4<f32>,\n" \
    "  color : vec4<f32>\n" \
    "};\n" \
    "struct SpotLight {\n" \
    "  position : vec4<f32>,\n" \
    "  direction : vec4<f32>,\n" \
    "  color : vec4<f32>\n" \
    "};\n" \
    "struct ClusterInfo {\n" \
    "  grid_size : vec4<u32>,\n" \
    "  screen_info : vec4<f32>\n" \
    "};\n" \
    "struct ClusterEntry {\n" \
    "  point_offset : u32,\n" \
    "  point_count : u32,\n" \
    "  spot_offset : u32,\n" \
    "  spot_count : u32\n" \
    "};\n"

/* Cluster bind group bindings at group 1 (shared with IBL + shadow). */
#define FLECS_ENGINE_SHADER_COMMON_CLUSTER_BINDINGS_WGSL \
    "@group(1) @binding(5) var<uniform> cluster_info : ClusterInfo;\n" \
    "@group(1) @binding(6) var<storage, read> cluster_grid : array<ClusterEntry>;\n" \
    "@group(1) @binding(7) var<storage, read> light_indices : array<u32>;\n" \
    "@group(1) @binding(8) var<storage, read> point_lights : array<PointLight>;\n" \
    "@group(1) @binding(9) var<storage, read> spot_lights : array<SpotLight>;\n"

/* Cluster index lookup function */
#define FLECS_ENGINE_SHADER_COMMON_CLUSTER_FUNCTIONS_WGSL \
    "fn getClusterIndex(frag_coord : vec4<f32>) -> u32 {\n" \
    "  let grid_x = cluster_info.grid_size.x;\n" \
    "  let grid_y = cluster_info.grid_size.y;\n" \
    "  let grid_z = cluster_info.grid_size.z;\n" \
    "  let screen_w = cluster_info.screen_info.x;\n" \
    "  let screen_h = cluster_info.screen_info.y;\n" \
    "  let near = cluster_info.screen_info.z;\n" \
    "  let log_ratio = cluster_info.screen_info.w;\n" \
    "  let tile_x = min(u32(frag_coord.x / (screen_w / f32(grid_x))), grid_x - 1u);\n" \
    "  let tile_y = min(u32(frag_coord.y / (screen_h / f32(grid_y))), grid_y - 1u);\n" \
    "  let depth = 1.0 / frag_coord.w;\n" \
    "  let slice = min(u32(max(log(depth / near) / log_ratio * f32(grid_z), 0.0)), grid_z - 1u);\n" \
    "  return tile_x + tile_y * grid_x + slice * grid_x * grid_y;\n" \
    "}\n"

/* Combined macro including types, bindings and functions */
#define FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL \
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_TYPES_WGSL \
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_BINDINGS_WGSL \
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_FUNCTIONS_WGSL

#endif
