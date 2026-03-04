#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../renderer.h"
#include "hdri_loader.h"
#include "ibl_internal.h"
#include "flecs_engine.h"

#define FLECS_ENGINE_IBL_ENV_SIZE (512u)
#define FLECS_ENGINE_IBL_BRDF_LUT_SIZE (256u)
#define FLECS_ENGINE_IBL_FALLBACK_IMAGE_SIZE (1u)

typedef struct FlecsIblFaceUniform {
    float face_index;
    float roughness;
    float face_size;
    float _padding1;
} FlecsIblFaceUniform;

static const char *kPrefilterShaderSource =
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>\n"
    "};\n"
    "struct FaceUniform {\n"
    "  data : vec4<f32>\n"
    "};\n"
    "@group(0) @binding(0) var equirect_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var env_sampler : sampler;\n"
    "@group(0) @binding(2) var<uniform> face_uniform : FaceUniform;\n"
    "const PI : f32 = 3.141592653589793;\n"
    "const SAMPLE_COUNT : u32 = 1024u;\n"
    "@vertex fn vs_main(@builtin(vertex_index) vid : u32) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  var pos = array<vec2<f32>, 3>(\n"
    "    vec2<f32>(-1.0, -1.0),\n"
    "    vec2<f32>(3.0, -1.0),\n"
    "    vec2<f32>(-1.0, 3.0));\n"
    "  let p = pos[vid];\n"
    "  out.pos = vec4<f32>(p, 0.0, 1.0);\n"
    "  out.uv = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);\n"
    "  return out;\n"
    "}\n"
    "fn cubeFaceUvToDir(face : u32, uv : vec2<f32>, face_size : f32) -> vec3<f32> {\n"
    "  let scale = max(1.0 - (1.0 / max(face_size, 1.0)), 0.0);\n"
    "  let st = (uv * 2.0 - vec2<f32>(1.0, 1.0)) * scale;\n"
    "  let x = st.x;\n"
    "  let y = -st.y;\n"
    "  if (face == 0u) {\n"
    "    return normalize(vec3<f32>(1.0, y, -x));\n"
    "  }\n"
    "  if (face == 1u) {\n"
    "    return normalize(vec3<f32>(-1.0, y, x));\n"
    "  }\n"
    "  if (face == 2u) {\n"
    "    return normalize(vec3<f32>(x, 1.0, -y));\n"
    "  }\n"
    "  if (face == 3u) {\n"
    "    return normalize(vec3<f32>(x, -1.0, y));\n"
    "  }\n"
    "  if (face == 4u) {\n"
    "    return normalize(vec3<f32>(x, y, 1.0));\n"
    "  }\n"
    "  return normalize(vec3<f32>(-x, y, -1.0));\n"
    "}\n"
    "fn radicalInverseVdC(bits_in : u32) -> f32 {\n"
    "  var bits = bits_in;\n"
    "  bits = (bits << 16u) | (bits >> 16u);\n"
    "  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
    "  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
    "  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
    "  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
    "  return f32(bits) * 2.3283064365386963e-10;\n"
    "}\n"
    "fn hammersley(i : u32, n : u32) -> vec2<f32> {\n"
    "  return vec2<f32>(f32(i) / f32(n), radicalInverseVdC(i));\n"
    "}\n"
    "fn importanceSampleGGX(xi : vec2<f32>, n : vec3<f32>, roughness : f32) -> vec3<f32> {\n"
    "  let a = roughness * roughness;\n"
    "  let phi = 2.0 * PI * xi.x;\n"
    "  let cos_theta = sqrt((1.0 - xi.y) / (1.0 + ((a * a) - 1.0) * xi.y));\n"
    "  let sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));\n"
    "  let h = vec3<f32>(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);\n"
    "  let up = select(vec3<f32>(0.0, 0.0, 1.0), vec3<f32>(1.0, 0.0, 0.0), abs(n.z) > 0.999);\n"
    "  let tangent = normalize(cross(up, n));\n"
    "  let bitangent = cross(n, tangent);\n"
    "  return normalize(tangent * h.x + bitangent * h.y + n * h.z);\n"
    "}\n"
    "fn directionToEquirectUv(dir : vec3<f32>) -> vec2<f32> {\n"
    "  let d = normalize(dir);\n"
    "  let phi = atan2(d.z, d.x);\n"
    "  let theta = asin(clamp(d.y, -1.0, 1.0));\n"
    "  return vec2<f32>(phi / (2.0 * PI) + 0.5, 0.5 - theta / PI);\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let face = u32(face_uniform.data.x + 0.5);\n"
    "  let roughness = clamp(face_uniform.data.y, 0.0, 1.0);\n"
    "  let n = cubeFaceUvToDir(face, in.uv, face_uniform.data.z);\n"
    "  let v = n;\n"
    "  var prefiltered = vec3<f32>(0.0);\n"
    "  var total_weight = 0.0;\n"
    "  var i = 0u;\n"
    "  loop {\n"
    "    if (i >= SAMPLE_COUNT) {\n"
    "      break;\n"
    "    }\n"
    "    let xi = hammersley(i, SAMPLE_COUNT);\n"
    "    let h = importanceSampleGGX(xi, n, roughness);\n"
    "    let l = normalize(2.0 * dot(v, h) * h - v);\n"
    "    let ndotl = max(dot(n, l), 0.0);\n"
    "    if (ndotl > 0.0) {\n"
    "      let env_uv = directionToEquirectUv(l);\n"
    "      prefiltered += textureSampleLevel(equirect_texture, env_sampler, env_uv, 0.0).rgb * ndotl;\n"
    "      total_weight += ndotl;\n"
    "    }\n"
    "    i = i + 1u;\n"
    "  }\n"
    "  if (total_weight > 0.0) {\n"
    "    return vec4<f32>(prefiltered / total_weight, 1.0);\n"
    "  }\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *kBrdfLutShaderSource =
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>\n"
    "};\n"
    "const PI : f32 = 3.141592653589793;\n"
    "const SAMPLE_COUNT : u32 = 1024u;\n"
    "@vertex fn vs_main(@builtin(vertex_index) vid : u32) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  var pos = array<vec2<f32>, 3>(\n"
    "    vec2<f32>(-1.0, -1.0),\n"
    "    vec2<f32>(3.0, -1.0),\n"
    "    vec2<f32>(-1.0, 3.0));\n"
    "  let p = pos[vid];\n"
    "  out.pos = vec4<f32>(p, 0.0, 1.0);\n"
    "  out.uv = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);\n"
    "  return out;\n"
    "}\n"
    "fn radicalInverseVdC(bits_in : u32) -> f32 {\n"
    "  var bits = bits_in;\n"
    "  bits = (bits << 16u) | (bits >> 16u);\n"
    "  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
    "  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
    "  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
    "  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
    "  return f32(bits) * 2.3283064365386963e-10;\n"
    "}\n"
    "fn hammersley(i : u32, n : u32) -> vec2<f32> {\n"
    "  return vec2<f32>(f32(i) / f32(n), radicalInverseVdC(i));\n"
    "}\n"
    "fn importanceSampleGGX(xi : vec2<f32>, n : vec3<f32>, roughness : f32) -> vec3<f32> {\n"
    "  let a = roughness * roughness;\n"
    "  let phi = 2.0 * PI * xi.x;\n"
    "  let cos_theta = sqrt((1.0 - xi.y) / (1.0 + ((a * a) - 1.0) * xi.y));\n"
    "  let sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));\n"
    "  let h = vec3<f32>(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);\n"
    "  let up = select(vec3<f32>(0.0, 0.0, 1.0), vec3<f32>(1.0, 0.0, 0.0), abs(n.z) > 0.999);\n"
    "  let tangent = normalize(cross(up, n));\n"
    "  let bitangent = cross(n, tangent);\n"
    "  return normalize(tangent * h.x + bitangent * h.y + n * h.z);\n"
    "}\n"
    "fn geometrySchlickGGX(ndotv : f32, roughness : f32) -> f32 {\n"
    "  let r = roughness + 1.0;\n"
    "  let k = (r * r) / 8.0;\n"
    "  return ndotv / max(ndotv * (1.0 - k) + k, 1e-4);\n"
    "}\n"
    "fn geometrySmith(n : vec3<f32>, v : vec3<f32>, l : vec3<f32>, roughness : f32) -> f32 {\n"
    "  let ndotv = max(dot(n, v), 0.0);\n"
    "  let ndotl = max(dot(n, l), 0.0);\n"
    "  let ggx_v = geometrySchlickGGX(ndotv, roughness);\n"
    "  let ggx_l = geometrySchlickGGX(ndotl, roughness);\n"
    "  return ggx_v * ggx_l;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec2<f32> {\n"
    "  let ndotv = clamp(in.uv.x, 0.0, 1.0);\n"
    "  let roughness = clamp(in.uv.y, 0.0, 1.0);\n"
    "  let n = vec3<f32>(0.0, 0.0, 1.0);\n"
    "  let v = vec3<f32>(sqrt(max(1.0 - ndotv * ndotv, 0.0)), 0.0, ndotv);\n"
    "  var a = 0.0;\n"
    "  var b = 0.0;\n"
    "  var i = 0u;\n"
    "  loop {\n"
    "    if (i >= SAMPLE_COUNT) {\n"
    "      break;\n"
    "    }\n"
    "    let xi = hammersley(i, SAMPLE_COUNT);\n"
    "    let h = importanceSampleGGX(xi, n, roughness);\n"
    "    let l = normalize(2.0 * dot(v, h) * h - v);\n"
    "    let ndotl = max(l.z, 0.0);\n"
    "    let ndoth = max(h.z, 0.0);\n"
    "    let vdoth = max(dot(v, h), 0.0);\n"
    "    if (ndotl > 0.0) {\n"
    "      let g = geometrySmith(n, v, l, roughness);\n"
    "      let g_vis = (g * vdoth) / max(ndoth * ndotv, 1e-4);\n"
    "      let fc = pow(1.0 - vdoth, 5.0);\n"
    "      a += (1.0 - fc) * g_vis;\n"
    "      b += fc * g_vis;\n"
    "    }\n"
    "    i = i + 1u;\n"
    "  }\n"
    "  return vec2<f32>(a, b) / f32(SAMPLE_COUNT);\n"
    "}\n";

