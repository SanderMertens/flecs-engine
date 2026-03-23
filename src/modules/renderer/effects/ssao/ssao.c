#include "../../renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsSSAO);
ECS_COMPONENT_DECLARE(FlecsSSAOImpl);

typedef struct FlecsSSAOUniform {
    mat4 proj;
    mat4 inv_proj;
    float params[4];    /* radius, bias, intensity, blur */
    float viewport[4];  /* width, height, 1/width, 1/height */
} FlecsSSAOUniform;

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct SsaoUniforms {\n"
    "  proj : mat4x4<f32>,\n"
    "  inv_proj : mat4x4<f32>,\n"
    "  params : vec4<f32>,\n"
    "  viewport : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> uniforms : SsaoUniforms;\n"

    "fn reconstruct_view_pos(uv : vec2<f32>, depth : f32) -> vec3<f32> {\n"
    "  let ndc = vec4<f32>(\n"
    "    uv.x * 2.0 - 1.0,\n"
    "    (1.0 - uv.y) * 2.0 - 1.0,\n"
    "    depth,\n"
    "    1.0);\n"
    "  let view_h = uniforms.inv_proj * ndc;\n"
    "  return view_h.xyz / view_h.w;\n"
    "}\n"

    "fn hash22(p : vec2<f32>) -> vec2<f32> {\n"
    "  var p3 = fract(vec3<f32>(p.xyx) * vec3<f32>(0.1031, 0.1030, 0.0973));\n"
    "  p3 += dot(p3, p3.yzx + 33.33);\n"
    "  return fract((p3.xx + p3.yz) * p3.zy);\n"
    "}\n"

    "const SAMPLE_COUNT = 16u;\n"
    "const kernel = array<vec3<f32>, 16>(\n"
    "  vec3<f32>( 0.0380,  0.0510,  0.0146),\n"
    "  vec3<f32>(-0.0447,  0.0118,  0.0327),\n"
    "  vec3<f32>( 0.0264, -0.0423,  0.0581),\n"
    "  vec3<f32>(-0.0307,  0.0576,  0.0421),\n"
    "  vec3<f32>( 0.0765,  0.0167,  0.0604),\n"
    "  vec3<f32>(-0.0579,  0.0611,  0.0527),\n"
    "  vec3<f32>( 0.0948, -0.0388,  0.0688),\n"
    "  vec3<f32>(-0.0635,  0.0906,  0.0784),\n"
    "  vec3<f32>( 0.1291, -0.0206,  0.1064),\n"
    "  vec3<f32>(-0.0598,  0.1399,  0.0862),\n"
    "  vec3<f32>( 0.1106,  0.1161,  0.1203),\n"
    "  vec3<f32>(-0.1588, -0.0539,  0.1040),\n"
    "  vec3<f32>( 0.1728,  0.1124,  0.1365),\n"
    "  vec3<f32>(-0.0756, -0.1867,  0.1226),\n"
    "  vec3<f32>( 0.2300, -0.0827,  0.1510),\n"
    "  vec3<f32>(-0.1842,  0.1689,  0.2032)\n"
    ");\n"

    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.999999));\n"
    "  let texel = vec2<i32>(clamped_uv * dims_f);\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  if (depth >= 0.999999) {\n"
    "    if (uniforms.params.w > 0.0) {\n"
    "      return vec4<f32>(1.0, 1.0, 1.0, 1.0);\n"
    "    }\n"
    "    return src;\n"
    "  }\n"

    "  let view_pos = reconstruct_view_pos(in.uv, depth);\n"

    "  let px = uniforms.viewport.z;\n"
    "  let py = uniforms.viewport.w;\n"
    "  let vp_r = reconstruct_view_pos(\n"
    "    in.uv + vec2<f32>(px, 0.0),\n"
    "    textureLoad(depth_texture, texel + vec2<i32>(1, 0), 0));\n"
    "  let vp_u = reconstruct_view_pos(\n"
    "    in.uv + vec2<f32>(0.0, -py),\n"
    "    textureLoad(depth_texture, texel + vec2<i32>(0, -1), 0));\n"
    "  let normal = normalize(cross(vp_r - view_pos, vp_u - view_pos));\n"

    "  let noise = hash22(vec2<f32>(f32(texel.x), f32(texel.y)));\n"
    "  var random_vec = vec3<f32>(noise.x * 2.0 - 1.0, noise.y * 2.0 - 1.0, 0.0);\n"
    "  random_vec = normalize(random_vec - normal * dot(random_vec, normal));\n"
    "  let tangent = random_vec;\n"
    "  let bitangent = cross(normal, tangent);\n"

    "  let radius = uniforms.params.x;\n"
    "  let bias = uniforms.params.y;\n"
    "  let intensity = uniforms.params.z;\n"

    "  var occlusion = 0.0;\n"
    "  for (var i = 0u; i < SAMPLE_COUNT; i++) {\n"
    "    let k = kernel[i];\n"
    "    let sample_dir = tangent * k.x + bitangent * k.y + normal * k.z;\n"
    "    let sample_pos = view_pos + sample_dir * radius;\n"

    "    let sample_clip = uniforms.proj * vec4<f32>(sample_pos, 1.0);\n"
    "    var sample_uv = sample_clip.xy / sample_clip.w;\n"
    "    sample_uv = sample_uv * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5);\n"

    "    if (sample_uv.x < 0.0 || sample_uv.x > 1.0 ||\n"
    "        sample_uv.y < 0.0 || sample_uv.y > 1.0) {\n"
    "      continue;\n"
    "    }\n"

    "    let s_texel = vec2<i32>(sample_uv * dims_f);\n"
    "    let s_depth = textureLoad(depth_texture, s_texel, 0);\n"
    "    let sample_view = reconstruct_view_pos(sample_uv, s_depth);\n"

    "    let range_check = smoothstep(\n"
    "      0.0, 1.0, radius / max(abs(view_pos.z - sample_view.z), 1e-6));\n"
    "    occlusion += select(0.0, 1.0,\n"
    "      sample_view.z >= sample_pos.z + bias) * range_check;\n"
    "  }\n"

    "  let fade = 1.0 - smoothstep(radius * 40.0, radius * 100.0, -view_pos.z);\n"
    "  let ao = clamp(1.0 - (occlusion / f32(SAMPLE_COUNT)) * intensity * fade, 0.0, 1.0);\n"

    /* When blur is enabled, output AO factor only so the blur pass
     * operates on the occlusion term without smearing scene colors.
     * Otherwise composite directly. */
    "  if (uniforms.params.w > 0.0) {\n"
    "    return vec4<f32>(ao, ao, ao, 1.0);\n"
    "  }\n"
    "  return vec4<f32>(src.rgb * ao, src.a);\n"
    "}\n";

