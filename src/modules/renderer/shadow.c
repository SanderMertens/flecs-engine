#include "renderer.h"
#include "flecs_engine.h"
#include <cglm/clipspace/ortho_rh_zo.h>
#include <math.h>

#define FLECS_ENGINE_SHADOW_MAP_FORMAT WGPUTextureFormat_Depth32Float

static const char *kShadowDepthShaderSource =
    "struct ShadowUniforms {\n"
    "  light_vp : mat4x4<f32>\n"
    "}\n"
    "@group(0) @binding(0) var<uniform> shadow_uniforms : ShadowUniforms;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(2) m0 : vec3<f32>,\n"
    "  @location(3) m1 : vec3<f32>,\n"
    "  @location(4) m2 : vec3<f32>,\n"
    "  @location(5) m3 : vec3<f32>\n"
    "}\n"
    "@vertex fn vs_main(input : VertexInput) -> @builtin(position) vec4<f32> {\n"
    "  let model = mat4x4<f32>(\n"
    "    vec4<f32>(input.m0, 0.0),\n"
    "    vec4<f32>(input.m1, 0.0),\n"
    "    vec4<f32>(input.m2, 0.0),\n"
    "    vec4<f32>(input.m3, 1.0)\n"
    "  );\n"
    "  let world_pos = model * vec4<f32>(input.pos, 1.0);\n"
    "  return shadow_uniforms.light_vp * world_pos;\n"
    "}\n";

int flecsEngine_shadow_init(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t shadow_map_size)
{
    (void)world;
    impl->shadow_map_size = shadow_map_size;

    /* Compute per-cascade effective sizes. Each successive cascade halves
     * in resolution, with a minimum of 256. The actual texture array uses
     * shadow_map_size for all layers; per-cascade sizes are realized via
     * viewport restrictions during rendering. */
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        uint32_t size = shadow_map_size >> i;
        if (size < 256) size = 256;
        impl->shadow_cascade_sizes[i] = size;
    }

    /* Compile shadow depth shader directly (bypasses ECS shader system
     * to avoid deferred context issues during batch setup) */
    impl->shadow_shader_module = flecsEngine_createShaderModule(
        impl->device, kShadowDepthShaderSource);
    if (!impl->shadow_shader_module) {
        ecs_err("failed to compile shadow depth shader");
        return -1;
    }

    /* Create shadow depth texture array (one layer per cascade) */
    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = impl->shadow_map_size,
            .height = impl->shadow_map_size,
            .depthOrArrayLayers = FLECS_ENGINE_SHADOW_CASCADE_COUNT
        },
        .format = FLECS_ENGINE_SHADOW_MAP_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    impl->shadow_texture = wgpuDeviceCreateTexture(impl->device, &tex_desc);
    if (!impl->shadow_texture) {
        ecs_err("failed to create shadow map texture");
        return -1;
    }

    /* Create 2D array view for fragment shader sampling */
    WGPUTextureViewDescriptor array_view_desc = {
        .format = FLECS_ENGINE_SHADOW_MAP_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = FLECS_ENGINE_SHADOW_CASCADE_COUNT,
        .aspect = WGPUTextureAspect_DepthOnly,
    };

    impl->shadow_texture_view = wgpuTextureCreateView(
        impl->shadow_texture, &array_view_desc);
    if (!impl->shadow_texture_view) {
        ecs_err("failed to create shadow map array view");
        return -1;
    }

    /* Create per-layer 2D views for rendering into each cascade */
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        WGPUTextureViewDescriptor layer_desc = {
            .format = FLECS_ENGINE_SHADOW_MAP_FORMAT,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = (uint32_t)i,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_DepthOnly,
        };

        impl->shadow_layer_views[i] = wgpuTextureCreateView(
            impl->shadow_texture, &layer_desc);
        if (!impl->shadow_layer_views[i]) {
            ecs_err("failed to create shadow map layer view %d", i);
            return -1;
        }
    }

    /* Create comparison sampler for shadow sampling */
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .compare = WGPUCompareFunction_Less,
        .maxAnisotropy = 1
    };

    impl->shadow_sampler = wgpuDeviceCreateSampler(impl->device, &sampler_desc);
    if (!impl->shadow_sampler) {
        ecs_err("failed to create shadow sampler");
        return -1;
    }

    /* Shadow pass bind group layout: binding 0 = light VP uniform */
    WGPUBindGroupLayoutEntry pass_layout_entries[1] = {{
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = (WGPUBufferBindingLayout){
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(mat4)
        }
    }};

    WGPUBindGroupLayoutDescriptor pass_layout_desc = {
        .entryCount = 1,
        .entries = pass_layout_entries
    };

    impl->shadow_pass_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &pass_layout_desc);
    if (!impl->shadow_pass_bind_layout) {
        ecs_err("failed to create shadow pass bind group layout");
        return -1;
    }

    /* Create one VP buffer and bind group per cascade. Each cascade needs its
     * own buffer because wgpuQueueWriteBuffer calls are all resolved before
     * the command buffer executes, so a single shared buffer would only
     * contain the last cascade's VP matrix by the time any render pass runs. */
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        WGPUBufferDescriptor buf_desc = {
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(mat4)
        };

        impl->shadow_vp_buffers[i] = wgpuDeviceCreateBuffer(
            impl->device, &buf_desc);
        if (!impl->shadow_vp_buffers[i]) {
            ecs_err("failed to create shadow VP buffer %d", i);
            return -1;
        }

        WGPUBindGroupEntry pass_entries[1] = {{
            .binding = 0,
            .buffer = impl->shadow_vp_buffers[i],
            .size = sizeof(mat4)
        }};

        WGPUBindGroupDescriptor pass_group_desc = {
            .layout = impl->shadow_pass_bind_layout,
            .entryCount = 1,
            .entries = pass_entries
        };

        impl->shadow_pass_bind_groups[i] = wgpuDeviceCreateBindGroup(
            impl->device, &pass_group_desc);
        if (!impl->shadow_pass_bind_groups[i]) {
            ecs_err("failed to create shadow pass bind group %d", i);
            return -1;
        }
    }

    /* Bump shadow version so that combined IBL+shadow bind groups are
     * recreated with the new shadow resources. */
    impl->scene_bind_version++;

    return 0;
}