static void flecsIblLogImageStats(
    const FlecsHdriImage *image,
    const char *path)
{
    if (!image || !image->pixels_rgba32f || image->width <= 0 || image->height <= 0) {
        return;
    }

    size_t pixel_count = (size_t)image->width * (size_t)image->height;
    float min_l = INFINITY;
    float max_l = 0.0f;
    for (size_t i = 0; i < pixel_count; i ++) {
        const float *p = &image->pixels_rgba32f[i * 4u];
        float l = p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f;
        if (l < min_l) {
            min_l = l;
        }
        if (l > max_l) {
            max_l = l;
        }
    }

    ecs_dbg(
        "[ibl] loaded HDRI '%s' (%dx%d), luminance range [%g, %g]",
        path ? path : "<unknown>",
        image->width,
        image->height,
        (double)min_l,
        (double)max_l);

    if (max_l <= 1.05f) {
        ecs_warn(
            "[ibl] HDRI '%s' appears low-range (max luminance=%g); reflections may look flat",
            path ? path : "<unknown>",
            (double)max_l);
    }
}

static uint32_t flecsIblComputeMipCount(
    uint32_t size)
{
    uint32_t mip_count = 1u;
    while (size > 1u) {
        size >>= 1u;
        mip_count ++;
    }
    return mip_count;
}