/* Cross-bilateral blur + composite shader.
 * Blurs only the AO term using a 1-D cross pattern (horizontal + vertical),
 * giving O(n) cost instead of O(n^2), then composites with the scene. */
static const char *kBlurShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct SsaoUniforms {\n"
    "  proj : mat4x4<f32>,\n"
    "  inv_proj : mat4x4<f32>,\n"
    "  params : vec4<f32>,\n"
    "  viewport : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var ao_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> uniforms : SsaoUniforms;\n"
    "@group(0) @binding(4) var scene_texture : texture_2d<f32>;\n"

    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let ao_dims = vec2<i32>(textureDimensions(ao_texture));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.999999));\n"
    "  let texel = vec2<i32>(clamped_uv * vec2<f32>(ao_dims));\n"
    "  let center_ao = textureLoad(ao_texture, texel, 0).r;\n"
    "  let scene_dims = vec2<i32>(textureDimensions(scene_texture));\n"
    "  let scene_texel = vec2<i32>(clamped_uv * vec2<f32>(scene_dims));\n"
    "  let scene = textureLoad(scene_texture, scene_texel, 0);\n"

    "  let blur_radius = i32(uniforms.params.w);\n"
    "  if (blur_radius <= 0) {\n"
    "    return vec4<f32>(scene.rgb * center_ao, scene.a);\n"
    "  }\n"

    "  let depth_dims = vec2<i32>(textureDimensions(depth_texture));\n"
    "  let depth_texel = vec2<i32>(in.uv * vec2<f32>(depth_dims));\n"
    "  let center_depth = textureLoad(depth_texture,\n"
    "    clamp(depth_texel, vec2<i32>(0), depth_dims - vec2<i32>(1)), 0);\n"

    "  if (center_depth >= 0.999999) {\n"
    "    return scene;\n"
    "  }\n"

    "  let sigma = max(f32(blur_radius) * 0.5, 1.0);\n"
    "  let sigma2 = 2.0 * sigma * sigma;\n"
    "  let depth_sigma = 0.001;\n"

    "  var total_ao = 0.0;\n"
    "  var total_weight = 0.0;\n"

    /* Horizontal axis */
    "  for (var x = -blur_radius; x <= blur_radius; x++) {\n"
    "    let st = texel + vec2<i32>(x, 0);\n"
    "    if (st.x < 0 || st.x >= ao_dims.x) { continue; }\n"
    "    let s_ao = textureLoad(ao_texture, st, 0).r;\n"
    "    let sd_texel = clamp(vec2<i32>(\n"
    "      (vec2<f32>(st) + 0.5) / vec2<f32>(ao_dims) * vec2<f32>(depth_dims)),\n"
    "      vec2<i32>(0), depth_dims - vec2<i32>(1));\n"
    "    let sd = textureLoad(depth_texture, sd_texel, 0);\n"
    "    let d2 = f32(x * x);\n"
    "    let sw = exp(-d2 / sigma2);\n"
    "    let dw = exp(-abs(center_depth - sd) / depth_sigma);\n"
    "    let w = sw * dw;\n"
    "    total_ao += s_ao * w;\n"
    "    total_weight += w;\n"
    "  }\n"

    /* Vertical axis (skip center, already counted) */
    "  for (var y = -blur_radius; y <= blur_radius; y++) {\n"
    "    if (y == 0) { continue; }\n"
    "    let st = texel + vec2<i32>(0, y);\n"
    "    if (st.y < 0 || st.y >= ao_dims.y) { continue; }\n"
    "    let s_ao = textureLoad(ao_texture, st, 0).r;\n"
    "    let sd_texel = clamp(vec2<i32>(\n"
    "      (vec2<f32>(st) + 0.5) / vec2<f32>(ao_dims) * vec2<f32>(depth_dims)),\n"
    "      vec2<i32>(0), depth_dims - vec2<i32>(1));\n"
    "    let sd = textureLoad(depth_texture, sd_texel, 0);\n"
    "    let d2 = f32(y * y);\n"
    "    let sw = exp(-d2 / sigma2);\n"
    "    let dw = exp(-abs(center_depth - sd) / depth_sigma);\n"
    "    let w = sw * dw;\n"
    "    total_ao += s_ao * w;\n"
    "    total_weight += w;\n"
    "  }\n"

    "  var blurred_ao = center_ao;\n"
    "  if (total_weight > 0.0) {\n"
    "    blurred_ao = total_ao / total_weight;\n"
    "  }\n"
    "  return vec4<f32>(scene.rgb * blurred_ao, scene.a);\n"
    "}\n";