void flecsEngine_shadow_cleanup(
    FlecsEngineImpl *impl)
{
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        if (impl->shadow_pass_bind_groups[i]) {
            wgpuBindGroupRelease(impl->shadow_pass_bind_groups[i]);
            impl->shadow_pass_bind_groups[i] = NULL;
        }
        if (impl->shadow_vp_buffers[i]) {
            wgpuBufferRelease(impl->shadow_vp_buffers[i]);
            impl->shadow_vp_buffers[i] = NULL;
        }
    }
    if (impl->shadow_pass_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->shadow_pass_bind_layout);
        impl->shadow_pass_bind_layout = NULL;
    }
    if (impl->shadow_sampler) {
        wgpuSamplerRelease(impl->shadow_sampler);
        impl->shadow_sampler = NULL;
    }
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        if (impl->shadow_layer_views[i]) {
            wgpuTextureViewRelease(impl->shadow_layer_views[i]);
            impl->shadow_layer_views[i] = NULL;
        }
    }
    if (impl->shadow_texture_view) {
        wgpuTextureViewRelease(impl->shadow_texture_view);
        impl->shadow_texture_view = NULL;
    }
    if (impl->shadow_texture) {
        wgpuTextureRelease(impl->shadow_texture);
        impl->shadow_texture = NULL;
    }
    if (impl->shadow_shader_module) {
        wgpuShaderModuleRelease(impl->shadow_shader_module);
        impl->shadow_shader_module = NULL;
    }
}