static uint16_t flecsIblFloatToHalf(
    float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));

    uint32_t sign = (bits >> 16u) & 0x8000u;
    uint32_t mantissa = bits & 0x007FFFFFu;
    int32_t exp = ((int32_t)((bits >> 23u) & 0xFFu)) - 127 + 15;

    if (exp <= 0) {
        if (exp < -10) {
            return (uint16_t)sign;
        }

        mantissa = (mantissa | 0x00800000u) >> (1 - exp);
        if (mantissa & 0x00001000u) {
            mantissa += 0x00002000u;
        }

        return (uint16_t)(sign | (mantissa >> 13u));
    }

    if (exp >= 31) {
        return (uint16_t)(sign | 0x7C00u);
    }

    if (mantissa & 0x00001000u) {
        mantissa += 0x00002000u;
        if (mantissa & 0x00800000u) {
            mantissa = 0;
            exp ++;
            if (exp >= 31) {
                return (uint16_t)(sign | 0x7C00u);
            }
        }
    }

    return (uint16_t)(sign | ((uint32_t)exp << 10u) | (mantissa >> 13u));
}

static WGPUShaderModule flecsIblCreateShaderModule(
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

static WGPURenderPipeline flecsIblCreatePipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule shader_module,
    const WGPUBindGroupLayout *bind_group_layouts,
    uint32_t bind_group_layout_count,
    const char *fragment_entry,
    WGPUTextureFormat format)
{
    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = bind_group_layout_count,
        .bindGroupLayouts = bind_group_layouts
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUColorTargetState color_target = {
        .format = format,
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

static WGPUTextureView flecsIblCreateCubeView(
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mip_count)
{
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_Cube,
        .baseMipLevel = 0,
        .mipLevelCount = mip_count,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &view_desc);
}

static WGPUTextureView flecsIblCreateCubeFaceView(
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mip_level,
    uint32_t array_layer)
{
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = mip_level,
        .mipLevelCount = 1,
        .baseArrayLayer = array_layer,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &view_desc);
}