static ecs_entity_t flecsEngine_ssao_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "SSAOPostShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsEngine_ssao_releaseBlurTexture(
    FlecsSSAOImpl *impl)
{
    if (impl->blur_intermediate_view) {
        wgpuTextureViewRelease(impl->blur_intermediate_view);
        impl->blur_intermediate_view = NULL;
    }

    if (impl->blur_intermediate_texture) {
        wgpuTextureRelease(impl->blur_intermediate_texture);
        impl->blur_intermediate_texture = NULL;
    }

    impl->blur_texture_width = 0;
    impl->blur_texture_height = 0;
}

static void flecsEngine_ssao_releaseResources(
    FlecsSSAOImpl *impl)
{
    if (impl->uniform_buffer) {
        wgpuBufferRelease(impl->uniform_buffer);
        impl->uniform_buffer = NULL;
    }

    flecsEngine_ssao_releaseBlurTexture(impl);

    if (impl->blur_pipeline_surface) {
        wgpuRenderPipelineRelease(impl->blur_pipeline_surface);
        impl->blur_pipeline_surface = NULL;
    }

    if (impl->blur_pipeline_hdr) {
        wgpuRenderPipelineRelease(impl->blur_pipeline_hdr);
        impl->blur_pipeline_hdr = NULL;
    }

    if (impl->blur_bind_layout) {
        wgpuBindGroupLayoutRelease(impl->blur_bind_layout);
        impl->blur_bind_layout = NULL;
    }
}

