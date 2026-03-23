/*
 * Gamma correction pass for emscripten.
 *
 * Browser WebGPU canvas only supports bgra8unorm (not sRGB). The canvas
 * compositor expects sRGB-encoded values, but the render pipeline outputs
 * linear values. This module provides an intermediate render target and a
 * blit pass that applies linear→sRGB conversion (pow 1/2.2).
 */

#ifdef __EMSCRIPTEN__

#include "renderer.h"

static WGPUTexture       gamma_texture;
static WGPUTextureView   gamma_texture_view;
static uint32_t           gamma_tex_w, gamma_tex_h;
static WGPURenderPipeline gamma_pipeline;
static WGPUBindGroupLayout gamma_bind_layout;
static WGPUSampler        gamma_sampler;

static const char *gamma_wgsl =
    "struct VSOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f }"
    "\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> VSOut {"
    "  var pos = array<vec2f,3>(vec2f(-1,-1), vec2f(3,-1), vec2f(-1,3));"
    "  var uv  = array<vec2f,3>(vec2f(0,1),   vec2f(2,1),  vec2f(0,-1));"
    "  var out: VSOut;"
    "  out.pos = vec4f(pos[i], 0, 1);"
    "  out.uv  = uv[i];"
    "  return out;"
    "}"
    "\n"
    "@group(0) @binding(0) var t: texture_2d<f32>;"
    "@group(0) @binding(1) var s: sampler;"
    "\n"
    "@fragment fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {"
    "  let c = textureSample(t, s, uv);"
    "  let gamma = vec3f(1.0 / 2.2);"
    "  return vec4f(pow(c.rgb, gamma), c.a);"
    "}";

static bool flecsEngine_gamma_ensurePipeline(FlecsEngineImpl *impl)
{
    if (gamma_pipeline) {
        return true;
    }

    WGPUShaderModule mod = flecsEngine_createShaderModule(
        impl->device, gamma_wgsl);
    if (!mod) {
        return false;
    }

    WGPUBindGroupLayoutEntry entries[2] = {
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
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor bgl_desc = {
        .entryCount = 2,
        .entries = entries
    };
    gamma_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &bgl_desc);

    WGPUPipelineLayoutDescriptor pl_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &gamma_bind_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(
        impl->device, &pl_desc);

    WGPUColorTargetState ct = {
        .format = impl->surface_config.format,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fs = {
        .module = mod,
        .entryPoint = WGPU_STR("fs"),
        .targetCount = 1,
        .targets = &ct
    };
    WGPURenderPipelineDescriptor pd = {
        .layout = layout,
        .vertex = {
            .module = mod,
            .entryPoint = WGPU_STR("vs")
        },
        .fragment = &fs,
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = WGPU_MULTISAMPLE_DEFAULT
    };
    gamma_pipeline = wgpuDeviceCreateRenderPipeline(impl->device, &pd);

    WGPUSamplerDescriptor sampler_desc = {
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear
    };
    gamma_sampler = wgpuDeviceCreateSampler(impl->device, &sampler_desc);

    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(mod);

    return gamma_pipeline != NULL;
}

static bool flecsEngine_gamma_ensureTexture(FlecsEngineImpl *impl)
{
    uint32_t w = (uint32_t)impl->width;
    uint32_t h = (uint32_t)impl->height;

    if (gamma_texture_view && gamma_tex_w == w && gamma_tex_h == h) {
        return true;
    }

    if (gamma_texture_view) {
        wgpuTextureViewRelease(gamma_texture_view);
        gamma_texture_view = NULL;
    }
    if (gamma_texture) {
        wgpuTextureRelease(gamma_texture);
        gamma_texture = NULL;
    }

    WGPUTextureDescriptor td = {
        .usage = WGPUTextureUsage_RenderAttachment |
                 WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { .width = w, .height = h, .depthOrArrayLayers = 1 },
        .format = flecsEngine_getHdrFormat(impl),
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    gamma_texture = wgpuDeviceCreateTexture(impl->device, &td);
    if (!gamma_texture) {
        return false;
    }

    gamma_texture_view = wgpuTextureCreateView(gamma_texture, NULL);
    gamma_tex_w = w;
    gamma_tex_h = h;
    return gamma_texture_view != NULL;
}

WGPUTextureView flecsEngine_gamma_getRenderTarget(FlecsEngineImpl *impl)
{
    if (!flecsEngine_gamma_ensurePipeline(impl)) {
        return NULL;
    }
    if (!flecsEngine_gamma_ensureTexture(impl)) {
        return NULL;
    }
    return gamma_texture_view;
}

void flecsEngine_gamma_blit(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView src_view,
    WGPUTextureView dst_view)
{
    (void)src_view; /* we use gamma_texture_view which is the same */

    WGPUBindGroupEntry bg_entries[2] = {
        { .binding = 0, .textureView = gamma_texture_view },
        { .binding = 1, .sampler = gamma_sampler }
    };
    WGPUBindGroupDescriptor bg_desc = {
        .layout = gamma_bind_layout,
        .entryCount = 2,
        .entries = bg_entries
    };
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(impl->device, &bg_desc);

    WGPURenderPassColorAttachment ca = {
        .view = dst_view,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0, 0, 0, 1}
    };
    WGPURenderPassDescriptor rpd = {
        .colorAttachmentCount = 1,
        .colorAttachments = &ca
    };

    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &rpd);
    wgpuRenderPassEncoderSetPipeline(pass, gamma_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bg);
}

#endif /* __EMSCRIPTEN__ */
