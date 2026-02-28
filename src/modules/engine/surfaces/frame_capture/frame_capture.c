#define FLECS_ENGINE_FRAME_OUTPUT_IMPL
#include "frame_capture.h"

#include <stdio.h>

ECS_COMPONENT_DECLARE(FlecsFrameOutput);

static const uint32_t kFrameOutputRowPitchAlignment = 256;

static uint32_t flecsEngineAlignTo(
    uint32_t value,
    uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

typedef struct {
    bool done;
    WGPUMapAsyncStatus status;
} FlecsEngineBufferMapState;

static void flecsEngineProcessEventsUntilDone(
    WGPUInstance instance,
    bool *done)
{
    while (!*done) {
        wgpuInstanceProcessEvents(instance);
    }
}

static void flecsEngineOnBufferMap(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)userdata2;

    FlecsEngineBufferMapState *state = userdata1;
    state->status = status;
    state->done = true;

    if (status != WGPUMapAsyncStatus_Success) {
        if (message.data) {
            ecs_err("Readback map failed: %.*s\n",
                (int)message.length, message.data);
        } else {
            ecs_err("Readback map failed with status=%d\n", (int)status);
        }
    }
}

static int flecsEngineWriteFramePpm(
    const FlecsEngineImpl *impl,
    const uint8_t *rgba,
    uint32_t bytes_per_row)
{
    const char *path = impl->frame_output_path;
    FILE *file = fopen(path, "wb");
    if (!file) {
        ecs_err("Failed to open frame-output file: %s\n", path);
        return -1;
    }

    if (fprintf(file, "P6\n%u %u\n255\n",
        (uint32_t)impl->width, (uint32_t)impl->height) < 0)
    {
        ecs_err("Failed to write PPM header: %s\n", path);
        fclose(file);
        return -1;
    }

    for (uint32_t y = 0; y < (uint32_t)impl->height; y ++) {
        const uint8_t *row = rgba + ((size_t)y * bytes_per_row);
        for (uint32_t x = 0; x < (uint32_t)impl->width; x ++) {
            if (fwrite(&row[(size_t)x * 4u], 1, 3, file) != 3) {
                ecs_err("Failed to write PPM pixel data: %s\n", path);
                fclose(file);
                return -1;
            }
        }
    }

    fclose(file);
    return 0;
}

static void flecsEngineFrameCaptureDestroyResources(
    FlecsEngineImpl *impl)
{
    if (impl->frame_output_texture_view) {
        wgpuTextureViewRelease(impl->frame_output_texture_view);
        impl->frame_output_texture_view = NULL;
    }

    if (impl->frame_output_texture) {
        wgpuTextureRelease(impl->frame_output_texture);
        impl->frame_output_texture = NULL;
    }
}

static int flecsEngineFrameCaptureCreateResources(
    FlecsEngineImpl *impl,
    uint32_t width,
    uint32_t height)
{
    flecsEngineFrameCaptureDestroyResources(impl);

    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = impl->surface_config.format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    impl->frame_output_texture =
        wgpuDeviceCreateTexture(impl->device, &color_desc);
    if (!impl->frame_output_texture) {
        ecs_err("Failed to create frame-output texture\n");
        return -1;
    }

    impl->frame_output_texture_view =
        wgpuTextureCreateView(impl->frame_output_texture, NULL);
    if (!impl->frame_output_texture_view) {
        ecs_err("Failed to create frame-output texture view\n");
        wgpuTextureRelease(impl->frame_output_texture);
        impl->frame_output_texture = NULL;
        return -1;
    }

    return 0;
}

static int flecsEngineFrameCaptureWriteImage(
    FlecsEngineImpl *impl,
    const FlecsEngineSurface *target)
{
    FlecsEngineBufferMapState map_state = {
        .done = false,
        .status = WGPUMapAsyncStatus_Unknown
    };

    WGPUBufferMapCallbackInfo callback = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = flecsEngineOnBufferMap,
        .userdata1 = &map_state,
        .userdata2 = NULL
    };

    wgpuBufferMapAsync(
        target->readback_buffer,
        WGPUMapMode_Read,
        0,
        (size_t)target->readback_buffer_size,
        callback);

    flecsEngineProcessEventsUntilDone(impl->instance, &map_state.done);
    if (map_state.status != WGPUMapAsyncStatus_Success) {
        return -1;
    }

    const uint8_t *mapped = wgpuBufferGetConstMappedRange(
        target->readback_buffer, 0, (size_t)target->readback_buffer_size);
    if (!mapped) {
        ecs_err("Failed to access mapped readback buffer\n");
        wgpuBufferUnmap(target->readback_buffer);
        return -1;
    }

    int result = flecsEngineWriteFramePpm(
        impl,
        mapped,
        target->readback_bytes_per_row);

    wgpuBufferUnmap(target->readback_buffer);
    return result;
}

static int flecsEngineFrameCaptureInitInstance(
    FlecsEngineImpl *impl,
    const void *config)
{
    const FlecsEngineFrameCaptureOutputConfig *capture_cfg = config;
    const char *path = "frame.ppm";

    if (capture_cfg && capture_cfg->path && capture_cfg->path[0]) {
        path = capture_cfg->path;
    }

    impl->frame_output_path = ecs_os_strdup(path);
    if (!impl->frame_output_path) {
        ecs_err("Failed to allocate frame-output path\n");
        return -1;
    }

    return 0;
}