ECS_DTOR(FlecsSSAOImpl, ptr, {
    flecsEngine_ssao_releaseResources(ptr);
})

ECS_MOVE(FlecsSSAOImpl, dst, src, {
    flecsEngine_ssao_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_ssao_fillUniform(
    const ecs_world_t *world,
    ecs_entity_t effect_entity,
    const FlecsSSAO *ssao,
    FlecsSSAOUniform *uniform)
{
    glm_mat4_identity(uniform->proj);
    glm_mat4_identity(uniform->inv_proj);

    uniform->params[0] = ssao->radius;
    uniform->params[1] = ssao->bias;
    uniform->params[2] = ssao->intensity;
    uniform->params[3] = (float)ssao->blur;

    uniform->viewport[0] = 0.0f;
    uniform->viewport[1] = 0.0f;
    uniform->viewport[2] = 0.0f;
    uniform->viewport[3] = 0.0f;

    ecs_entity_t view_entity = ecs_get_target(world, effect_entity, EcsChildOf, 0);
    if (!view_entity) {
        return;
    }

    const FlecsRenderView *view = ecs_get(world, view_entity, FlecsRenderView);
    if (!view || !view->camera) {
        return;
    }

    const FlecsCameraImpl *camera = ecs_get(world, view->camera, FlecsCameraImpl);
    if (!camera) {
        return;
    }

    glm_mat4_copy((vec4*)camera->proj, uniform->proj);

    mat4 proj_copy;
    glm_mat4_copy((vec4*)camera->proj, proj_copy);
    glm_mat4_inv(proj_copy, uniform->inv_proj);

    const FlecsRenderViewImpl *view_impl = ecs_get(
        world, view_entity, FlecsRenderViewImpl);
    if (view_impl && view_impl->effect_target_width > 0) {
        float w = (float)view_impl->effect_target_width;
        float h = (float)view_impl->effect_target_height;
        uniform->viewport[0] = w;
        uniform->viewport[1] = h;
        uniform->viewport[2] = 1.0f / w;
        uniform->viewport[3] = 1.0f / h;
    }
}

static WGPURenderPipeline flecsEngine_ssao_createBlurPipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule shader_module,
    WGPUBindGroupLayout bind_layout,
    WGPUTextureFormat color_format)
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
        .writeMask = WGPUColorWriteMask_All
    };

    WGPUVertexState vertex_state = {
        .module = shader_module,
        .entryPoint = WGPU_STR("vs_main")
    };

    WGPUFragmentState fragment_state = {
        .module = shader_module,
        .entryPoint = WGPU_STR("fs_main"),
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

static bool flecsEngine_ssao_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)effect;
    (void)effect_impl;

    ecs_assert(layout_entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    FlecsSSAOImpl ssao_impl = {0};

    WGPUBufferDescriptor uniform_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(FlecsSSAOUniform)
    };

    ssao_impl.uniform_buffer = wgpuDeviceCreateBuffer(engine->device, &uniform_desc);
    if (!ssao_impl.uniform_buffer) {
        return false;
    }

    /* SSAO pass layout entries */
    layout_entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = false
        }
    };

    layout_entries[3] = (WGPUBindGroupLayoutEntry){
        .binding = 3,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(FlecsSSAOUniform)
        }
    };

    *entry_count = 4;

    /* Blur+composite pass: ao_texture, sampler, depth, uniforms, scene_texture */
    WGPUBindGroupLayoutEntry blur_layout_entries[5] = {
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
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsSSAOUniform)
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        }
    };

    WGPUBindGroupLayoutDescriptor blur_layout_desc = {
        .entryCount = 5,
        .entries = blur_layout_entries
    };

    ssao_impl.blur_bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device, &blur_layout_desc);
    if (!ssao_impl.blur_bind_layout) {
        flecsEngine_ssao_releaseResources(&ssao_impl);
        return false;
    }

    WGPUShaderModule blur_shader = flecsEngine_createShaderModule(
        engine->device, kBlurShaderSource);
    if (!blur_shader) {
        flecsEngine_ssao_releaseResources(&ssao_impl);
        return false;
    }

    WGPUTextureFormat surface_format = engine->surface_config.format;
    WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);

    ssao_impl.blur_pipeline_surface = flecsEngine_ssao_createBlurPipeline(
        engine, blur_shader, ssao_impl.blur_bind_layout, surface_format);
    ssao_impl.blur_pipeline_hdr = flecsEngine_ssao_createBlurPipeline(
        engine, blur_shader, ssao_impl.blur_bind_layout, hdr_format);

    wgpuShaderModuleRelease(blur_shader);

    if (!ssao_impl.blur_pipeline_surface || !ssao_impl.blur_pipeline_hdr) {
        flecsEngine_ssao_releaseResources(&ssao_impl);
        return false;
    }

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsSSAOImpl, &ssao_impl);
    return true;
}

