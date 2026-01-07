#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>
#include <cglm/clipspace/persp_rh_zo.h>
#include <webgpu.h>

typedef struct {
  float x, y, z;
  uint8_t r, g, b, a;
} Vertex;

// Allocate a depth texture sized to the current framebuffer.
static void create_depth_resources(WGPUDevice device, uint32_t width,
                                   uint32_t height, WGPUTexture *texture,
                                   WGPUTextureView *view) {
  if (*view) {
    wgpuTextureViewRelease(*view);
    *view = NULL;
  }
  if (*texture) {
    wgpuTextureRelease(*texture);
    *texture = NULL;
  }

  WGPUTextureDescriptor depth_desc = {
      .usage = WGPUTextureUsage_RenderAttachment,
      .dimension = WGPUTextureDimension_2D,
      .size = (WGPUExtent3D){.width = width,
                            .height = height,
                            .depthOrArrayLayers = 1},
      .format = WGPUTextureFormat_Depth24Plus,
      .mipLevelCount = 1,
      .sampleCount = 1};
  *texture = wgpuDeviceCreateTexture(device, &depth_desc);
  *view = wgpuTextureCreateView(*texture, NULL);
}

static const Vertex kVertices[] = {
    {-1.0f, -1.0f, -1.0f, 0, 51, 51, 255},
    {1.0f, -1.0f, -1.0f, 51, 255, 51, 255},
    {1.0f, 1.0f, -1.0f, 51, 51, 255, 255},
    {-1.0f, 1.0f, -1.0f, 255, 255, 51, 255},
    {-1.0f, -1.0f, 1.0f, 51, 255, 255, 255},
    {1.0f, -1.0f, 1.0f, 255, 51, 255, 255},
    {1.0f, 1.0f, 1.0f, 255, 255, 255, 255},
    {-1.0f, 1.0f, 1.0f, 51, 51, 51, 255},
};

static const uint16_t kIndices[] = {
    0, 2, 1, 0, 3, 2,  // back
    4, 5, 6, 4, 6, 7,  // front
    0, 1, 5, 0, 5, 4,  // bottom
    3, 6, 2, 3, 7, 6,  // top
    0, 4, 7, 0, 7, 3,  // left
    1, 2, 6, 1, 6, 5   // right
};

typedef struct {
  WGPUAdapter adapter;
  bool done;
} AdapterRequest;

typedef struct {
  WGPUDevice device;
  bool done;
} DeviceRequest;

// Adapter request callback: captures the selected adapter or prints an error.
static void on_request_adapter(WGPURequestAdapterStatus status,
                               WGPUAdapter adapter, WGPUStringView message,
                               void *userdata1, void *userdata2) {
  (void)userdata2;
  AdapterRequest *request = userdata1;
  if (status == WGPURequestAdapterStatus_Success) {
    request->adapter = adapter;
  } else {
    if (message.data) {
      fprintf(stderr, "Adapter request failed: %.*s\n",
              (int)message.length, message.data);
    } else {
      fprintf(stderr, "Adapter request failed: unknown\n");
    }
  }
  request->done = true;
}

// Device request callback: captures the created device or prints an error.
static void on_request_device(WGPURequestDeviceStatus status, WGPUDevice device,
                              WGPUStringView message, void *userdata1,
                              void *userdata2) {
  (void)userdata2;
  DeviceRequest *request = userdata1;
  if (status == WGPURequestDeviceStatus_Success) {
    request->device = device;
  } else {
    if (message.data) {
      fprintf(stderr, "Device request failed: %.*s\n",
              (int)message.length, message.data);
    } else {
      fprintf(stderr, "Device request failed: unknown\n");
    }
  }
  request->done = true;
}

// Spin until the async WGPUFuture completes.
static void wait_for_future(WGPUInstance instance, WGPUFuture future,
                            bool *done) {
  // Wait info: poll until the async callback flips the done flag.
  WGPUFutureWaitInfo wait_info = {.future = future};
  while (!*done) {
    wait_info.completed = false;
    wgpuInstanceWaitAny(instance, 1, &wait_info, 0);
  }
}

void *flecs_create_metal_layer(void *ns_window);

// Minimal WGSL shader for colored vertices and a uniform MVP matrix.
static const char *kShaderSource =
    "struct Uniforms { mvp : mat4x4<f32> };\n"
    "@group(0) @binding(0) var<uniform> uniforms : Uniforms;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) color : vec4<f32>\n"
    "};\n"
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) color : vec3<f32>\n"
    "};\n"
    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  out.pos = uniforms.mvp * vec4<f32>(input.pos, 1.0);\n"
    "  out.color = input.color.rgb;\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(input.color, 1.0);\n"
    "}\n";