static WGPUTextureView flecsIblCreateTexture2DView(
    WGPUTexture texture,
    WGPUTextureFormat format)
{
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &view_desc);
}

static bool flecsIblCreateEquirectTexture(
    const FlecsEngineImpl *engine,
    FlecHdriImpl *ibl,
    const FlecsHdriImage *image)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = (uint32_t)image->width,
            .height = (uint32_t)image->height,
            .depthOrArrayLayers = 1
        },
        .format = WGPUTextureFormat_RGBA16Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    ibl->ibl_equirect_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!ibl->ibl_equirect_texture) {
        return false;
    }

    ibl->ibl_equirect_texture_view = flecsIblCreateTexture2DView(
        ibl->ibl_equirect_texture,
        WGPUTextureFormat_RGBA16Float);
    if (!ibl->ibl_equirect_texture_view) {
        return false;
    }

    size_t pixel_count = (size_t)image->width * (size_t)image->height;
    size_t texel_count = pixel_count * 4u;
    uint16_t *pixels_rgba16 = malloc(texel_count * sizeof(uint16_t));
    if (!pixels_rgba16) {
        return false;
    }

    for (size_t i = 0; i < texel_count; i ++) {
        pixels_rgba16[i] = flecsIblFloatToHalf(image->pixels_rgba32f[i]);
    }

    WGPUTexelCopyTextureInfo destination = {
        .texture = ibl->ibl_equirect_texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferLayout layout = {
        .offset = 0,
        .bytesPerRow = (uint32_t)image->width * sizeof(uint16_t) * 4u,
        .rowsPerImage = (uint32_t)image->height
    };

    WGPUExtent3D size = {
        .width = (uint32_t)image->width,
        .height = (uint32_t)image->height,
        .depthOrArrayLayers = 1
    };

    wgpuQueueWriteTexture(
        engine->queue,
        &destination,
        pixels_rgba16,
        texel_count * sizeof(uint16_t),
        &layout,
        &size);

    free(pixels_rgba16);
    return true;
}

static bool flecsIblCreateCubeTexture(
    const FlecsEngineImpl *engine,
    uint32_t size,
    uint32_t mip_count,
    WGPUTextureFormat format,
    WGPUTexture *out_texture,
    WGPUTextureView *out_cube_view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = size,
            .height = size,
            .depthOrArrayLayers = 6
        },
        .format = format,
        .mipLevelCount = mip_count,
        .sampleCount = 1
    };

    *out_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!*out_texture) {
        return false;
    }

    *out_cube_view = flecsIblCreateCubeView(*out_texture, format, mip_count);
    if (!*out_cube_view) {
        return false;
    }

    return true;
}

static bool flecsIblCreateBrdfLutTexture(
    const FlecsEngineImpl *engine,
    FlecHdriImpl *ibl)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = FLECS_ENGINE_IBL_BRDF_LUT_SIZE,
            .height = FLECS_ENGINE_IBL_BRDF_LUT_SIZE,
            .depthOrArrayLayers = 1
        },
        .format = WGPUTextureFormat_RG16Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    ibl->ibl_brdf_lut_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!ibl->ibl_brdf_lut_texture) {
        return false;
    }

    ibl->ibl_brdf_lut_texture_view = flecsIblCreateTexture2DView(
        ibl->ibl_brdf_lut_texture,
        WGPUTextureFormat_RG16Float);
    if (!ibl->ibl_brdf_lut_texture_view) {
        return false;
    }

    return true;
}

