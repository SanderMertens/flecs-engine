#include <math.h>

#include "../../renderer.h"
#include "flecs_engine.h"

#define FLECS_ENGINE_BLOOM_PREFERRED_TEXTURE_FORMAT (WGPUTextureFormat_RG11B10Ufloat)
#define FLECS_ENGINE_BLOOM_MAX_DIMENSION (8192u)

typedef struct FlecsBloomUniform {
    float threshold_precomputations[4];
    float viewport[4];
    float scale[2];
    float aspect;
    float _padding;
} FlecsBloomUniform;

static const char *kPlaceholderShaderSource =
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
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  return textureSample(input_texture, input_sampler, in.uv);\n"
    "}\n";

static const char *kBloomShaderSource =
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>\n"
    "};\n"
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

static float flecsBloomClampFloat(
    float value,
    float min_value,
    float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static WGPUTextureFormat flecsBloomChooseWorkingFormat(
    const FlecsEngineImpl *engine)
{
    if (wgpuDeviceHasFeature(
        engine->device,
        WGPUFeatureName_RG11B10UfloatRenderable))
    {
        return FLECS_ENGINE_BLOOM_PREFERRED_TEXTURE_FORMAT;
    }

    if (engine->hdr_color_format != WGPUTextureFormat_Undefined) {
        return engine->hdr_color_format;
    }

    return engine->surface_config.format;
}

FlecsBloomSettings flecsEngine_bloomSettingsDefault(void)
{
    return (FlecsBloomSettings){
        .intensity = 0.15f,
        .low_frequency_boost = 0.5f,
        .low_frequency_boost_curvature = 0.95f,
        .high_pass_frequency = 1.0f,
        .prefilter = {
            .threshold = 0.0f,
            .threshold_softness = 0.0f
        },
        .composite_mode = FlecsBloomCompositeMode_EnergyConserving,
        .max_mip_dimension = 512u,
        .scale_x = 1.0f,
        .scale_y = 1.0f
    };
}

FlecsBloomSettings flecsEngine_bloomSettingsAnamorphic(void)
{
    FlecsBloomSettings settings = flecsEngine_bloomSettingsDefault();
    settings.max_mip_dimension *= 2u;
    settings.scale_x = 4.0f;
    settings.scale_y = 1.0f;
    return settings;
}

FlecsBloomSettings flecsEngine_bloomSettingsOldSchool(void)
{
    FlecsBloomSettings settings = flecsEngine_bloomSettingsDefault();
    settings.intensity = 0.05f;
    settings.prefilter.threshold = 0.6f;
    settings.prefilter.threshold_softness = 0.2f;
    settings.composite_mode = FlecsBloomCompositeMode_Additive;
    return settings;
}

FlecsBloomSettings flecsEngine_bloomSettingsScreenBlur(void)
{
    FlecsBloomSettings settings = flecsEngine_bloomSettingsDefault();
    settings.intensity = 1.0f;
    settings.low_frequency_boost = 0.0f;
    settings.low_frequency_boost_curvature = 0.0f;
    settings.high_pass_frequency = 1.0f / 3.0f;
    return settings;
}

static FlecsBloomSettings flecsBloomSanitizeSettings(
    const FlecsBloomSettings *settings)
{
    FlecsBloomSettings result = settings
        ? *settings
        : flecsEngine_bloomSettingsDefault();

    if (!isfinite(result.intensity) || result.intensity < 0.0f) {
        result.intensity = 0.0f;
    }
    if (!isfinite(result.low_frequency_boost) || result.low_frequency_boost < 0.0f) {
        result.low_frequency_boost = 0.0f;
    }
    if (!isfinite(result.low_frequency_boost_curvature)) {
        result.low_frequency_boost_curvature = 0.0f;
    }
    result.low_frequency_boost_curvature = flecsBloomClampFloat(
        result.low_frequency_boost_curvature, 0.0f, 0.9999f);

    if (!isfinite(result.high_pass_frequency)) {
        result.high_pass_frequency = 1.0f;
    }
    result.high_pass_frequency = flecsBloomClampFloat(
        result.high_pass_frequency, 0.0001f, 1.0f);

    if (!isfinite(result.prefilter.threshold) || result.prefilter.threshold < 0.0f) {
        result.prefilter.threshold = 0.0f;
    }
    if (!isfinite(result.prefilter.threshold_softness)) {
        result.prefilter.threshold_softness = 0.0f;
    }
    result.prefilter.threshold_softness = flecsBloomClampFloat(
        result.prefilter.threshold_softness, 0.0f, 1.0f);

    if (result.composite_mode != FlecsBloomCompositeMode_EnergyConserving &&
        result.composite_mode != FlecsBloomCompositeMode_Additive)
    {
        result.composite_mode = FlecsBloomCompositeMode_EnergyConserving;
    }

    if (result.max_mip_dimension < 4u) {
        result.max_mip_dimension = 4u;
    }
    if (result.max_mip_dimension > FLECS_ENGINE_BLOOM_MAX_DIMENSION) {
        result.max_mip_dimension = FLECS_ENGINE_BLOOM_MAX_DIMENSION;
    }

    if (!isfinite(result.scale_x) || result.scale_x <= 0.0f) {
        result.scale_x = 1.0f;
    }
    if (!isfinite(result.scale_y) || result.scale_y <= 0.0f) {
        result.scale_y = 1.0f;
    }

    return result;
}

static uint32_t flecsBloomIlog2(
    uint32_t value)
{
    uint32_t result = 0;
    while (value > 1u) {
        value >>= 1u;
        result ++;
    }
    return result;
}

static uint32_t flecsBloomComputeMipCount(
    const FlecsBloomSettings *settings)
{
    uint32_t ilog = flecsBloomIlog2(settings->max_mip_dimension);
    if (ilog < 2u) {
        ilog = 2u;
    }
    return ilog - 1u;
}

static void flecsBloomComputeTextureSize(
    const FlecsEngineImpl *engine,
    const FlecsBloomSettings *settings,
    uint32_t *out_width,
    uint32_t *out_height)
{
    if (engine->height <= 0 || engine->width <= 0) {
        *out_width = 1u;
        *out_height = 1u;
        return;
    }

    float ratio = (float)settings->max_mip_dimension / (float)engine->height;
    uint32_t width = (uint32_t)lroundf((float)engine->width * ratio);
    uint32_t height = (uint32_t)lroundf((float)engine->height * ratio);
    if (!width) {
        width = 1u;
    }
    if (!height) {
        height = 1u;
    }

    *out_width = width;
    *out_height = height;
}

static void flecsBloomReleaseTexture(
    FlecsRenderEffectImpl *impl)
{
    if (impl->bloom_mip_views) {
        for (uint32_t i = 0; i < impl->bloom_mip_count; i ++) {
            if (impl->bloom_mip_views[i]) {
                wgpuTextureViewRelease(impl->bloom_mip_views[i]);
            }
        }
        ecs_os_free(impl->bloom_mip_views);
        impl->bloom_mip_views = NULL;
    }

    if (impl->bloom_texture) {
        wgpuTextureRelease(impl->bloom_texture);
        impl->bloom_texture = NULL;
    }

    impl->bloom_mip_count = 0;
    impl->bloom_texture_width = 0;
    impl->bloom_texture_height = 0;
    impl->bloom_texture_format = WGPUTextureFormat_Undefined;
}

static bool flecsBloomCreateTexture(
    const FlecsEngineImpl *engine,
    FlecsRenderEffectImpl *impl,
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

    impl->bloom_texture = wgpuDeviceCreateTexture(engine->device, &texture_desc);
    if (!impl->bloom_texture) {
        return false;
    }

    impl->bloom_mip_views = ecs_os_calloc_n(WGPUTextureView, mip_count);
    if (!impl->bloom_mip_views) {
        flecsBloomReleaseTexture(impl);
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

        impl->bloom_mip_views[i] = wgpuTextureCreateView(
            impl->bloom_texture, &view_desc);
        if (!impl->bloom_mip_views[i]) {
            flecsBloomReleaseTexture(impl);
            return false;
        }
    }

    impl->bloom_mip_count = mip_count;
    impl->bloom_texture_width = width;
    impl->bloom_texture_height = height;
    impl->bloom_texture_format = format;
    return true;
}

static bool flecsBloomEnsureTexture(
    const FlecsEngineImpl *engine,
    FlecsRenderEffectImpl *impl)
{
    uint32_t width = 0;
    uint32_t height = 0;
    flecsBloomComputeTextureSize(engine, &impl->bloom_settings, &width, &height);
    uint32_t mip_count = flecsBloomComputeMipCount(&impl->bloom_settings);

    if (impl->bloom_texture &&
        impl->bloom_mip_views &&
        impl->bloom_texture_width == width &&
        impl->bloom_texture_height == height &&
        impl->bloom_mip_count == mip_count &&
        impl->bloom_texture_format == flecsBloomChooseWorkingFormat(engine))
    {
        return true;
    }

    flecsBloomReleaseTexture(impl);

    WGPUTextureFormat working_format = flecsBloomChooseWorkingFormat(engine);

    if (flecsBloomCreateTexture(
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

static ecs_entity_t flecsRenderEffect_bloom_shader(
    ecs_world_t *world)
{
    return flecsEngineEnsureShader(world, "BloomEffectPlaceholderShader",
        &(FlecsShader){
            .source = kPlaceholderShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static WGPUShaderModule flecsBloomCreateShaderModule(
    const FlecsEngineImpl *engine,
    const char *source)
{
    WGPUShaderSourceWGSL wgsl_desc = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = (WGPUStringView){
            .data = source,
            .length = WGPU_STRLEN
        }
    };

    WGPUShaderModuleDescriptor shader_desc = {
        .nextInChain = (WGPUChainedStruct*)&wgsl_desc
    };

    return wgpuDeviceCreateShaderModule(engine->device, &shader_desc);
}

static WGPURenderPipeline flecsBloomCreatePipeline(
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
        .entryPoint = (WGPUStringView){
            .data = "vs_main",
            .length = WGPU_STRLEN
        }
    };

    WGPUFragmentState fragment_state = {
        .module = shader_module,
        .entryPoint = (WGPUStringView){
            .data = fragment_entry,
            .length = WGPU_STRLEN
        },
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
        .multisample = {
            .count = 1
        }
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

static WGPUBlendState flecsBloomGetBlendState(
    FlecsBloomCompositeMode composite_mode)
{
    WGPUBlendComponent color = {
        .srcFactor = composite_mode == FlecsBloomCompositeMode_Additive
            ? WGPUBlendFactor_Constant
            : WGPUBlendFactor_Constant,
        .dstFactor = composite_mode == FlecsBloomCompositeMode_Additive
            ? WGPUBlendFactor_One
            : WGPUBlendFactor_OneMinusConstant,
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

static bool flecsBloomRunPass(
    const FlecsEngineImpl *engine,
    const FlecsRenderEffectImpl *impl,
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
        { .binding = 1, .sampler = impl->bloom_sampler },
        {
            .binding = 2,
            .buffer = impl->bloom_uniform_buffer,
            .offset = 0,
            .size = sizeof(FlecsBloomUniform)
        }
    };

    WGPUBindGroupDescriptor bind_desc = {
        .layout = impl->bloom_bind_layout,
        .entryCount = 3,
        .entries = bind_entries
    };

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(engine->device, &bind_desc);
    if (!bind_group) {
        return false;
    }

    WGPURenderPassColorAttachment color_attachment = {
        .view = target_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
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

static float flecsBloomComputeBlendFactor(
    const FlecsBloomSettings *bloom,
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
        flecsBloomClampFloat(
            (normalized - bloom->high_pass_frequency) /
                bloom->high_pass_frequency,
            0.0f,
            1.0f);

    if (bloom->composite_mode == FlecsBloomCompositeMode_EnergyConserving) {
        lf_boost *= 1.0f - bloom->intensity;
    }

    return (bloom->intensity + lf_boost) * high_pass_lq;
}

static void flecsBloomFillUniform(
    const FlecsEngineImpl *engine,
    const FlecsBloomSettings *settings,
    FlecsBloomUniform *uniform)
{
    float knee = settings->prefilter.threshold *
        flecsBloomClampFloat(settings->prefilter.threshold_softness, 0.0f, 1.0f);

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
    uniform->aspect = engine->height > 0
        ? (float)engine->width / (float)engine->height
        : 1.0f;
    uniform->_padding = 0.0f;
}

static bool flecsRenderEffect_bloom_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)world;
    (void)layout_entries;

    ecs_assert(effect != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    impl->bloom_settings = flecsBloomSanitizeSettings(
        (const FlecsBloomSettings*)effect->ctx);

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

    impl->bloom_sampler = wgpuDeviceCreateSampler(engine->device, &sampler_desc);
    if (!impl->bloom_sampler) {
        return false;
    }

    WGPUBufferDescriptor uniform_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(FlecsBloomUniform)
    };

    impl->bloom_uniform_buffer = wgpuDeviceCreateBuffer(engine->device, &uniform_desc);
    if (!impl->bloom_uniform_buffer) {
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

    impl->bloom_bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device, &bloom_bind_layout_desc);
    if (!impl->bloom_bind_layout) {
        return false;
    }

    WGPUShaderModule bloom_shader = flecsBloomCreateShaderModule(
        engine, kBloomShaderSource);
    if (!bloom_shader) {
        return false;
    }

    WGPUTextureFormat surface_format = engine->surface_config.format;
    WGPUTextureFormat hdr_format = engine->hdr_color_format;
    if (hdr_format == WGPUTextureFormat_Undefined) {
        hdr_format = surface_format;
    }
    WGPUTextureFormat bloom_format = flecsBloomChooseWorkingFormat(engine);

    WGPUBlendState blend_state = flecsBloomGetBlendState(
        impl->bloom_settings.composite_mode);

    impl->bloom_downsample_first_pipeline = flecsBloomCreatePipeline(
        engine,
        bloom_shader,
        impl->bloom_bind_layout,
        "downsample_first",
        bloom_format,
        NULL);
    impl->bloom_downsample_pipeline = flecsBloomCreatePipeline(
        engine,
        bloom_shader,
        impl->bloom_bind_layout,
        "downsample",
        bloom_format,
        NULL);
    impl->bloom_upsample_pipeline = flecsBloomCreatePipeline(
        engine,
        bloom_shader,
        impl->bloom_bind_layout,
        "upsample",
        bloom_format,
        &blend_state);
    impl->bloom_upsample_final_surface_pipeline = flecsBloomCreatePipeline(
        engine,
        bloom_shader,
        impl->bloom_bind_layout,
        "upsample",
        surface_format,
        &blend_state);
    impl->bloom_upsample_final_hdr_pipeline = flecsBloomCreatePipeline(
        engine,
        bloom_shader,
        impl->bloom_bind_layout,
        "upsample",
        hdr_format,
        &blend_state);

    wgpuShaderModuleRelease(bloom_shader);

    if (!impl->bloom_downsample_first_pipeline ||
        !impl->bloom_downsample_pipeline ||
        !impl->bloom_upsample_pipeline ||
        !impl->bloom_upsample_final_surface_pipeline ||
        !impl->bloom_upsample_final_hdr_pipeline)
    {
        return false;
    }

    return true;
}

static bool flecsRenderEffect_bloom_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)world;
    (void)engine;
    (void)effect;
    (void)impl;
    (void)entries;

    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);
    return true;
}

static bool flecsRenderEffect_bloom_renderPassthrough(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView input_view,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    WGPURenderPassColorAttachment color_attachment = {
        .view = output_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
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

    flecsEngineRenderEffect(
        world,
        engine,
        pass,
        effect,
        impl,
        input_view,
        output_format);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

static bool flecsRenderEffect_bloom_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *impl,
    WGPUTextureView input_view,
    WGPUTextureFormat input_format,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    (void)input_format;

    if (impl->bloom_settings.intensity == 0.0f) {
        return flecsRenderEffect_bloom_renderPassthrough(
            world,
            engine,
            effect,
            impl,
            encoder,
            input_view,
            output_view,
            output_format,
            output_load_op);
    }

    if (!flecsBloomEnsureTexture(engine, impl)) {
        return false;
    }

    FlecsBloomUniform uniform = {0};
    flecsBloomFillUniform(engine, &impl->bloom_settings, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        impl->bloom_uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    if (!flecsBloomRunPass(
        engine,
        impl,
        encoder,
        impl->bloom_downsample_first_pipeline,
        input_view,
        impl->bloom_mip_views[0],
        WGPULoadOp_Clear,
        false,
        0.0f))
    {
        return false;
    }

    for (uint32_t mip = 1; mip < impl->bloom_mip_count; mip ++) {
        if (!flecsBloomRunPass(
            engine,
            impl,
            encoder,
            impl->bloom_downsample_pipeline,
            impl->bloom_mip_views[mip - 1],
            impl->bloom_mip_views[mip],
            WGPULoadOp_Clear,
            false,
            0.0f))
        {
            return false;
        }
    }

    float max_mip = (float)(impl->bloom_mip_count - 1u);
    for (uint32_t mip = impl->bloom_mip_count - 1u; mip > 0u; mip --) {
        float blend = flecsBloomComputeBlendFactor(
            &impl->bloom_settings,
            (float)mip,
            max_mip);
        if (!flecsBloomRunPass(
            engine,
            impl,
            encoder,
            impl->bloom_upsample_pipeline,
            impl->bloom_mip_views[mip],
            impl->bloom_mip_views[mip - 1u],
            WGPULoadOp_Load,
            true,
            blend))
        {
            return false;
        }
    }

    WGPURenderPipeline final_pipeline = output_format == engine->surface_config.format
        ? impl->bloom_upsample_final_surface_pipeline
        : impl->bloom_upsample_final_hdr_pipeline;

    float final_blend = flecsBloomComputeBlendFactor(
        &impl->bloom_settings,
        0.0f,
        max_mip);

    if (!flecsRenderEffect_bloom_renderPassthrough(
        world,
        engine,
        effect,
        impl,
        encoder,
        input_view,
        output_view,
        output_format,
        output_load_op))
    {
        return false;
    }

    return flecsBloomRunPass(
        engine,
        impl,
        encoder,
        final_pipeline,
        impl->bloom_mip_views[0],
        output_view,
        WGPULoadOp_Load,
        true,
        final_blend);
}

static void flecsBloomFreeSettings(
    void *ctx)
{
    ecs_os_free(ctx);
}

ecs_entity_t flecsEngine_createEffect_bloom(
    ecs_world_t *world,
    int32_t input,
    const FlecsBloomSettings *settings)
{
    FlecsBloomSettings resolved = flecsBloomSanitizeSettings(settings);
    FlecsBloomSettings *stored_settings = ecs_os_malloc_t(FlecsBloomSettings);
    if (!stored_settings) {
        ecs_err("failed to allocate bloom settings");
        return 0;
    }

    *stored_settings = resolved;

    ecs_entity_t effect = ecs_new(world);
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsRenderEffect_bloom_shader(world),
        .input = input,
        .setup_callback = flecsRenderEffect_bloom_setup,
        .bind_callback = flecsRenderEffect_bloom_bind,
        .render_callback = flecsRenderEffect_bloom_render,
        .ctx = stored_settings,
        .free_ctx = flecsBloomFreeSettings
    });

    return effect;
}