static bool flecsEngine_ssao_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffect *effect,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)effect;
    (void)impl;

    ecs_assert(entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    if (!engine->depth_texture_view) {
        return false;
    }

    const FlecsSSAO *ssao = ecs_get(world, effect_entity, FlecsSSAO);
    const FlecsSSAOImpl *ssao_impl = ecs_get(
        world, effect_entity, FlecsSSAOImpl);
    if (!ssao || !ssao_impl || !ssao_impl->uniform_buffer) {
        return false;
    }

    FlecsSSAOUniform uniform = {0};
    flecsEngine_ssao_fillUniform(world, effect_entity, ssao, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        ssao_impl->uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2,
        .textureView = engine->depth_texture_view
    };

    entries[3] = (WGPUBindGroupEntry){
        .binding = 3,
        .buffer = ssao_impl->uniform_buffer,
        .offset = 0,
        .size = sizeof(FlecsSSAOUniform)
    };

    *entry_count = 4;
    return true;
}

static bool flecsEngine_ssao_ensureBlurTexture(
    const FlecsEngineImpl *engine,
    FlecsSSAOImpl *impl,
    uint32_t width,
    uint32_t height)
{
    WGPUTextureFormat format = flecsEngine_getHdrFormat(engine);

    if (impl->blur_intermediate_texture &&
        impl->blur_texture_width == width &&
        impl->blur_texture_height == height)
    {
        return true;
    }

    flecsEngine_ssao_releaseBlurTexture(impl);

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){ .width = width, .height = height, .depthOrArrayLayers = 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    impl->blur_intermediate_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!impl->blur_intermediate_texture) {
        return false;
    }

    impl->blur_intermediate_view = wgpuTextureCreateView(
        impl->blur_intermediate_texture, NULL);
    if (!impl->blur_intermediate_view) {
        flecsEngine_ssao_releaseBlurTexture(impl);
        return false;
    }

    impl->blur_texture_width = width;
    impl->blur_texture_height = height;
    return true;
}

