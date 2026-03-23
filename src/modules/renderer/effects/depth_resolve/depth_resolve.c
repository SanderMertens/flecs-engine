#include "../../renderer.h"
#include "flecs_engine.h"

/* Fullscreen-triangle shader that reads sample 0 from the multisampled depth
 * texture and outputs it via @builtin(frag_depth) into the 1-sample depth
 * attachment. */
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
    "@group(0) @binding(0) var ms_depth : texture_depth_multisampled_2d;\n"
    "struct FragOut {\n"
    "  @builtin(frag_depth) depth : f32\n"
    "};\n"
    "@fragment fn fs_main(in : VertexOutput) -> FragOut {\n"
    "  let dims = textureDimensions(ms_depth);\n"
    "  let texel = vec2<i32>(in.uv * vec2<f32>(f32(dims.x), f32(dims.y)));\n"
    "  var out : FragOut;\n"
    "  out.depth = textureLoad(ms_depth, texel, 0);\n"
    "  return out;\n"
    "}\n";

int flecsEngine_initDepthResolve(
    FlecsEngineImpl *impl)
{
    WGPUShaderModule module = flecsEngine_createShaderModule(
        impl->device, kShaderSource);
    if (!module) {
        return -1;
    }

    WGPUBindGroupLayoutEntry layout_entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = true
        }
    };

    impl->depth_resolve_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &(WGPUBindGroupLayoutDescriptor){
            .entries = &layout_entry,
            .entryCount = 1
        });
    if (!impl->depth_resolve_bind_layout) {
        wgpuShaderModuleRelease(module);
        return -1;
    }

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        impl->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &impl->depth_resolve_bind_layout
        });
    if (!pipeline_layout) {
        wgpuShaderModuleRelease(module);
        return -1;
    }

    WGPUDepthStencilState depth_stencil = {
        .format = WGPUTextureFormat_Depth24Plus,
        .depthWriteEnabled = true,
        .depthCompare = WGPUCompareFunction_Always
    };

    impl->depth_resolve_pipeline = wgpuDeviceCreateRenderPipeline(
        impl->device, &(WGPURenderPipelineDescriptor){
            .layout = pipeline_layout,
            .vertex = {
                .module = module,
                .entryPoint = WGPU_STR("vs_main")
            },
            .fragment = &(WGPUFragmentState){
                .module = module,
                .entryPoint = WGPU_STR("fs_main"),
                .targetCount = 0,
                .targets = NULL
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .cullMode = WGPUCullMode_None,
                .frontFace = WGPUFrontFace_CCW
            },
            .depthStencil = &depth_stencil,
            .multisample = WGPU_MULTISAMPLE_DEFAULT
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(module);

    return impl->depth_resolve_pipeline ? 0 : -1;
}

void flecsEngine_depthResolve(
    const FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder)
{
    if (!impl->depth_resolve_pipeline ||
        !impl->msaa_depth_texture_view ||
        !impl->depth_texture_view)
    {
        return;
    }

    WGPUBindGroupEntry entry = {
        .binding = 0,
        .textureView = impl->msaa_depth_texture_view
    };

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(
        impl->device, &(WGPUBindGroupDescriptor){
            .layout = impl->depth_resolve_bind_layout,
            .entryCount = 1,
            .entries = &entry
        });
    if (!bind_group) {
        return;
    }

    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = impl->depth_texture_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 1.0f,
        .depthReadOnly = false,
        .stencilLoadOp = WGPULoadOp_Undefined,
        .stencilStoreOp = WGPUStoreOp_Undefined,
        .stencilClearValue = 0,
        .stencilReadOnly = true
    };

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 0,
        .colorAttachments = NULL,
        .depthStencilAttachment = &depth_attachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        encoder, &pass_desc);
    if (pass) {
        wgpuRenderPassEncoderSetPipeline(pass, impl->depth_resolve_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    wgpuBindGroupRelease(bind_group);
}