static int flecsEngineFrameCaptureConfigureTarget(
    FlecsEngineImpl *impl)
{
    impl->surface_config = (WGPUSurfaceConfiguration){
        .device = impl->device,
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
        .format = WGPUTextureFormat_RGBA8Unorm,
        .width = (uint32_t)impl->width,
        .height = (uint32_t)impl->height
    };

    if (flecsEngineFrameCaptureCreateResources(
        impl,
        (uint32_t)impl->width,
        (uint32_t)impl->height))
    {
        return -1;
    }

    return 0;
}

static int flecsEngineFrameCapturePrepareFrame(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (impl->output_done) {
        ecs_quit(world);
        return 1;
    }

    return 0;
}

static int flecsEngineFrameCaptureAcquireFrame(
    FlecsEngineImpl *impl,
    FlecsEngineSurface *target)
{
    target->view_texture = impl->frame_output_texture_view;
    target->surface_texture = NULL;
    target->owns_view_texture = false;
    target->surface_status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    target->readback_buffer = NULL;
    target->readback_bytes_per_row = 0;
    target->readback_buffer_size = 0;

    if (!target->view_texture) {
        ecs_err("Failed to acquire frame-output texture view\n");
        return -1;
    }

    return 0;
}

static int flecsEngineFrameCaptureEncodeFrame(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    FlecsEngineSurface *target)
{
    uint32_t bytes_per_row = flecsEngineAlignTo(
        (uint32_t)impl->width * 4u,
        kFrameOutputRowPitchAlignment);
    uint64_t buffer_size = (uint64_t)bytes_per_row * (uint64_t)impl->height;

    WGPUBufferDescriptor readback_desc = {
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        .size = buffer_size,
        .mappedAtCreation = false
    };

    WGPUBuffer readback = wgpuDeviceCreateBuffer(impl->device, &readback_desc);
    if (!readback) {
        ecs_err("Failed to create readback buffer\n");
        return -1;
    }

    WGPUTexelCopyTextureInfo source = {
        .texture = impl->frame_output_texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferInfo destination = {
        .layout = {
            .offset = 0,
            .bytesPerRow = bytes_per_row,
            .rowsPerImage = (uint32_t)impl->height
        },
        .buffer = readback
    };

    WGPUExtent3D copy_size = {
        .width = (uint32_t)impl->width,
        .height = (uint32_t)impl->height,
        .depthOrArrayLayers = 1
    };

    wgpuCommandEncoderCopyTextureToBuffer(
        encoder,
        &source,
        &destination,
        &copy_size);

    target->readback_buffer = readback;
    target->readback_bytes_per_row = bytes_per_row;
    target->readback_buffer_size = buffer_size;
    return 0;
}

static int flecsEngineFrameCaptureSubmitFrame(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    const FlecsEngineSurface *target)
{
    int result = flecsEngineFrameCaptureWriteImage(impl, target);
    if (result == 0) {
        ecs_log(0, "[engine] wrote frame-output image '%s' (%dx%d)",
            impl->frame_output_path,
            impl->width,
            impl->height);
    }

    impl->output_done = true;
    ecs_quit(world);
    return result;
}

static void flecsEngineFrameCaptureOnFrameFailed(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    impl->output_done = true;
    ecs_quit(world);
}

static void flecsEngineFrameCaptureCleanup(
    FlecsEngineImpl *impl,
    bool terminate_runtime)
{
    (void)terminate_runtime;
    flecsEngineFrameCaptureDestroyResources(impl);
}

const FlecsEngineSurfaceInterface flecsEngineFrameCaptureOutputOps = {
    .init_instance = flecsEngineFrameCaptureInitInstance,
    .configure_target = flecsEngineFrameCaptureConfigureTarget,
    .prepare_frame = flecsEngineFrameCapturePrepareFrame,
    .acquire_frame = flecsEngineFrameCaptureAcquireFrame,
    .encode_frame = flecsEngineFrameCaptureEncodeFrame,
    .submit_frame = flecsEngineFrameCaptureSubmitFrame,
    .on_frame_failed = flecsEngineFrameCaptureOnFrameFailed,
    .cleanup = flecsEngineFrameCaptureCleanup
};

static void FlecsFrameOutputOnSet(
    ecs_iter_t *it)
{
    if (ecs_singleton_get(it->world, FlecsEngineImpl)) {
        return;
    }

    FlecsFrameOutput *outputs = ecs_field(it, FlecsFrameOutput, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        FlecsEngineFrameCaptureOutputConfig output_cfg = {
            .path = outputs[i].path
        };

        FlecsEngineOutputDesc output_desc = {
            .ops = &flecsEngineFrameCaptureOutputOps,
            .config = &output_cfg,
            .width = outputs[i].width,
            .height = outputs[i].height,
            .clear_color = outputs[i].clear_color
        };

        if (flecsEngineInit(it->world, &output_desc)) {
            ecs_err("Failed to initialize engine frame-output mode\n");
            ecs_quit(it->world);
        }

        break;
    }
}

void FlecsEngineFrameCaptureImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineFrameCapture);

    ecs_set_name_prefix(world, "Flecs");

    ecs_set_scope(world, ecs_get_parent(world, ecs_id(FlecsEngineFrameCapture)));

    ECS_META_COMPONENT(world, FlecsFrameOutput);

    ecs_set_hooks(world, FlecsFrameOutput, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsFrameOutputOnSet
    });
}
