#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "renderer.h"
#include "flecs_engine.h"

#define FLECS_ENGINE_CLUSTER_INITIAL_LIGHTS 32
#define FLECS_ENGINE_CLUSTER_INITIAL_INDICES 4096

/* (Re)create the bind group that references the current GPU buffers.
 * Must be called whenever a buffer is replaced due to growth. */
static bool flecsEngine_cluster_createBindGroup(
    FlecsEngineImpl *impl)
{
    if (impl->cluster_bind_group) {
        wgpuBindGroupRelease(impl->cluster_bind_group);
        impl->cluster_bind_group = NULL;
    }

    WGPUBindGroupEntry entries[5] = {0};
    entries[0] = (WGPUBindGroupEntry){
        .binding = 0, .buffer = impl->cluster_info_buffer,
        .size = sizeof(FlecsClusterInfo) };
    entries[1] = (WGPUBindGroupEntry){
        .binding = 1, .buffer = impl->cluster_grid_buffer,
        .size = (uint64_t)FLECS_ENGINE_CLUSTER_TOTAL *
            sizeof(FlecsClusterEntry) };
    entries[2] = (WGPUBindGroupEntry){
        .binding = 2, .buffer = impl->cluster_index_buffer,
        .size = (uint64_t)impl->cluster_index_capacity *
            sizeof(uint32_t) };
    entries[3] = (WGPUBindGroupEntry){
        .binding = 3, .buffer = impl->point_light_buffer,
        .size = (uint64_t)impl->point_light_capacity *
            sizeof(FlecsGpuPointLight) };
    entries[4] = (WGPUBindGroupEntry){
        .binding = 4, .buffer = impl->spot_light_buffer,
        .size = (uint64_t)impl->spot_light_capacity *
            sizeof(FlecsGpuSpotLight) };

    WGPUBindGroupDescriptor bg_desc = {
        .layout = impl->cluster_bind_layout,
        .entryCount = 5,
        .entries = entries
    };
    impl->cluster_bind_group = wgpuDeviceCreateBindGroup(
        impl->device, &bg_desc);
    return impl->cluster_bind_group != NULL;
}

/* Grow a storage buffer to at least `needed` elements of `elem_size`.
 * Returns the new capacity via `*capacity`. Replaces *buffer in place. */
static bool flecsEngine_cluster_growBuffer(
    WGPUDevice device,
    WGPUBuffer *buffer,
    int32_t *capacity,
    int32_t needed,
    uint64_t elem_size)
{
    if (needed <= *capacity) {
        return false; /* no growth needed */
    }

    int32_t new_cap = *capacity ? *capacity : 1;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    if (*buffer) {
        wgpuBufferRelease(*buffer);
    }

    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)new_cap * elem_size
    };
    *buffer = wgpuDeviceCreateBuffer(device, &desc);
    *capacity = new_cap;
    return true; /* buffer changed, bind group must be recreated */
}

