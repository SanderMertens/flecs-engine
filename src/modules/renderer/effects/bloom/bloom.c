#include <math.h>

#include "../../renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsBloom);
ECS_COMPONENT_DECLARE(FlecsBloomImpl);

#define FLECS_ENGINE_BLOOM_PREFERRED_TEXTURE_FORMAT (WGPUTextureFormat_RG11B10Ufloat)
#define FLECS_ENGINE_BLOOM_MAX_MIP_COUNT (12u)

typedef struct FlecsBloomUniform {
    float threshold_precomputations[4];
    float viewport[4];
    float scale[2];
    float aspect;
    float _padding;
} FlecsBloomUniform;

static const char *kPlaceholderShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  return textureSample(input_texture, input_sampler, in.uv);\n"
    "}\n";

static const char *kBloomShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct BloomUniforms {\n"
    "  threshold_precomputations : vec4<f32>,\n"
    "  viewport : vec4<f32>,\n"
    "  scale : vec2<f32>,\n"
    "  aspect : f32,\n"
    "  _padding : f32,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var bloom_sampler : sampler;\n"
    "@group(0) @binding(2) var<uniform> uniforms : BloomUniforms;\n"
    "fn soft_threshold(color : vec3<f32>) -> vec3<f32> {\n"
    "  let brightness = max(color.r, max(color.g, color.b));\n"
    "  var softness = brightness - uniforms.threshold_precomputations.y;\n"
    "  softness = clamp(softness, 0.0, uniforms.threshold_precomputations.z);\n"
    "  softness = softness * softness * uniforms.threshold_precomputations.w;\n"
    "  var contribution = max(brightness - uniforms.threshold_precomputations.x, softness);\n"
    "  contribution /= max(brightness, 0.00001);\n"
    "  return color * contribution;\n"
    "}\n"
    "fn tonemapping_luminance(v : vec3<f32>) -> f32 {\n"
    "  return dot(v, vec3<f32>(0.2126, 0.7152, 0.0722));\n"
    "}\n"
    "fn karis_average(color : vec3<f32>) -> f32 {\n"
    "  let luma = tonemapping_luminance(color) / 4.0;\n"
    "  return 1.0 / (1.0 + luma);\n"
    "}\n"
    "fn sample_input_13_tap_uniform(uv : vec2<f32>, first_downsample : bool) -> vec3<f32> {\n"
    "  let a = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2,  2)).rgb;\n"
    "  let b = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0,  2)).rgb;\n"
    "  let c = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2,  2)).rgb;\n"
    "  let d = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2,  0)).rgb;\n"
    "  let e = textureSample(input_texture, bloom_sampler, uv).rgb;\n"
    "  let f = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2,  0)).rgb;\n"
    "  let g = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2, -2)).rgb;\n"
    "  let h = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0, -2)).rgb;\n"
    "  let i = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2, -2)).rgb;\n"
    "  let j = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1,  1)).rgb;\n"
    "  let k = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1,  1)).rgb;\n"
    "  let l = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1, -1)).rgb;\n"
    "  let m = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1, -1)).rgb;\n"
    "  if (first_downsample) {\n"
    "    var group0 = (a + b + d + e) * (0.125 / 4.0);\n"
    "    var group1 = (b + c + e + f) * (0.125 / 4.0);\n"
    "    var group2 = (d + e + g + h) * (0.125 / 4.0);\n"
    "    var group3 = (e + f + h + i) * (0.125 / 4.0);\n"
    "    var group4 = (j + k + l + m) * (0.5 / 4.0);\n"
    "    group0 *= karis_average(group0);\n"
    "    group1 *= karis_average(group1);\n"
    "    group2 *= karis_average(group2);\n"
    "    group3 *= karis_average(group3);\n"
    "    group4 *= karis_average(group4);\n"
    "    return group0 + group1 + group2 + group3 + group4;\n"
    "  }\n"
    "  var sample = (a + c + g + i) * 0.03125;\n"
    "  sample += (b + d + f + h) * 0.0625;\n"
    "  sample += (e + j + k + l + m) * 0.125;\n"
    "  return sample;\n"
    "}\n"
    "fn sample_input_13_tap_scaled(uv : vec2<f32>, first_downsample : bool) -> vec3<f32> {\n"
    "  let ps = uniforms.scale / vec2<f32>(textureDimensions(input_texture));\n"
    "  let pl = 2.0 * ps;\n"
    "  let ns = -1.0 * ps;\n"
    "  let nl = -2.0 * ps;\n"
    "  let a = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(nl.x, pl.y)).rgb;\n"
    "  let b = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(0.00, pl.y)).rgb;\n"
    "  let c = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(pl.x, pl.y)).rgb;\n"
    "  let d = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(nl.x, 0.00)).rgb;\n"
    "  let e = textureSample(input_texture, bloom_sampler, uv).rgb;\n"
    "  let f = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(pl.x, 0.00)).rgb;\n"
    "  let g = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(nl.x, nl.y)).rgb;\n"
    "  let h = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(0.00, nl.y)).rgb;\n"
    "  let i = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(pl.x, nl.y)).rgb;\n"
    "  let j = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(ns.x, ps.y)).rgb;\n"
    "  let k = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(ps.x, ps.y)).rgb;\n"
    "  let l = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(ns.x, ns.y)).rgb;\n"
    "  let m = textureSample(input_texture, bloom_sampler, uv + vec2<f32>(ps.x, ns.y)).rgb;\n"
    "  if (first_downsample) {\n"
    "    var group0 = (a + b + d + e) * (0.125 / 4.0);\n"
    "    var group1 = (b + c + e + f) * (0.125 / 4.0);\n"
    "    var group2 = (d + e + g + h) * (0.125 / 4.0);\n"
    "    var group3 = (e + f + h + i) * (0.125 / 4.0);\n"
    "    var group4 = (j + k + l + m) * (0.5 / 4.0);\n"
    "    group0 *= karis_average(group0);\n"
    "    group1 *= karis_average(group1);\n"
    "    group2 *= karis_average(group2);\n"
    "    group3 *= karis_average(group3);\n"
    "    group4 *= karis_average(group4);\n"
    "    return group0 + group1 + group2 + group3 + group4;\n"
    "  }\n"
    "  var sample = (a + c + g + i) * 0.03125;\n"
    "  sample += (b + d + f + h) * 0.0625;\n"
    "  sample += (e + j + k + l + m) * 0.125;\n"
    "  return sample;\n"
    "}\n"
    "fn sample_input_13_tap(uv : vec2<f32>, first_downsample : bool) -> vec3<f32> {\n"
    "  let uniform_scale = abs(uniforms.scale.x - 1.0) < 0.0001 && abs(uniforms.scale.y - 1.0) < 0.0001;\n"
    "  if (uniform_scale) {\n"
    "    return sample_input_13_tap_uniform(uv, first_downsample);\n"
    "  }\n"
    "  return sample_input_13_tap_scaled(uv, first_downsample);\n"
    "}\n"
    "fn sample_input_3x3_tent(uv : vec2<f32>) -> vec3<f32> {\n"
    "  let frag_size = uniforms.scale / vec2<f32>(textureDimensions(input_texture));\n"
    "  let x = frag_size.x;\n"
    "  let y = frag_size.y;\n"
    "  let a = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x - x, uv.y + y)).rgb;\n"
    "  let b = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x, uv.y + y)).rgb;\n"
    "  let c = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x + x, uv.y + y)).rgb;\n"
    "  let d = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x - x, uv.y)).rgb;\n"
    "  let e = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x, uv.y)).rgb;\n"
    "  let f = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x + x, uv.y)).rgb;\n"
    "  let g = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x - x, uv.y - y)).rgb;\n"
    "  let h = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x, uv.y - y)).rgb;\n"
    "  let i = textureSample(input_texture, bloom_sampler, vec2<f32>(uv.x + x, uv.y - y)).rgb;\n"
    "  var sample = e * 0.25;\n"
    "  sample += (b + d + f + h) * 0.125;\n"
    "  sample += (a + c + g + i) * 0.0625;\n"
    "  return sample;\n"
    "}\n"
    "@fragment fn downsample_first(@location(0) output_uv : vec2<f32>) -> @location(0) vec4<f32> {\n"
    "  let sample_uv = uniforms.viewport.xy + output_uv * uniforms.viewport.zw;\n"
    "  var sample = sample_input_13_tap(sample_uv, true);\n"
    "  sample = clamp(sample, vec3<f32>(0.0001), vec3<f32>(3.40282347e+37));\n"
    "  if (uniforms.threshold_precomputations.x > 0.0 || uniforms.threshold_precomputations.z > 0.0) {\n"
    "    sample = soft_threshold(sample);\n"
    "  }\n"
    "  return vec4<f32>(sample, 1.0);\n"
    "}\n"
    "@fragment fn downsample(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(sample_input_13_tap(uv, false), 1.0);\n"
    "}\n"
    "@fragment fn upsample(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(sample_input_3x3_tent(uv), 1.0);\n"
    "}\n";

FlecsBloom flecsEngine_bloomSettingsDefault(void)
{
    return (FlecsBloom){
        .intensity = 0.3f,
        .low_frequency_boost = 0.7f,
        .low_frequency_boost_curvature = 0.95f,
        .high_pass_frequency = 1.0f,
        .prefilter = {
            .threshold = 1.0f,
            .threshold_softness = 0.0f
        },
        .mip_count = 6u,
        .scale_x = 1.0f,
        .scale_y = 1.0f
    };
}

static uint32_t flecsEngine_bloom_computeMipCount(
    const FlecsBloom *settings)
{
    uint32_t count = settings->mip_count;
    if (count < 2u) {
        count = 2u;
    }
    if (count > FLECS_ENGINE_BLOOM_MAX_MIP_COUNT) {
        count = FLECS_ENGINE_BLOOM_MAX_MIP_COUNT;
    }
    return count;
}

static void flecsEngine_bloom_computeTextureSize(
    const FlecsEngineImpl *engine,
    uint32_t *out_width,
    uint32_t *out_height)
{
    if (engine->actual_height <= 0 || engine->actual_width <= 0) {
        *out_width = 1u;
        *out_height = 1u;
        return;
    }

    uint32_t width = (uint32_t)engine->actual_width / 2u;
    uint32_t height = (uint32_t)engine->actual_height / 2u;
    if (!width) {
        width = 1u;
    }
    if (!height) {
        height = 1u;
    }

    *out_width = width;
    *out_height = height;
}

static void flecsEngine_bloom_releaseTexture(
    FlecsBloomImpl *bloom)
{
    if (bloom->mip_views) {
        for (uint32_t i = 0; i < bloom->mip_count; i ++) {
            if (bloom->mip_views[i]) {
                wgpuTextureViewRelease(bloom->mip_views[i]);
            }
        }
        ecs_os_free(bloom->mip_views);
        bloom->mip_views = NULL;
    }

    if (bloom->texture) {
        wgpuTextureRelease(bloom->texture);
        bloom->texture = NULL;
    }

    bloom->mip_count = 0;
    bloom->texture_width = 0;
    bloom->texture_height = 0;
    bloom->texture_format = WGPUTextureFormat_Undefined;
}

static void flecsEngine_bloom_releaseResources(
    FlecsBloomImpl *bloom)
{
    flecsEngine_bloom_releaseTexture(bloom);

    if (bloom->uniform_buffer) {
        wgpuBufferRelease(bloom->uniform_buffer);
        bloom->uniform_buffer = NULL;
    }

    if (bloom->sampler) {
        wgpuSamplerRelease(bloom->sampler);
        bloom->sampler = NULL;
    }

    if (bloom->upsample_final_hdr_pipeline) {
        wgpuRenderPipelineRelease(bloom->upsample_final_hdr_pipeline);
        bloom->upsample_final_hdr_pipeline = NULL;
    }

    if (bloom->upsample_final_surface_pipeline) {
        wgpuRenderPipelineRelease(bloom->upsample_final_surface_pipeline);
        bloom->upsample_final_surface_pipeline = NULL;
    }

    if (bloom->upsample_pipeline) {
        wgpuRenderPipelineRelease(bloom->upsample_pipeline);
        bloom->upsample_pipeline = NULL;
    }

    if (bloom->downsample_pipeline) {
        wgpuRenderPipelineRelease(bloom->downsample_pipeline);
        bloom->downsample_pipeline = NULL;
    }

    if (bloom->downsample_first_pipeline) {
        wgpuRenderPipelineRelease(bloom->downsample_first_pipeline);
        bloom->downsample_first_pipeline = NULL;
    }

    if (bloom->bind_layout) {
        wgpuBindGroupLayoutRelease(bloom->bind_layout);
        bloom->bind_layout = NULL;
    }
}

ECS_DTOR(FlecsBloomImpl, ptr, {
    flecsEngine_bloom_releaseResources(ptr);
})

ECS_MOVE(FlecsBloomImpl, dst, src, {
    flecsEngine_bloom_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static bool flecsEngine_bloom_createTexture(
    const FlecsEngineImpl *engine,
    FlecsBloomImpl *bloom,
    uint32_t width,
    uint32_t height,
    uint32_t mip_count,
    WGPUTextureFormat format)
{
    WGPUTextureDescriptor texture_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = format,
        .mipLevelCount = mip_count,
        .sampleCount = 1
    };

    bloom->texture = wgpuDeviceCreateTexture(engine->device, &texture_desc);
    if (!bloom->texture) {
        return false;
    }

    bloom->mip_views = ecs_os_calloc_n(WGPUTextureView, mip_count);
    if (!bloom->mip_views) {
        flecsEngine_bloom_releaseTexture(bloom);
        return false;
    }

    for (uint32_t i = 0; i < mip_count; i ++) {
        WGPUTextureViewDescriptor view_desc = {
            .format = format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = i,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };

        bloom->mip_views[i] = wgpuTextureCreateView(
            bloom->texture, &view_desc);
        if (!bloom->mip_views[i]) {
            flecsEngine_bloom_releaseTexture(bloom);
            return false;
        }
    }

    bloom->mip_count = mip_count;
    bloom->texture_width = width;
    bloom->texture_height = height;
    bloom->texture_format = format;
    return true;
}

static bool flecsEngine_bloom_ensureTexture(
    const FlecsEngineImpl *engine,
    const FlecsBloom *bloom,
    FlecsBloomImpl *impl)
{
    uint32_t width = 0;
    uint32_t height = 0;
    flecsEngine_bloom_computeTextureSize(engine, &width, &height);
    uint32_t mip_count = flecsEngine_bloom_computeMipCount(bloom);

    /* Clamp to the maximum mip count the texture dimensions support */
    uint32_t max_dim = width > height ? width : height;
    uint32_t max_mips = 1u;
    while ((1u << max_mips) <= max_dim) {
        max_mips ++;
    }
    if (mip_count > max_mips) {
        mip_count = max_mips;
    }

    if (impl->texture &&
        impl->mip_views &&
        impl->texture_width == width &&
        impl->texture_height == height &&
        impl->mip_count == mip_count &&
        impl->texture_format == flecsEngine_getHdrFormat(engine))
    {
        return true;
    }

    flecsEngine_bloom_releaseTexture(impl);

    WGPUTextureFormat working_format = flecsEngine_getHdrFormat(engine);

    if (flecsEngine_bloom_createTexture(
        engine,
        impl,
        width,
        height,
        mip_count,
        working_format))
    {
        return true;
    }

    return false;
}

static ecs_entity_t flecsEngine_bloom_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "BloomEffectPlaceholderShader",
        &(FlecsShader){
            .source = kPlaceholderShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static WGPURenderPipeline flecsEngine_bloom_createPipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule shader_module,
    WGPUBindGroupLayout bind_layout,
    const char *fragment_entry,
    WGPUTextureFormat color_format,
    const WGPUBlendState *blend_state)
{
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &bind_layout
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &pipeline_layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUColorTargetState color_target = {
        .format = color_format,
        .blend = (WGPUBlendState*)blend_state,
        .writeMask = WGPUColorWriteMask_All
    };

    WGPUVertexState vertex_state = {
        .module = shader_module,
        .entryPoint = WGPU_STR("vs_main")
    };

    WGPUFragmentState fragment_state = {
        .module = shader_module,
        .entryPoint = WGPU_STR(fragment_entry),
        .targetCount = 1,
        .targets = &color_target
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = vertex_state,
        .fragment = &fragment_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = WGPU_MULTISAMPLE_DEFAULT
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

static WGPUBlendState flecsEngine_bloom_getBlendState(void)
{
    WGPUBlendComponent color = {
        .srcFactor = WGPUBlendFactor_Constant,
        .dstFactor = WGPUBlendFactor_One,
        .operation = WGPUBlendOperation_Add
    };

    WGPUBlendComponent alpha = {
        .srcFactor = WGPUBlendFactor_Zero,
        .dstFactor = WGPUBlendFactor_One,
        .operation = WGPUBlendOperation_Add
    };

    return (WGPUBlendState){
        .color = color,
        .alpha = alpha
    };
}

static bool flecsEngine_bloom_runPass(
    const FlecsEngineImpl *engine,
    const FlecsBloomImpl *bloom,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUTextureView source_view,
    WGPUTextureView target_view,
    WGPULoadOp load_op,
    bool use_blend_constant,
    float blend_value)
{
    WGPUBindGroupEntry bind_entries[3] = {
        { .binding = 0, .textureView = source_view },
        { .binding = 1, .sampler = bloom->sampler },
        {
            .binding = 2,
            .buffer = bloom->uniform_buffer,
            .offset = 0,
            .size = sizeof(FlecsBloomUniform)
        }
    };

    WGPUBindGroupDescriptor bind_desc = {
        .layout = bloom->bind_layout,
        .entryCount = 3,
        .entries = bind_entries
    };

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(engine->device, &bind_desc);
    if (!bind_group) {
        return false;
    }

    WGPURenderPassColorAttachment color_attachment = {
        .view = target_view,
        WGPU_DEPTH_SLICE
        .loadOp = load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuBindGroupRelease(bind_group);
        return false;
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    if (use_blend_constant) {
        WGPUColor blend = {
            .r = blend_value,
            .g = blend_value,
            .b = blend_value,
            .a = blend_value
        };
        wgpuRenderPassEncoderSetBlendConstant(pass, &blend);
    }
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bind_group);
    return true;
}

static float flecsEngine_bloom_computeBlendFactor(
    const FlecsBloom *bloom,
    float mip,
    float max_mip)
{
    if (max_mip <= 0.0f) {
        return bloom->intensity;
    }

    float normalized = mip / max_mip;
    float curve = 1.0f / (1.0f - bloom->low_frequency_boost_curvature);
    float lf_boost = (1.0f - powf(1.0f - normalized, curve)) *
        bloom->low_frequency_boost;
    float high_pass_lq = 1.0f -
        glm_clamp(
            (normalized - bloom->high_pass_frequency) /
                bloom->high_pass_frequency,
            0.0f,
            1.0f);

    lf_boost *= 1.0f - bloom->intensity;

    return (bloom->intensity + lf_boost) * high_pass_lq;
}

static void flecsEngine_bloom_fillUniform(
    const FlecsEngineImpl *engine,
    const FlecsBloom *settings,
    FlecsBloomUniform *uniform)
{
    float knee = settings->prefilter.threshold *
        glm_clamp(settings->prefilter.threshold_softness, 0.0f, 1.0f);

    uniform->threshold_precomputations[0] = settings->prefilter.threshold;
    uniform->threshold_precomputations[1] = settings->prefilter.threshold - knee;
    uniform->threshold_precomputations[2] = 2.0f * knee;
    uniform->threshold_precomputations[3] = 0.25f / (knee + 0.00001f);

    uniform->viewport[0] = 0.0f;
    uniform->viewport[1] = 0.0f;
    uniform->viewport[2] = 1.0f;
    uniform->viewport[3] = 1.0f;

    uniform->scale[0] = settings->scale_x;
    uniform->scale[1] = settings->scale_y;
    uniform->aspect = engine->actual_height > 0
        ? (float)engine->actual_width / (float)engine->actual_height
        : 1.0f;
    uniform->_padding = 0.0f;
}

static bool flecsEngine_bloom_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)effect_impl;
    (void)layout_entries;

    ecs_assert(effect != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    FlecsBloomImpl bloom = {0};

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

    bloom.sampler = wgpuDeviceCreateSampler(engine->device, &sampler_desc);
    if (!bloom.sampler) {
        return false;
    }

    WGPUBufferDescriptor uniform_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(FlecsBloomUniform)
    };

    bloom.uniform_buffer = wgpuDeviceCreateBuffer(engine->device, &uniform_desc);
    if (!bloom.uniform_buffer) {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    WGPUBindGroupLayoutEntry bloom_layout_entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsBloomUniform)
            }
        }
    };

    WGPUBindGroupLayoutDescriptor bloom_bind_layout_desc = {
        .entryCount = 3,
        .entries = bloom_layout_entries
    };

    bloom.bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device, &bloom_bind_layout_desc);
    if (!bloom.bind_layout) {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    WGPUShaderModule bloom_shader = flecsEngine_createShaderModule(
        engine->device, kBloomShaderSource);
    if (!bloom_shader) {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    WGPUTextureFormat surface_format = engine->surface_config.format;
    WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);
    WGPUTextureFormat bloom_format = hdr_format;

    WGPUBlendState blend_state = flecsEngine_bloom_getBlendState();

    bloom.downsample_first_pipeline = flecsEngine_bloom_createPipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        "downsample_first",
        bloom_format,
        NULL);
    bloom.downsample_pipeline = flecsEngine_bloom_createPipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        "downsample",
        bloom_format,
        NULL);
    bloom.upsample_pipeline = flecsEngine_bloom_createPipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        "upsample",
        bloom_format,
        &blend_state);
    bloom.upsample_final_surface_pipeline = flecsEngine_bloom_createPipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        "upsample",
        surface_format,
        &blend_state);
    bloom.upsample_final_hdr_pipeline = flecsEngine_bloom_createPipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        "upsample",
        hdr_format,
        &blend_state);

    wgpuShaderModuleRelease(bloom_shader);

    if (!bloom.downsample_first_pipeline ||
        !bloom.downsample_pipeline ||
        !bloom.upsample_pipeline ||
        !bloom.upsample_final_surface_pipeline ||
        !bloom.upsample_final_hdr_pipeline)
    {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsBloomImpl, &bloom);
    return true;
}