// Main loop: init WebGPU, create resources, then render a rotating cube.
int main(void) {
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  GLFWwindow *window =
      glfwCreateWindow(1280, 720, "wgpu rotating cube", NULL, NULL);
  if (!window) {
    fprintf(stderr, "Failed to create window\n");
    glfwTerminate();
    return 1;
  }

  // Instance descriptor: default options.
  WGPUInstanceDescriptor instance_desc = {0};
  WGPUInstance instance = wgpuCreateInstance(&instance_desc);
  if (!instance) {
    fprintf(stderr, "Failed to create WGPU instance\n");
    glfwTerminate();
    return 1;
  }

  void *metal_layer = flecs_create_metal_layer(glfwGetCocoaWindow(window));
  // Surface descriptor: use the CAMetalLayer from the GLFW window.
  WGPUSurfaceSourceMetalLayer metal_desc = {
      .chain = {.sType = WGPUSType_SurfaceSourceMetalLayer},
      .layer = metal_layer};
  WGPUSurfaceDescriptor surface_desc = {
      .nextInChain = (WGPUChainedStruct *)&metal_desc};
  WGPUSurface surface = wgpuInstanceCreateSurface(instance, &surface_desc);

  AdapterRequest adapter_request = {0};
  // Adapter options: select an adapter compatible with the surface.
  WGPURequestAdapterOptions adapter_options = {.compatibleSurface = surface};
  // Adapter callback info: wait mode + callback context.
  WGPURequestAdapterCallbackInfo adapter_callback = {
      .mode = WGPUCallbackMode_WaitAnyOnly,
      .callback = on_request_adapter,
      .userdata1 = &adapter_request};
  WGPUFuture adapter_future =
      wgpuInstanceRequestAdapter(instance, &adapter_options, adapter_callback);
  wait_for_future(instance, adapter_future, &adapter_request.done);
  if (!adapter_request.adapter) {
    glfwTerminate();
    return 1;
  }

  DeviceRequest device_request = {0};
  // Device descriptor: default limits/features.
  WGPUDeviceDescriptor device_desc = {0};
  // Device callback info: wait mode + callback context.
  WGPURequestDeviceCallbackInfo device_callback = {
      .mode = WGPUCallbackMode_WaitAnyOnly,
      .callback = on_request_device,
      .userdata1 = &device_request};
  WGPUFuture device_future = wgpuAdapterRequestDevice(
      adapter_request.adapter, &device_desc, device_callback);
  wait_for_future(instance, device_future, &device_request.done);
  if (!device_request.device) {
    glfwTerminate();
    return 1;
  }

  WGPUDevice device = device_request.device;
  WGPUQueue queue = wgpuDeviceGetQueue(device);

  WGPUSurfaceCapabilities surface_caps = {0};
  if (wgpuSurfaceGetCapabilities(surface, adapter_request.adapter,
                                 &surface_caps) != WGPUStatus_Success ||
      surface_caps.formatCount == 0) {
    fprintf(stderr, "Failed to get surface capabilities\n");
    glfwTerminate();
    return 1;
  }
  // Choose the first supported surface format.
  WGPUTextureFormat surface_format = surface_caps.formats[0];

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  // Surface config: color attachment usage, format, size, present mode.
  WGPUSurfaceConfiguration surface_config = {
      .device = device,
      .usage = WGPUTextureUsage_RenderAttachment,
      .format = surface_format,
      .width = (uint32_t)width,
      .height = (uint32_t)height,
      .presentMode = WGPUPresentMode_Fifo,
      .alphaMode = surface_caps.alphaModes[0]};
  wgpuSurfaceConfigure(surface, &surface_config);
  wgpuSurfaceCapabilitiesFreeMembers(surface_caps);

  // Vertex buffer: GPU vertex data uploaded from CPU.
  WGPUBufferDescriptor vertex_desc = {
      .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
      .size = sizeof(kVertices)};
  WGPUBuffer vertex_buffer = wgpuDeviceCreateBuffer(device, &vertex_desc);
  wgpuQueueWriteBuffer(queue, vertex_buffer, 0, kVertices, sizeof(kVertices));

  // Index buffer: triangle indices uploaded from CPU.
  WGPUBufferDescriptor index_desc = {
      .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
      .size = sizeof(kIndices)};
  WGPUBuffer index_buffer = wgpuDeviceCreateBuffer(device, &index_desc);
  wgpuQueueWriteBuffer(queue, index_buffer, 0, kIndices, sizeof(kIndices));

  // Uniform buffer: per-frame MVP matrix.
  WGPUBufferDescriptor uniform_desc = {
      .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
      .size = sizeof(mat4)};
  WGPUBuffer uniform_buffer = wgpuDeviceCreateBuffer(device, &uniform_desc);

  // Bind group layout: a single uniform buffer at binding 0 in the vertex stage.
  WGPUBindGroupLayoutEntry bind_entry = {
      .binding = 0,
      .visibility = WGPUShaderStage_Vertex,
      .buffer = {.type = WGPUBufferBindingType_Uniform,
                 .minBindingSize = sizeof(mat4)}};
  WGPUBindGroupLayoutDescriptor bind_layout_desc = {
      .entryCount = 1,
      .entries = &bind_entry};
  WGPUBindGroupLayout bind_layout =
      wgpuDeviceCreateBindGroupLayout(device, &bind_layout_desc);

  // Bind group entry: point binding 0 at the uniform buffer.
  WGPUBindGroupEntry bind_group_entry = {
      .binding = 0,
      .buffer = uniform_buffer,
      .size = sizeof(mat4)};
  WGPUBindGroupDescriptor bind_group_desc = {
      .layout = bind_layout,
      .entryCount = 1,
      .entries = &bind_group_entry};
  WGPUBindGroup bind_group =
      wgpuDeviceCreateBindGroup(device, &bind_group_desc);

  // Shader module: WGSL source embedded as a string.
  WGPUShaderSourceWGSL wgsl_desc = {
      .chain = {.sType = WGPUSType_ShaderSourceWGSL},
      .code = (WGPUStringView){.data = kShaderSource, .length = WGPU_STRLEN}};
  WGPUShaderModuleDescriptor shader_desc = {
      .nextInChain = (WGPUChainedStruct *)&wgsl_desc};
  WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_desc);

  // Vertex layout: position and color attributes.
  WGPUVertexAttribute vertex_attrs[2] = {
      {.format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0},
      {.format = WGPUVertexFormat_Unorm8x4,
       .offset = sizeof(float) * 3,
       .shaderLocation = 1}};
  WGPUVertexBufferLayout vertex_layout = {
      .arrayStride = sizeof(Vertex),
      .stepMode = WGPUVertexStepMode_Vertex,
      .attributeCount = 2,
      .attributes = vertex_attrs};

  // Pipeline layout: one bind group layout (uniforms).
  WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &bind_layout};
  WGPUPipelineLayout pipeline_layout =
      wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_desc);

  // Color output target: surface format with all color channels enabled.
  WGPUColorTargetState color_target = {.format = surface_format,
                                       .writeMask = WGPUColorWriteMask_All};
  // Depth testing: write depth and keep nearest fragments.
  WGPUDepthStencilState depth_state = {
      .format = WGPUTextureFormat_Depth24Plus,
      .depthWriteEnabled = WGPUOptionalBool_True,
      .depthCompare = WGPUCompareFunction_Less,
      .stencilReadMask = 0xFFFFFFFF,
      .stencilWriteMask = 0xFFFFFFFF};
  WGPUStringView fs_entry = {.data = "fs_main", .length = WGPU_STRLEN};
  WGPUStringView vs_entry = {.data = "vs_main", .length = WGPU_STRLEN};
  // Fragment stage: entry point and color target.
  WGPUFragmentState fragment_state = {.module = shader,
                                      .entryPoint = fs_entry,
                                      .targetCount = 1,
                                      .targets = &color_target};
  // Vertex stage: entry point and vertex buffer layout.
  WGPUVertexState vertex_state = {.module = shader,
                                  .entryPoint = vs_entry,
                                  .bufferCount = 1,
                                  .buffers = &vertex_layout};

  // Render pipeline: shaders, fixed-function state, and depth config.
  WGPURenderPipelineDescriptor pipeline_desc = {
      .layout = pipeline_layout,
      .vertex = vertex_state,
      .fragment = &fragment_state,
      .depthStencil = &depth_state,
      .primitive = {.topology = WGPUPrimitiveTopology_TriangleList,
                    .cullMode = WGPUCullMode_Back,
                    .frontFace = WGPUFrontFace_CCW},
      .multisample = {.count = 1}};
  WGPURenderPipeline pipeline =
      wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);

  // Depth resources track the surface size.
  WGPUTexture depth_texture = NULL;
  WGPUTextureView depth_view = NULL;
  if (width > 0 && height > 0) {
    create_depth_resources(device, (uint32_t)width, (uint32_t)height,
                           &depth_texture, &depth_view);
  }

  double start_time = glfwGetTime();
  int last_width = width;
  int last_height = height;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    glfwGetFramebufferSize(window, &width, &height);
    if (width != last_width || height != last_height) {
      surface_config.width = (uint32_t)width;
      surface_config.height = (uint32_t)height;
      wgpuSurfaceConfigure(surface, &surface_config);
      if (width > 0 && height > 0) {
        create_depth_resources(device, (uint32_t)width, (uint32_t)height,
                               &depth_texture, &depth_view);
      }
      last_width = width;
      last_height = height;
    }
    if (width == 0 || height == 0) {
      continue;
    }

    float time = (float)(glfwGetTime() - start_time);
    mat4 rot_y = GLM_MAT4_IDENTITY_INIT;
    mat4 rot_x = GLM_MAT4_IDENTITY_INIT;
    mat4 model;
    glm_rotate_y(rot_y, time, rot_y);
    glm_rotate_x(rot_x, time * 0.7f, rot_x);
    glm_mat4_mul(rot_y, rot_x, model);

    mat4 view = GLM_MAT4_IDENTITY_INIT;
    glm_translate(view, (vec3){0.0f, 0.0f, -5.0f});

    mat4 proj;
    glm_perspective_rh_zo(0.7f, (float)width / (float)height, 0.1f, 100.0f,
                          proj);

    mat4 view_model;
    mat4 mvp;
    glm_mat4_mul(view, model, view_model);
    glm_mat4_mul(proj, view_model, mvp);
    wgpuQueueWriteBuffer(queue, uniform_buffer, 0, mvp, sizeof(mat4));

    // Acquire the next swapchain texture.
    WGPUSurfaceTexture surface_texture = {0};
    wgpuSurfaceGetCurrentTexture(surface, &surface_texture);
    if (surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
      wgpuSurfaceConfigure(surface, &surface_config);
      continue;
    }

    // Create a view of the swapchain texture.
    WGPUTextureView view_texture =
        wgpuTextureCreateView(surface_texture.texture, NULL);
    // Command encoder: record GPU commands for this frame.
    WGPUCommandEncoderDescriptor encoder_desc = {0};
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(device, &encoder_desc);

    // Color attachment: clear to background and render into swapchain.
    WGPURenderPassColorAttachment color_attachment = {
        .view = view_texture,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0.05, 0.05, 0.08, 1.0}};
    // Depth attachment: clear depth each frame.
    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = depth_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 1.0f,
        .depthReadOnly = false,
        .stencilLoadOp = WGPULoadOp_Undefined,
        .stencilStoreOp = WGPUStoreOp_Undefined,
        .stencilClearValue = 0,
        .stencilReadOnly = true};
    // Render pass: bind color and depth attachments.
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = 1,
                                          .colorAttachments =
                                              &color_attachment,
                                          .depthStencilAttachment =
                                              &depth_attachment};
    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertex_buffer, 0,
                                         sizeof(kVertices));
    wgpuRenderPassEncoderSetIndexBuffer(pass, index_buffer,
                                        WGPUIndexFormat_Uint16, 0,
                                        sizeof(kIndices));
    wgpuRenderPassEncoderDrawIndexed(pass, (uint32_t)(sizeof(kIndices) /
                                                      sizeof(kIndices[0])),
                                     1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    // Finalize the command buffer for submission.
    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuSurfacePresent(surface);

    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view_texture);
    wgpuTextureRelease(surface_texture.texture);
  }

  wgpuRenderPipelineRelease(pipeline);
  wgpuPipelineLayoutRelease(pipeline_layout);
  wgpuShaderModuleRelease(shader);
  wgpuBindGroupRelease(bind_group);
  wgpuBindGroupLayoutRelease(bind_layout);
  wgpuBufferRelease(uniform_buffer);
  wgpuBufferRelease(index_buffer);
  wgpuBufferRelease(vertex_buffer);
  wgpuTextureViewRelease(depth_view);
  wgpuTextureRelease(depth_texture);
  wgpuSurfaceRelease(surface);
  wgpuQueueRelease(queue);
  wgpuDeviceRelease(device);
  wgpuAdapterRelease(adapter_request.adapter);
  wgpuInstanceRelease(instance);

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