static bool flecsIblCreateSampler(
    const FlecsEngineImpl *engine,
    FlecHdriImpl *ibl)
{
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = (float)ibl->ibl_prefilter_mip_count,
        .maxAnisotropy = 1
    };

    ibl->ibl_sampler = wgpuDeviceCreateSampler(engine->device, &sampler_desc);
    return ibl->ibl_sampler != NULL;
}

static bool flecsIblDrawFullscreenPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView target_view,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group)
{
    WGPURenderPassColorAttachment color_attachment = {
        .view = target_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        encoder, &pass_desc);
    if (!pass) {
        return false;
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (bind_group) {
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    }
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

static bool flecsIblSubmitFullscreenPass(
    const FlecsEngineImpl *engine,
    WGPUTextureView target_view,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        engine->device, &(WGPUCommandEncoderDescriptor){0});
    if (!encoder) {
        return false;
    }

    if (!flecsIblDrawFullscreenPass(encoder, target_view, pipeline, bind_group)) {
        wgpuCommandEncoderRelease(encoder);
        return false;
    }

    WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(
        encoder, &(WGPUCommandBufferDescriptor){0});
    wgpuCommandEncoderRelease(encoder);
    if (!command_buffer) {
        return false;
    }

    wgpuQueueSubmit(engine->queue, 1, &command_buffer);
    wgpuCommandBufferRelease(command_buffer);
    return true;
}

static bool flecsIblRunPreprocessPasses(
    const FlecsEngineImpl *engine,
    FlecHdriImpl *ibl)
{
    bool result = false;
    WGPUShaderModule prefilter_shader = NULL;
    WGPUShaderModule brdf_shader = NULL;
    WGPUBindGroupLayout prefilter_bind_layout = NULL;
    WGPUBindGroup prefilter_bind_group = NULL;
    WGPUBuffer uniform_buffer = NULL;
    WGPURenderPipeline prefilter_pipeline = NULL;
    WGPURenderPipeline brdf_pipeline = NULL;

    prefilter_shader = flecsIblCreateShaderModule(
        engine, kPrefilterShaderSource);
    brdf_shader = flecsIblCreateShaderModule(
        engine, kBrdfLutShaderSource);
    if (!prefilter_shader || !brdf_shader) {
        goto cleanup;
    }

    WGPUBindGroupLayoutEntry prefilter_layout_entries[3] = {
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
                .minBindingSize = sizeof(FlecsIblFaceUniform)
            }
        }
    };

    prefilter_bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 3,
            .entries = prefilter_layout_entries
        });
    if (!prefilter_bind_layout) {
        goto cleanup;
    }

    uniform_buffer = wgpuDeviceCreateBuffer(
        engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(FlecsIblFaceUniform)
        });
    if (!uniform_buffer) {
        goto cleanup;
    }

    prefilter_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = prefilter_bind_layout,
            .entryCount = 3,
            .entries = (WGPUBindGroupEntry[3]){
                {
                    .binding = 0,
                    .textureView = ibl->ibl_equirect_texture_view
                },
                {
                    .binding = 1,
                    .sampler = ibl->ibl_sampler
                },
                {
                    .binding = 2,
                    .buffer = uniform_buffer,
                    .size = sizeof(FlecsIblFaceUniform)
                }
            }
        });
    if (!prefilter_bind_group) {
        goto cleanup;
    }

    prefilter_pipeline = flecsIblCreatePipeline(
        engine,
        prefilter_shader,
        &prefilter_bind_layout,
        1,
        "fs_main",
        WGPUTextureFormat_RGBA16Float);
    brdf_pipeline = flecsIblCreatePipeline(
        engine,
        brdf_shader,
        NULL,
        0,
        "fs_main",
        WGPUTextureFormat_RG16Float);
    if (!prefilter_pipeline || !brdf_pipeline) {
        goto cleanup;
    }

    for (uint32_t mip = 0; mip < ibl->ibl_prefilter_mip_count; mip ++) {
        float roughness = ibl->ibl_prefilter_mip_count > 1u
            ? (float)mip / (float)(ibl->ibl_prefilter_mip_count - 1u)
            : 0.0f;

        for (uint32_t face = 0; face < 6u; face ++) {
            uint32_t face_size_u = FLECS_ENGINE_IBL_ENV_SIZE >> mip;
            if (!face_size_u) {
                face_size_u = 1u;
            }
            FlecsIblFaceUniform uniform = {
                .face_index = (float)face,
                .roughness = roughness,
                .face_size = (float)face_size_u,
                ._padding1 = 0.0f
            };
            wgpuQueueWriteBuffer(
                engine->queue,
                uniform_buffer,
                0,
                &uniform,
                sizeof(uniform));

            WGPUTextureView face_view = flecsIblCreateCubeFaceView(
                ibl->ibl_prefiltered_cubemap,
                WGPUTextureFormat_RGBA16Float,
                mip,
                face);
            if (!face_view) {
                goto cleanup;
            }

            bool draw_ok = flecsIblSubmitFullscreenPass(
                engine,
                face_view,
                prefilter_pipeline,
                prefilter_bind_group);
            wgpuTextureViewRelease(face_view);
            if (!draw_ok) {
                goto cleanup;
            }
        }
    }

    if (!flecsIblSubmitFullscreenPass(
        engine,
        ibl->ibl_brdf_lut_texture_view,
        brdf_pipeline,
        NULL))
    {
        goto cleanup;
    }
    result = true;