static bool flecsEngine_ssao_render(
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

    const FlecsSSAO *ssao = ecs_get(world, effect_entity, FlecsSSAO);
    FlecsSSAOImpl *impl = ecs_get_mut(world, effect_entity, FlecsSSAOImpl);
    ecs_assert(ssao != NULL, ECS_INVALID_OPERATION, NULL);
    ecs_assert(impl != NULL, ECS_INVALID_OPERATION, NULL);

    if (ssao->blur <= 0) {
        /* No blur: render SSAO composited directly to output */
        WGPURenderPassColorAttachment color_att = {
            .view = output_view,
            WGPU_DEPTH_SLICE
            .loadOp = output_load_op,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = (WGPUColor){0}
        };

        WGPURenderPassDescriptor pass_desc = {
            .colorAttachmentCount = 1,
            .colorAttachments = &color_att
        };

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            encoder, &pass_desc);
        if (!pass) {
            return false;
        }

        flecsEngine_renderEffect_render(
            world, engine, pass, effect_entity,
            effect, effect_impl, input_view, output_format);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        return true;
    }

    /* Determine intermediate texture size */
    uint32_t width = engine->actual_width > 0 ? (uint32_t)engine->actual_width : 1;
    uint32_t height = engine->actual_height > 0 ? (uint32_t)engine->actual_height : 1;

    ecs_entity_t view_entity = ecs_get_target(
        world, effect_entity, EcsChildOf, 0);
    if (view_entity) {
        const FlecsRenderViewImpl *view_impl = ecs_get(
            world, view_entity, FlecsRenderViewImpl);
        if (view_impl && view_impl->effect_target_width > 0) {
            width = view_impl->effect_target_width;
            height = view_impl->effect_target_height;
        }
    }

    uint32_t half_width = width > 1 ? width / 2 : 1;
    uint32_t half_height = height > 1 ? height / 2 : 1;
    if (!flecsEngine_ssao_ensureBlurTexture(engine, impl, half_width, half_height)) {
        return false;
    }

    /* Pass 1: SSAO → intermediate (AO factor only) */
    {
        WGPURenderPassColorAttachment color_att = {
            .view = impl->blur_intermediate_view,
            WGPU_DEPTH_SLICE
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = (WGPUColor){ .r = 1, .g = 1, .b = 1, .a = 1 }
        };

        WGPURenderPassDescriptor pass_desc = {
            .colorAttachmentCount = 1,
            .colorAttachments = &color_att
        };

        WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            encoder, &pass_desc);
        if (!pass) {
            return false;
        }

        flecsEngine_renderEffect_render(
            world, engine, pass, effect_entity,
            effect, effect_impl, input_view, hdr_format);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    /* Pass 2: Cross-bilateral blur of AO + composite with scene */
    {
        WGPUBindGroupEntry blur_entries[5] = {
            { .binding = 0, .textureView = impl->blur_intermediate_view },
            { .binding = 1, .sampler = effect_impl->input_sampler },
            { .binding = 2, .textureView = engine->depth_texture_view },
            {
                .binding = 3,
                .buffer = impl->uniform_buffer,
                .offset = 0,
                .size = sizeof(FlecsSSAOUniform)
            },
            { .binding = 4, .textureView = input_view }
        };

        WGPUBindGroupDescriptor bind_desc = {
            .layout = impl->blur_bind_layout,
            .entryCount = 5,
            .entries = blur_entries
        };

        WGPUBindGroup blur_bind_group = wgpuDeviceCreateBindGroup(
            engine->device, &bind_desc);
        if (!blur_bind_group) {
            return false;
        }

        WGPURenderPassColorAttachment color_att = {
            .view = output_view,
            WGPU_DEPTH_SLICE
            .loadOp = output_load_op,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = (WGPUColor){0}
        };

        WGPURenderPassDescriptor pass_desc = {
            .colorAttachmentCount = 1,
            .colorAttachments = &color_att
        };

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            encoder, &pass_desc);
        if (!pass) {
            wgpuBindGroupRelease(blur_bind_group);
            return false;
        }

        WGPURenderPipeline blur_pipeline =
            output_format == engine->surface_config.format
                ? impl->blur_pipeline_surface
                : impl->blur_pipeline_hdr;

        wgpuRenderPassEncoderSetPipeline(pass, blur_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, blur_bind_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(blur_bind_group);
    }

    return true;
}

FlecsSSAO flecsEngine_ssaoSettingsDefault(void)
{
    return (FlecsSSAO){
        .radius = 0.5f,
        .bias = 0.025f,
        .intensity = 1.0f,
        .blur = 4
    };
}

ecs_entity_t flecsEngine_createEffect_ssao(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    int32_t input,
    const FlecsSSAO *settings)
{
    ecs_entity_t effect = ecs_entity(world, { .parent = parent, .name = name });

    FlecsSSAO ssao = settings
        ? *settings
        : flecsEngine_ssaoSettingsDefault();

    ecs_set_ptr(world, effect, FlecsSSAO, &ssao);
    ecs_set(world, effect, FlecsRenderEffect, {
        .shader = flecsEngine_ssao_shader(world),
        .input = input,
        .setup_callback = flecsEngine_ssao_setup,
        .bind_callback = flecsEngine_ssao_bind,
        .render_callback = flecsEngine_ssao_render
    });

    return effect;
}

void flecsEngine_ssao_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsSSAO);
    ECS_COMPONENT_DEFINE(world, FlecsSSAOImpl);

    ecs_set_hooks(world, FlecsSSAOImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsSSAOImpl),
        .dtor = ecs_dtor(FlecsSSAOImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsSSAO),
        .members = {
            { .name = "radius", .type = ecs_id(ecs_f32_t) },
            { .name = "bias", .type = ecs_id(ecs_f32_t) },
            { .name = "intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "blur", .type = ecs_id(ecs_i32_t) }
        }
    });
}