int flecsEngine_cluster_init(
    FlecsEngineImpl *impl)
{
    int32_t init_lights = FLECS_ENGINE_CLUSTER_INITIAL_LIGHTS;
    int32_t init_indices = FLECS_ENGINE_CLUSTER_INITIAL_INDICES;

    /* CPU-side arrays */
    impl->cpu_point_lights = ecs_os_calloc_n(
        FlecsGpuPointLight, init_lights);
    impl->point_light_capacity = init_lights;

    impl->cpu_spot_lights = ecs_os_calloc_n(
        FlecsGpuSpotLight, init_lights);
    impl->spot_light_capacity = init_lights;

    impl->cpu_cluster_indices = ecs_os_calloc_n(uint32_t, init_indices);
    impl->cluster_index_capacity = init_indices;

    /* GPU point light storage buffer */
    WGPUBufferDescriptor pl_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)init_lights * sizeof(FlecsGpuPointLight)
    };
    impl->point_light_buffer = wgpuDeviceCreateBuffer(
        impl->device, &pl_desc);

    /* GPU spot light storage buffer */
    WGPUBufferDescriptor sl_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)init_lights * sizeof(FlecsGpuSpotLight)
    };
    impl->spot_light_buffer = wgpuDeviceCreateBuffer(
        impl->device, &sl_desc);

    /* Cluster info uniform buffer (fixed size) */
    WGPUBufferDescriptor info_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(FlecsClusterInfo)
    };
    impl->cluster_info_buffer = wgpuDeviceCreateBuffer(
        impl->device, &info_desc);

    /* Cluster grid storage buffer (fixed size) */
    WGPUBufferDescriptor grid_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)FLECS_ENGINE_CLUSTER_TOTAL *
            sizeof(FlecsClusterEntry)
    };
    impl->cluster_grid_buffer = wgpuDeviceCreateBuffer(
        impl->device, &grid_desc);

    /* Light index storage buffer */
    WGPUBufferDescriptor idx_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)init_indices * sizeof(uint32_t)
    };
    impl->cluster_index_buffer = wgpuDeviceCreateBuffer(
        impl->device, &idx_desc);

    if (!impl->point_light_buffer || !impl->spot_light_buffer ||
        !impl->cluster_info_buffer || !impl->cluster_grid_buffer ||
        !impl->cluster_index_buffer)
    {
        ecs_err("failed to create cluster GPU buffers");
        return -1;
    }

    /* Bind group layout (5 bindings, fixed) */
    WGPUBindGroupLayoutEntry layout_entries[5] = {0};
    layout_entries[0] = (WGPUBindGroupLayoutEntry){
        .binding = 0, .visibility = WGPUShaderStage_Fragment,
        .buffer = { .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(FlecsClusterInfo) }
    };
    layout_entries[1] = (WGPUBindGroupLayoutEntry){
        .binding = 1, .visibility = WGPUShaderStage_Fragment,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage,
            .minBindingSize = sizeof(FlecsClusterEntry) }
    };
    layout_entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2, .visibility = WGPUShaderStage_Fragment,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage,
            .minBindingSize = sizeof(uint32_t) }
    };
    layout_entries[3] = (WGPUBindGroupLayoutEntry){
        .binding = 3, .visibility = WGPUShaderStage_Fragment,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage,
            .minBindingSize = sizeof(FlecsGpuPointLight) }
    };
    layout_entries[4] = (WGPUBindGroupLayoutEntry){
        .binding = 4, .visibility = WGPUShaderStage_Fragment,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage,
            .minBindingSize = sizeof(FlecsGpuSpotLight) }
    };

    WGPUBindGroupLayoutDescriptor layout_desc = {
        .entryCount = 5, .entries = layout_entries
    };
    impl->cluster_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &layout_desc);
    if (!impl->cluster_bind_layout) {
        ecs_err("failed to create cluster bind group layout");
        return -1;
    }

    if (!flecsEngine_cluster_createBindGroup(impl)) {
        ecs_err("failed to create cluster bind group");
        return -1;
    }

    return 0;
}

void flecsEngine_cluster_cleanup(
    FlecsEngineImpl *impl)
{
    if (impl->cluster_bind_group) {
        wgpuBindGroupRelease(impl->cluster_bind_group);
        impl->cluster_bind_group = NULL;
    }
    if (impl->cluster_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->cluster_bind_layout);
        impl->cluster_bind_layout = NULL;
    }
    if (impl->cluster_index_buffer) {
        wgpuBufferRelease(impl->cluster_index_buffer);
        impl->cluster_index_buffer = NULL;
    }
    if (impl->cluster_grid_buffer) {
        wgpuBufferRelease(impl->cluster_grid_buffer);
        impl->cluster_grid_buffer = NULL;
    }
    if (impl->cluster_info_buffer) {
        wgpuBufferRelease(impl->cluster_info_buffer);
        impl->cluster_info_buffer = NULL;
    }
    if (impl->point_light_buffer) {
        wgpuBufferRelease(impl->point_light_buffer);
        impl->point_light_buffer = NULL;
    }
    if (impl->spot_light_buffer) {
        wgpuBufferRelease(impl->spot_light_buffer);
        impl->spot_light_buffer = NULL;
    }

    ecs_os_free(impl->cpu_point_lights);
    impl->cpu_point_lights = NULL;
    impl->point_light_capacity = 0;
    ecs_os_free(impl->cpu_spot_lights);
    impl->cpu_spot_lights = NULL;
    impl->spot_light_capacity = 0;
    ecs_os_free(impl->cpu_cluster_indices);
    impl->cpu_cluster_indices = NULL;
    impl->cluster_index_capacity = 0;
}

/* Ensure CPU + GPU light arrays can hold `needed` lights. Returns true
 * if a GPU buffer was reallocated (bind group must be recreated). */