static bool flecsEngine_bloom_renderPassthrough(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *effect_impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView input_view,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    WGPURenderPassColorAttachment color_attachment = {
        .view = output_view,
        WGPU_DEPTH_SLICE
        .loadOp = output_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        return false;
    }

    flecsEngine_renderEffect_render(
        world,
        engine,
        pass,
        effect_entity,
        effect,
        effect_impl,
        input_view,
        output_format);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

static bool flecsEngine_bloom_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *effect_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat input_format,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    (void)input_format;
    FlecsBloomImpl *impl = ecs_get_mut(world, effect_entity, FlecsBloomImpl);
    ecs_assert(impl != NULL, ECS_INVALID_OPERATION, NULL);

    const FlecsBloom *bloom = ecs_get(world, effect_entity, FlecsBloom);
    ecs_assert(bloom != NULL, ECS_INVALID_OPERATION, NULL);

    if (bloom->intensity <= 0.0f) {
        return flecsEngine_bloom_renderPassthrough(
            world,
            engine,
            effect_entity,
            effect,
            effect_impl,
            encoder,
            input_view,
            output_view,
            output_format,
            output_load_op);
    }

    if (!flecsEngine_bloom_ensureTexture(engine, bloom, impl)) {
        return false;
    }

    FlecsBloomUniform uniform = {0};
    flecsEngine_bloom_fillUniform(engine, bloom, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        impl->uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    if (!flecsEngine_bloom_runPass(
        engine,
        impl,
        encoder,
        impl->downsample_first_pipeline,
        input_view,
        impl->mip_views[0],
        WGPULoadOp_Clear,
        false,
        0.0f))
    {
        return false;
    }

    for (uint32_t mip = 1; mip < impl->mip_count; mip ++) {
        if (!flecsEngine_bloom_runPass(
            engine,
            impl,
            encoder,
            impl->downsample_pipeline,
            impl->mip_views[mip - 1],
            impl->mip_views[mip],
            WGPULoadOp_Clear,
            false,
            0.0f))
        {
            return false;
        }
    }

    float max_mip = (float)(impl->mip_count - 1u);
    for (uint32_t mip = impl->mip_count - 1u; mip > 0u; mip --) {
        float blend = flecsEngine_bloom_computeBlendFactor(
            bloom, mip, max_mip);
        if (!flecsEngine_bloom_runPass(
            engine,
            impl,
            encoder,
            impl->upsample_pipeline,
            impl->mip_views[mip],
            impl->mip_views[mip - 1u],
            WGPULoadOp_Load,
            true,
            blend))
        {
            return false;
        }
    }

    WGPURenderPipeline final_pipeline = output_format == engine->surface_config.format
        ? impl->upsample_final_surface_pipeline
        : impl->upsample_final_hdr_pipeline;

    float final_blend = flecsEngine_bloom_computeBlendFactor(
        bloom, 0.0f, max_mip);

    if (!flecsEngine_bloom_renderPassthrough(
        world,
        engine,
        effect_entity,
        effect,
        effect_impl,
        encoder,
        input_view,
        output_view,
        output_format,
        output_load_op))
    {
        return false;
    }

    return flecsEngine_bloom_runPass(
        engine,
        impl,
        encoder,
        final_pipeline,
        impl->mip_views[0],
        output_view,
        WGPULoadOp_Load,
        true,
        final_blend);
}

ecs_entity_t flecsEngine_createEffect_bloom(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsBloom *settings)
{
    ecs_entity_t effect = ecs_entity(world, { .parent = parent, .name = name });
    ecs_set_ptr(world, effect, FlecsBloom, settings);

    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsEngine_bloom_shader(world),
        .input = input,
        .setup_callback = flecsEngine_bloom_setup,
        .render_callback = flecsEngine_bloom_render
    });

    return effect;
}

void flecsEngine_bloom_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsBloom);
    ECS_COMPONENT_DEFINE(world, FlecsBloomImpl);

    ecs_set_hooks(world, FlecsBloomImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsBloomImpl),
        .dtor = ecs_dtor(FlecsBloomImpl)
    });

    ecs_entity_t bloom_prefilter_t = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "FlecsBloomPrefilter" }),
        .members = {
            { .name = "threshold", .type = ecs_id(ecs_f32_t) },
            { .name = "threshold_softness", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBloom),
        .members = {
            { .name = "intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "low_frequency_boost", .type = ecs_id(ecs_f32_t) },
            { .name = "low_frequency_boost_curvature", .type = ecs_id(ecs_f32_t) },
            { .name = "high_pass_frequency", .type = ecs_id(ecs_f32_t) },
            { .name = "prefilter", .type = bloom_prefilter_t },
            { .name = "mip_count", .type = ecs_id(ecs_u32_t) },
            { .name = "scale_x", .type = ecs_id(ecs_f32_t) },
            { .name = "scale_y", .type = ecs_id(ecs_f32_t) }
        }
    });
}