void flecsEngine_shadow_computeCascades(
    const ecs_world_t *world,
    const FlecsRenderView *view,
    uint32_t shadow_map_size,
    const uint32_t cascade_sizes[FLECS_ENGINE_SHADOW_CASCADE_COUNT],
    mat4 out_light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT],
    float out_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT])
{
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        glm_mat4_identity(out_light_vp[i]);
        out_splits[i] = 0.0f;
    }

    if (!view->light || !view->camera) {
        return;
    }

    /* Get light direction */
    const FlecsRotation3 *rotation = ecs_get(
        world, view->light, FlecsRotation3);
    if (!rotation) {
        return;
    }

    vec3 ray_dir;
    if (!flecsEngine_lightDirFromRotation(rotation, ray_dir)) {
        return;
    }

    /* Choose an up vector that isn't parallel to the light direction */
    vec3 up = {0.0f, 1.0f, 0.0f};
    if (fabsf(ray_dir[1]) > 0.99f) {
        up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f;
    }

    /* Get camera properties for frustum computation */
    const FlecsCamera *cam = ecs_get(world, view->camera, FlecsCamera);
    const FlecsCameraImpl *cam_impl = ecs_get(
        world, view->camera, FlecsCameraImpl);
    if (!cam || !cam_impl) {
        return;
    }

    float near = cam->near_;
    float far = cam->far_;
    float fov = cam->fov;
    float aspect = cam->aspect_ratio;
    if (aspect <= 0.0f) {
        aspect = 1.0f;
    }

    /* Compute inverse view matrix (view space -> world space) */
    mat4 inv_view;
    glm_mat4_inv((vec4*)cam_impl->view, inv_view);

    /* Compute cascade split distances (practical split scheme) */
    float lambda = 0.75f;
    float splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        float p = (float)(i + 1) / (float)FLECS_ENGINE_SHADOW_CASCADE_COUNT;
        float log_split = near * powf(far / near, p);
        float lin_split = near + (far - near) * p;
        splits[i] = lambda * log_split + (1.0f - lambda) * lin_split;
        out_splits[i] = splits[i];
    }

    float tan_half_fov = tanf(fov * 0.5f);

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        float cascade_near = (c == 0) ? near : splits[c - 1];
        float cascade_far = splits[c];

        /* Compute 8 frustum corners in view space (right-handed, -Z forward) */
        float nh = cascade_near * tan_half_fov;
        float nw = nh * aspect;
        float fh = cascade_far * tan_half_fov;
        float fw = fh * aspect;

        vec4 corners[8] = {
            { -nw, -nh, -cascade_near, 1.0f },
            {  nw, -nh, -cascade_near, 1.0f },
            {  nw,  nh, -cascade_near, 1.0f },
            { -nw,  nh, -cascade_near, 1.0f },
            { -fw, -fh, -cascade_far, 1.0f },
            {  fw, -fh, -cascade_far, 1.0f },
            {  fw,  fh, -cascade_far, 1.0f },
            { -fw,  fh, -cascade_far, 1.0f },
        };

        /* Transform corners to world space and compute center */
        vec3 center = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 8; i++) {
            glm_mat4_mulv(inv_view, corners[i], corners[i]);
            center[0] += corners[i][0];
            center[1] += corners[i][1];
            center[2] += corners[i][2];
        }

        glm_vec3_scale(center, 1.0f / 8.0f, center);

        /* Build light view matrix centered on the cascade frustum */
        float shadow_distance = 200.0f;
        vec3 light_pos;
        glm_vec3_scale(ray_dir, -shadow_distance, light_pos);
        glm_vec3_add(light_pos, center, light_pos);

        mat4 light_view;
        glm_lookat(light_pos, center, up, light_view);

        /* Transform corners to light space and compute tight AABB */
        float min_x, max_x, min_y, max_y, min_z, max_z;
        {
            vec4 lc;
            glm_mat4_mulv(light_view, corners[0], lc);
            min_x = max_x = lc[0];
            min_y = max_y = lc[1];
            min_z = max_z = lc[2];
        }

        for (int i = 1; i < 8; i++) {
            vec4 lc;
            glm_mat4_mulv(light_view, corners[i], lc);
            if (lc[0] < min_x) min_x = lc[0];
            if (lc[0] > max_x) max_x = lc[0];
            if (lc[1] < min_y) min_y = lc[1];
            if (lc[1] > max_y) max_y = lc[1];
            if (lc[2] < min_z) min_z = lc[2];
            if (lc[2] > max_z) max_z = lc[2];
        }

        /* Push the light back along its view direction so that tall
         * shadow casters above the camera frustum remain in front of
         * the light. Without this, objects extending above the frustum
         * can end up behind the light (positive Z in light-view space)
         * and get clipped by the orthographic projection. Pushing by
         * shadow_distance ensures objects up to shadow_distance above
         * the nearest frustum corner are captured. */
        float z_push = max_z + shadow_distance;
        if (z_push > 0.0f) {
            light_view[3][2] -= z_push;
            min_z -= z_push;
            max_z -= z_push;
        }

        /* Use square extent for stable shadow map (avoids axis-dependent
         * resolution differences and simplifies texel snapping). */
        float half_x = (max_x - min_x) * 0.5f;
        float half_y = (max_y - min_y) * 0.5f;
        float extent = fmaxf(half_x, half_y);

        /* Pad by one texel so the texel-snapping step below (which can
         * shift the view by up to one texel) never pushes frustum corners
         * outside the shadow map UV [0,1] range. Use the per-cascade
         * effective size for correct texel calculations. */
        uint32_t cascade_size = cascade_sizes[c];
        extent += (2.0f * extent) / (float)cascade_size;

        /* Re-center light view on the AABB center so the symmetric ortho
         * projection [-extent, extent] fully covers the frustum slice.
         * Without this, the AABB may be off-center from the light-space
         * origin, causing pixels near cascade edges to fall outside the
         * shadow map UV [0,1] range. */
        float aabb_cx = (min_x + max_x) * 0.5f;
        float aabb_cy = (min_y + max_y) * 0.5f;
        light_view[3][0] -= aabb_cx;
        light_view[3][1] -= aabb_cy;

        /* Extend far Z range to catch shadow casters behind the camera */
        float z_range = max_z - min_z;
        min_z -= z_range;

        /* Snap light-space origin to texel boundaries to prevent shimmer */
        float texel_size = (2.0f * extent) / (float)cascade_size;
        vec4 origin = {0.0f, 0.0f, 0.0f, 1.0f};
        glm_mat4_mulv(light_view, origin, origin);
        origin[0] = floorf(origin[0] / texel_size) * texel_size;
        origin[1] = floorf(origin[1] / texel_size) * texel_size;

        vec4 snapped;
        glm_mat4_mulv(light_view, (vec4){0.0f, 0.0f, 0.0f, 1.0f}, snapped);
        light_view[3][0] += origin[0] - snapped[0];
        light_view[3][1] += origin[1] - snapped[1];

        /* Build orthographic projection.
         * Use a near plane close to the light to avoid clipping tall
         * shadow casters that are closer to the light than the frustum
         * corners. With Float32 depth, precision is sufficient. */
        float near_plane = 0.01f;
        float far_plane = -min_z;
        if (far_plane <= near_plane) {
            far_plane = near_plane + 1.0f;
        }

        mat4 light_proj;
        glm_ortho_rh_zo(
            -extent, extent,
            -extent, extent,
            near_plane, far_plane,
            light_proj);

        glm_mat4_mul(light_proj, light_view, out_light_vp[c]);
    }
}

int flecsEngine_shadow_ensureSize(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint32_t shadow_map_size)
{
    if (shadow_map_size == 0) {
        shadow_map_size = FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT;
    }

    if (impl->shadow_map_size == shadow_map_size) {
        return 0;
    }

    flecsEngine_shadow_cleanup(impl);
    return flecsEngine_shadow_init(world, impl, shadow_map_size);
}