bool flecsEngine_cluster_ensurePointLights(
    FlecsEngineImpl *engine, int32_t needed)
{
    if (needed <= engine->point_light_capacity) {
        return false;
    }

    int32_t new_cap = engine->point_light_capacity ? engine->point_light_capacity : 1;
    while (new_cap < needed) new_cap *= 2;

    engine->cpu_point_lights = ecs_os_realloc_n(
        engine->cpu_point_lights, FlecsGpuPointLight, new_cap);

    flecsEngine_cluster_growBuffer(engine->device,
        &engine->point_light_buffer, &engine->point_light_capacity,
        new_cap, sizeof(FlecsGpuPointLight));

    engine->cluster_bind_group_dirty = true;
    return true;
}

bool flecsEngine_cluster_ensureSpotLights(
    FlecsEngineImpl *engine, int32_t needed)
{
    if (needed <= engine->spot_light_capacity) {
        return false;
    }

    int32_t new_cap = engine->spot_light_capacity ? engine->spot_light_capacity : 1;
    while (new_cap < needed) new_cap *= 2;

    engine->cpu_spot_lights = ecs_os_realloc_n(
        engine->cpu_spot_lights, FlecsGpuSpotLight, new_cap);

    flecsEngine_cluster_growBuffer(engine->device,
        &engine->spot_light_buffer, &engine->spot_light_capacity,
        new_cap, sizeof(FlecsGpuSpotLight));

    engine->cluster_bind_group_dirty = true;
    return true;
}

static bool flecsEngine_cluster_ensureIndices(
    FlecsEngineImpl *engine, int32_t needed)
{
    if (needed <= engine->cluster_index_capacity) {
        return false;
    }

    int32_t new_cap = engine->cluster_index_capacity
        ? engine->cluster_index_capacity : 1;
    while (new_cap < needed) new_cap *= 2;

    engine->cpu_cluster_indices = ecs_os_realloc_n(
        engine->cpu_cluster_indices, uint32_t, new_cap);

    bool changed = flecsEngine_cluster_growBuffer(engine->device,
        &engine->cluster_index_buffer, &engine->cluster_index_capacity,
        new_cap, sizeof(uint32_t));

    return changed;
}

static inline int flecsEngine_cluster_index(int tx, int ty, int sz) {
    return tx + ty * FLECS_ENGINE_CLUSTER_X
             + sz * FLECS_ENGINE_CLUSTER_X * FLECS_ENGINE_CLUSTER_Y;
}

/* Compute overlapping cluster tile range for a light sphere. */
static bool flecsEngine_cluster_sphereRange(
    const float view_mat[4][4],
    float world_x, float world_y, float world_z,
    float range,
    float near, float far,
    float tan_half_fov, float aspect,
    float log_ratio,
    int *out_tx_min, int *out_tx_max,
    int *out_ty_min, int *out_ty_max,
    int *out_sz_min, int *out_sz_max)
{
    float vx = view_mat[0][0] * world_x + view_mat[1][0] * world_y +
               view_mat[2][0] * world_z + view_mat[3][0];
    float vy = view_mat[0][1] * world_x + view_mat[1][1] * world_y +
               view_mat[2][1] * world_z + view_mat[3][1];
    float vz = view_mat[0][2] * world_x + view_mat[1][2] * world_y +
               view_mat[2][2] * world_z + view_mat[3][2];

    float depth = -vz;

    if (depth + range < near || depth - range > far) {
        return false;
    }

    float z_min = fmaxf(depth - range, near);
    float z_max = fminf(depth + range, far);

    int sz_min = (int)floorf(logf(z_min / near) / log_ratio *
        (float)FLECS_ENGINE_CLUSTER_Z);
    int sz_max = (int)floorf(logf(z_max / near) / log_ratio *
        (float)FLECS_ENGINE_CLUSTER_Z);
    if (sz_min < 0) sz_min = 0;
    if (sz_max >= FLECS_ENGINE_CLUSTER_Z) sz_max = FLECS_ENGINE_CLUSTER_Z - 1;

    int tx_min, tx_max, ty_min, ty_max;
    if (depth <= range) {
        tx_min = 0; tx_max = FLECS_ENGINE_CLUSTER_X - 1;
        ty_min = 0; ty_max = FLECS_ENGINE_CLUSTER_Y - 1;
    } else {
        float half_y = depth * tan_half_fov;
        float half_x = half_y * aspect;
        float ndc_cx = (half_x > 1e-6f) ? (vx / half_x) : 0.0f;
        float ndc_cy = (half_y > 1e-6f) ? (vy / half_y) : 0.0f;

        float min_depth = fmaxf(depth - range, near);
        float half_y_min = min_depth * tan_half_fov;
        float half_x_min = half_y_min * aspect;
        float ndc_rx = (half_x_min > 1e-6f) ? (range / half_x_min) : 100.0f;
        float ndc_ry = (half_y_min > 1e-6f) ? (range / half_y_min) : 100.0f;

        float tile_cx = ( ndc_cx * 0.5f + 0.5f) * (float)FLECS_ENGINE_CLUSTER_X;
        float tile_cy = (-ndc_cy * 0.5f + 0.5f) * (float)FLECS_ENGINE_CLUSTER_Y;
        float tile_rx = ndc_rx * 0.5f * (float)FLECS_ENGINE_CLUSTER_X;
        float tile_ry = ndc_ry * 0.5f * (float)FLECS_ENGINE_CLUSTER_Y;

        tx_min = (int)floorf(tile_cx - tile_rx);
        tx_max = (int)floorf(tile_cx + tile_rx);
        ty_min = (int)floorf(tile_cy - tile_ry);
        ty_max = (int)floorf(tile_cy + tile_ry);
        if (tx_min < 0) tx_min = 0;
        if (tx_max >= FLECS_ENGINE_CLUSTER_X) tx_max = FLECS_ENGINE_CLUSTER_X - 1;
        if (ty_min < 0) ty_min = 0;
        if (ty_max >= FLECS_ENGINE_CLUSTER_Y) ty_max = FLECS_ENGINE_CLUSTER_Y - 1;
    }

    *out_tx_min = tx_min; *out_tx_max = tx_max;
    *out_ty_min = ty_min; *out_ty_max = ty_max;
    *out_sz_min = sz_min; *out_sz_max = sz_max;
    return true;
}

