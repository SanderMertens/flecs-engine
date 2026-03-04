#include "../../renderer.h"
#include "tony_mc_mapface_lut.h"
#include "flecs_engine.h"

static const char *kShaderSource =
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>\n"
    "};\n"
    "@vertex fn vs_main(@builtin(vertex_index) vid : u32) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  var pos = array<vec2<f32>, 3>(\n"
    "      vec2<f32>(-1.0, -1.0),\n"
    "      vec2<f32>(3.0, -1.0),\n"
    "      vec2<f32>(-1.0, 3.0));\n"
    "  let p = pos[vid];\n"
    "  out.pos = vec4<f32>(p, 0.0, 1.0);\n"
    "  out.uv = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);\n"
    "  return out;\n"
    "}\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var tony_lut : texture_3d<f32>;\n"
    "@group(0) @binding(3) var tony_lut_sampler : sampler;\n"
    "fn interleaved_gradient_noise(pixel : vec2<f32>) -> f32 {\n"
    "  return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let encoded = src.rgb / (src.rgb + vec3<f32>(1.0));\n"
    "  let dims = 48.0;\n"
    "  let uv = clamp(encoded * ((dims - 1.0) / dims) + (0.5 / dims), vec3<f32>(0.0), vec3<f32>(1.0));\n"
    "  let mapped = textureSampleLevel(tony_lut, tony_lut_sampler, uv, 0.0).rgb;\n"
    "  let tex_size = vec2<f32>(textureDimensions(input_texture));\n"
    "  let pixel = floor(in.uv * tex_size);\n"
    "  let dither = (interleaved_gradient_noise(pixel) - 0.5) / 255.0;\n"
    "  let dithered = clamp(mapped + vec3<f32>(dither), vec3<f32>(0.0), vec3<f32>(1.0));\n"
    "  return vec4<f32>(dithered, src.a);\n"
    "}\n";

static ecs_entity_t flecsRenderEffect_tonyMcMapFace_shader(
    ecs_world_t *world)
{
    return flecsEngineEnsureShader(world, "TonyMcMapfaceShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsTonyReleaseResources(
    FlecsTonyImpl *impl)
{
    if (impl->tony_lut_sampler) {
        wgpuSamplerRelease(impl->tony_lut_sampler);
        impl->tony_lut_sampler = NULL;
    }

    if (impl->tony_lut_texture_view) {
        wgpuTextureViewRelease(impl->tony_lut_texture_view);
        impl->tony_lut_texture_view = NULL;
    }

    if (impl->tony_lut_texture) {
        wgpuTextureRelease(impl->tony_lut_texture);
        impl->tony_lut_texture = NULL;
    }
}

ECS_DTOR(FlecsTonyImpl, ptr, {
    flecsTonyReleaseResources(ptr);
})

ECS_MOVE(FlecsTonyImpl, dst, src, {
    flecsTonyReleaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static bool flecsRenderEffect_tonyMcMapFace_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)effect_impl;
    (void)effect;

    ecs_assert(layout_entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    };

    FlecsTonyImpl tony = {0};

    tony.tony_lut_sampler = wgpuDeviceCreateSampler(engine->device, &sampler_desc);
    if (!tony.tony_lut_sampler) {
        return false;
    }

    WGPUTextureDescriptor lut_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = (WGPUExtent3D){
            .width = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_WIDTH,
            .height = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_HEIGHT,
            .depthOrArrayLayers = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_DEPTH
        },
        .format = WGPUTextureFormat_RGB9E5Ufloat,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    tony.tony_lut_texture = wgpuDeviceCreateTexture(engine->device, &lut_desc);
    if (!tony.tony_lut_texture) {
        flecsTonyReleaseResources(&tony);
        return false;
    }

    WGPUTextureViewDescriptor lut_view_desc = {
        .format = WGPUTextureFormat_RGB9E5Ufloat,
        .dimension = WGPUTextureViewDimension_3D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All,
        .usage = WGPUTextureUsage_TextureBinding
    };

    tony.tony_lut_texture_view = wgpuTextureCreateView(
        tony.tony_lut_texture, &lut_view_desc);
    if (!tony.tony_lut_texture_view) {
        flecsTonyReleaseResources(&tony);
        return false;
    }

    WGPUTexelCopyTextureInfo destination = {
        .texture = tony.tony_lut_texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferLayout data_layout = {
        .offset = 0,
        .bytesPerRow = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_WIDTH * sizeof(uint32_t),
        .rowsPerImage = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_HEIGHT
    };

    WGPUExtent3D write_size = {
        .width = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_WIDTH,
        .height = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_HEIGHT,
        .depthOrArrayLayers = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_DEPTH
    };

    wgpuQueueWriteTexture(
        engine->queue,
        &destination,
        flecs_engine_tony_mc_mapface_lut,
        sizeof(uint32_t) * FLECS_ENGINE_TONY_MC_MAPFACE_LUT_TEXEL_COUNT,
        &data_layout,
        &write_size);

    layout_entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_3D,
            .multisampled = false
        }
    };

    layout_entries[3] = (WGPUBindGroupLayoutEntry){
        .binding = 3,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = {
            .type = WGPUSamplerBindingType_Filtering
        }
    };

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsTonyImpl, &tony);

    *entry_count = 4;
    return true;
}

static bool flecsRenderEffect_tonyMcMapFace_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)engine;
    (void)effect;
    (void)impl;

    ecs_assert(entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    const FlecsTonyImpl *tony = ecs_get(world, effect_entity, FlecsTonyImpl);
    if (!tony) {
        return false;
    }

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2,
        .textureView = tony->tony_lut_texture_view
    };

    entries[3] = (WGPUBindGroupEntry){
        .binding = 3,
        .sampler = tony->tony_lut_sampler
    };

    *entry_count = 4;
    return true;
}

ecs_entity_t flecsEngine_createEffect_tonyMcMapFace(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input)
{
    ecs_entity_t effect = ecs_entity(world, { .parent = parent, .name = name });
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsRenderEffect_tonyMcMapFace_shader(world),
        .input = input,
        .setup_callback = flecsRenderEffect_tonyMcMapFace_setup,
        .bind_callback = flecsRenderEffect_tonyMcMapFace_bind
    });

    return effect;
}