cleanup:
    if (brdf_pipeline) {
        wgpuRenderPipelineRelease(brdf_pipeline);
    }
    if (prefilter_pipeline) {
        wgpuRenderPipelineRelease(prefilter_pipeline);
    }
    if (prefilter_bind_group) {
        wgpuBindGroupRelease(prefilter_bind_group);
    }
    if (uniform_buffer) {
        wgpuBufferRelease(uniform_buffer);
    }
    if (prefilter_bind_layout) {
        wgpuBindGroupLayoutRelease(prefilter_bind_layout);
    }
    if (brdf_shader) {
        wgpuShaderModuleRelease(brdf_shader);
    }
    if (prefilter_shader) {
        wgpuShaderModuleRelease(prefilter_shader);
    }

    return result;
}

bool flecsEngineInitIblResources(
    FlecsEngineImpl *engine,
    FlecHdriImpl *ibl,
    const char *hdri_path)
{
    flecsIblReleaseRuntimeResources(ibl);

    if (!flecsEngineEnsureIblBindLayout(engine)) {
        return false;
    }

    ibl->ibl_prefilter_mip_count = flecsIblComputeMipCount(
        FLECS_ENGINE_IBL_ENV_SIZE);

    FlecsHdriImage image = {0};
    if (hdri_path && hdri_path[0]) {
        if (!flecsHdriLoad(hdri_path, &image)) {
            ecs_warn(
                "failed to load HDRI '%s', falling back to black IBL",
                hdri_path);
        } else {
            flecsIblLogImageStats(&image, hdri_path);
        }
    }

    if (!image.pixels_rgba32f) {
        image.width = (int32_t)FLECS_ENGINE_IBL_FALLBACK_IMAGE_SIZE;
        image.height = (int32_t)FLECS_ENGINE_IBL_FALLBACK_IMAGE_SIZE;
        image.pixels_rgba32f = malloc(sizeof(float) * 4u);
        if (!image.pixels_rgba32f) {
            return false;
        }

        image.pixels_rgba32f[0] = 0.0f;
        image.pixels_rgba32f[1] = 0.0f;
        image.pixels_rgba32f[2] = 0.0f;
        image.pixels_rgba32f[3] = 1.0f;
    }

    bool ok = false;
    if (!flecsIblCreateEquirectTexture(engine, ibl, &image)) {
        goto done;
    }

    if (!flecsIblCreateCubeTexture(
        engine,
        FLECS_ENGINE_IBL_ENV_SIZE,
        ibl->ibl_prefilter_mip_count,
        WGPUTextureFormat_RGBA16Float,
        &ibl->ibl_prefiltered_cubemap,
        &ibl->ibl_prefiltered_cubemap_view))
    {
        goto done;
    }

    if (!flecsIblCreateBrdfLutTexture(engine, ibl)) {
        goto done;
    }

    if (!flecsIblCreateSampler(engine, ibl)) {
        goto done;
    }

    if (!flecsIblRunPreprocessPasses(engine, ibl)) {
        goto done;
    }

    if (!flecsIblCreateRuntimeBindGroup(engine, ibl)) {
        goto done;
    }

    ok = true;

done:
    flecsHdriImageFree(&image);
    if (!ok) {
        flecsIblReleaseRuntimeResources(ibl);
    }
    return ok;
}