void flecsEngine_cluster_build(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view)
{
    if (!engine->cluster_info_buffer || !view->camera) {
        return;
    }

    const FlecsCamera *cam = ecs_get(world, view->camera, FlecsCamera);
    const FlecsCameraImpl *cam_impl = ecs_get(
        world, view->camera, FlecsCameraImpl);
    if (!cam || !cam_impl) {
        return;
    }

    float near = cam->near_;
    float far = cam->far_;
    if (near <= 0.0f) near = 0.1f;
    if (far <= near) far = near + 1.0f;
    float tan_half_fov = tanf(cam->fov * 0.5f);
    float aspect = (engine->width > 0 && engine->height > 0)
        ? (float)engine->width / (float)engine->height : 1.0f;
    float log_ratio = logf(far / near);
    if (log_ratio < 1e-6f) return;

    const float (*view_mat)[4] = (const float (*)[4])cam_impl->view;

    int32_t point_count = engine->point_light_count;
    int32_t spot_count = engine->spot_light_count;

    /* Upload cluster info */
    FlecsClusterInfo info = {
        .grid_size = { FLECS_ENGINE_CLUSTER_X, FLECS_ENGINE_CLUSTER_Y,
            FLECS_ENGINE_CLUSTER_Z, FLECS_ENGINE_CLUSTER_TOTAL },
        .screen_info = { (float)engine->width, (float)engine->height,
            near, log_ratio }
    };
    wgpuQueueWriteBuffer(engine->queue, engine->cluster_info_buffer,
        0, &info, sizeof(info));

    /* --- Two-pass cluster assignment --- */

    /* Pass 1: count per-cluster lights */
    uint16_t point_counts[FLECS_ENGINE_CLUSTER_TOTAL];
    uint16_t spot_counts[FLECS_ENGINE_CLUSTER_TOTAL];
    memset(point_counts, 0, sizeof(point_counts));
    memset(spot_counts, 0, sizeof(spot_counts));

    for (int32_t li = 0; li < point_count; li++) {
        int tx0, tx1, ty0, ty1, sz0, sz1;
        if (!flecsEngine_cluster_sphereRange(view_mat,
            engine->cpu_point_lights[li].position[0],
            engine->cpu_point_lights[li].position[1],
            engine->cpu_point_lights[li].position[2],
            engine->cpu_point_lights[li].position[3],
            near, far, tan_half_fov, aspect, log_ratio,
            &tx0, &tx1, &ty0, &ty1, &sz0, &sz1)) continue;
        for (int sz = sz0; sz <= sz1; sz++)
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++)
                    point_counts[flecsEngine_cluster_index(tx, ty, sz)]++;
    }

    for (int32_t li = 0; li < spot_count; li++) {
        int tx0, tx1, ty0, ty1, sz0, sz1;
        if (!flecsEngine_cluster_sphereRange(view_mat,
            engine->cpu_spot_lights[li].position[0],
            engine->cpu_spot_lights[li].position[1],
            engine->cpu_spot_lights[li].position[2],
            engine->cpu_spot_lights[li].position[3],
            near, far, tan_half_fov, aspect, log_ratio,
            &tx0, &tx1, &ty0, &ty1, &sz0, &sz1)) continue;
        for (int sz = sz0; sz <= sz1; sz++)
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++)
                    spot_counts[flecsEngine_cluster_index(tx, ty, sz)]++;
    }

    /* Compute offsets via prefix sum */
    FlecsClusterEntry grid[FLECS_ENGINE_CLUSTER_TOTAL];
    uint32_t point_offset = 0;
    for (int i = 0; i < FLECS_ENGINE_CLUSTER_TOTAL; i++) {
        grid[i].point_offset = point_offset;
        grid[i].point_count = point_counts[i];
        point_offset += point_counts[i];
    }
    uint32_t spot_offset = point_offset;
    for (int i = 0; i < FLECS_ENGINE_CLUSTER_TOTAL; i++) {
        grid[i].spot_offset = spot_offset;
        grid[i].spot_count = spot_counts[i];
        spot_offset += spot_counts[i];
    }
    uint32_t total_indices = spot_offset;

    /* Grow index buffer if needed. Also pick up the dirty flag set by
     * the light setup functions if they resized GPU buffers. */
    bool bg_dirty = engine->cluster_bind_group_dirty;
    bg_dirty |= flecsEngine_cluster_ensureIndices(
        engine, (int32_t)total_indices);
    if (bg_dirty) {
        flecsEngine_cluster_createBindGroup(engine);
        engine->cluster_bind_group_dirty = false;
    }

    /* Pass 2: fill index list */
    uint16_t point_fill[FLECS_ENGINE_CLUSTER_TOTAL];
    uint16_t spot_fill[FLECS_ENGINE_CLUSTER_TOTAL];
    memset(point_fill, 0, sizeof(point_fill));
    memset(spot_fill, 0, sizeof(spot_fill));

    uint32_t *indices = engine->cpu_cluster_indices;

    for (int32_t li = 0; li < point_count; li++) {
        int tx0, tx1, ty0, ty1, sz0, sz1;
        if (!flecsEngine_cluster_sphereRange(view_mat,
            engine->cpu_point_lights[li].position[0],
            engine->cpu_point_lights[li].position[1],
            engine->cpu_point_lights[li].position[2],
            engine->cpu_point_lights[li].position[3],
            near, far, tan_half_fov, aspect, log_ratio,
            &tx0, &tx1, &ty0, &ty1, &sz0, &sz1)) continue;
        for (int sz = sz0; sz <= sz1; sz++)
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++) {
                    int ci = flecsEngine_cluster_index(tx, ty, sz);
                    uint32_t dst = grid[ci].point_offset +
                        point_fill[ci];
                    indices[dst] = (uint32_t)li;
                    point_fill[ci]++;
                }
    }

    for (int32_t li = 0; li < spot_count; li++) {
        int tx0, tx1, ty0, ty1, sz0, sz1;
        if (!flecsEngine_cluster_sphereRange(view_mat,
            engine->cpu_spot_lights[li].position[0],
            engine->cpu_spot_lights[li].position[1],
            engine->cpu_spot_lights[li].position[2],
            engine->cpu_spot_lights[li].position[3],
            near, far, tan_half_fov, aspect, log_ratio,
            &tx0, &tx1, &ty0, &ty1, &sz0, &sz1)) continue;
        for (int sz = sz0; sz <= sz1; sz++)
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++) {
                    int ci = flecsEngine_cluster_index(tx, ty, sz);
                    uint32_t dst = grid[ci].spot_offset +
                        spot_fill[ci];
                    indices[dst] = (uint32_t)li;
                    spot_fill[ci]++;
                }
    }

    /* Upload everything to GPU */
    wgpuQueueWriteBuffer(engine->queue, engine->cluster_grid_buffer,
        0, grid, sizeof(grid));

    if (total_indices > 0) {
        wgpuQueueWriteBuffer(engine->queue, engine->cluster_index_buffer,
            0, indices, (uint64_t)total_indices * sizeof(uint32_t));
    }

    if (point_count > 0) {
        wgpuQueueWriteBuffer(engine->queue, engine->point_light_buffer,
            0, engine->cpu_point_lights,
            (uint64_t)point_count * sizeof(FlecsGpuPointLight));
    }

    if (spot_count > 0) {
        wgpuQueueWriteBuffer(engine->queue, engine->spot_light_buffer,
            0, engine->cpu_spot_lights,
            (uint64_t)spot_count * sizeof(FlecsGpuSpotLight));
    }
}
