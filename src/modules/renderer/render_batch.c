#include "renderer.h"
#include "flecs_engine.h"

static int32_t flecsVertexAttrFromType(
    const ecs_world_t *world,
    ecs_entity_t type,
    WGPUVertexAttribute *attrs,
    int32_t attr_count,
    int32_t location_offset)
{
    const EcsStruct *s = ecs_get(world, type, EcsStruct);
    if (!s) {
        char *str = ecs_id_str(world, type);
        ecs_err("cannot derive attributes from non-struct type '%s'",
            str);
        ecs_os_free(str);
        return -1;
    }

    int32_t i, member_count = ecs_vec_count(&s->members);
    if (ecs_vec_count(&s->members) >= attr_count) {
        char *str = ecs_id_str(world, type);
        ecs_err("cannot derive attributes from type '%s': too many "
            "members (%d, max is %d)", str, member_count, attr_count);
        ecs_os_free(str);
        return -1;
    }

    ecs_member_t *members = ecs_vec_first(&s->members);
    int32_t attr = 0;
    for (i = 0; i < member_count; i ++) {
        if (members[i].type == ecs_id(flecs_vec3_t)) {
            attrs[attr].format = WGPUVertexFormat_Float32x3;
            attrs[attr].shaderLocation = location_offset + attr;
            attrs[attr].offset = members[i].offset;
            attr ++;

        } else if (members[i].type == ecs_id(flecs_rgba_t)) {
            attrs[attr].format = WGPUVertexFormat_Unorm8x4;
            attrs[attr].shaderLocation = location_offset + attr;
            attrs[attr].offset = members[i].offset;
            attr ++;

        } else if (members[i].type == ecs_id(flecs_mat4_t)) {
            if ((attr + 4) >= attr_count) {
                char *str = ecs_id_str(world, type);
                ecs_err("cannot derive attributes from type '%s': "
                    "too many attributes (max is %d)", str, attr_count);
                ecs_os_free(str);
                return -1;
            }

            for (int32_t col = 0; col < 4; col ++) {
                attrs[attr].format = WGPUVertexFormat_Float32x4;
                attrs[attr].shaderLocation = location_offset + attr;
                attrs[attr].offset = members[i].offset + (sizeof(vec4) * col);
                attr ++;
            }
        } else {
            char *type_str = ecs_id_str(world, type);
            char *member_type_str = ecs_id_str(world, members[i].type);
            ecs_err("unsupported member type '%s' for attribute '%s' "
                "in type '%s'", member_type_str, members[i].name, type_str);
            ecs_os_free(member_type_str);
            ecs_os_free(type_str);
            return -1;
        }
    }

    return attr;
}

static uint64_t flecs_type_sizeof(
    const ecs_world_t *world,
    ecs_entity_t type)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, type);
    if (!ti) {
        char *str = ecs_id_str(world, type);
        ecs_err("entity '%s' is not a type", str);
        ecs_os_free(str);
        return 0;
    }

    return ti->size;
}

static int32_t flecsSetupInstanceBindings(
    const ecs_world_t *world,
    FlecsRenderBatch *rb,
    int32_t location_offset,
    WGPUVertexBufferLayout *vertex_buffers,
    int32_t vertex_buffer_count,
    WGPUVertexAttribute *instance_attrs)
{
    int32_t attr_count = 0;
    for (int i = 0; i < FLECS_ENGINE_INSTANCE_TYPES_MAX; i ++) {
        ecs_entity_t type = rb->instance_types[i];
        if (!type) {
            break;
        }

        // Setup instance attributes
        int32_t count = flecsVertexAttrFromType(
            world, rb->instance_types[i], &instance_attrs[attr_count], 16, location_offset);

        if (count == -1) {
            continue;
        }

        vertex_buffers[vertex_buffer_count ++] = (WGPUVertexBufferLayout){
            .arrayStride = flecs_type_sizeof(world, type),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = count,
            .attributes = &instance_attrs[attr_count],
        };

        location_offset += count;
        attr_count += count;
    }

    return vertex_buffer_count;
}

static void flecsSetupUniformBindings(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const ecs_entity_t *uniform_types,
    WGPUShaderStage visibility,
    FlecsRenderBatchImpl *impl)
{
    WGPUBindGroupLayoutEntry layout_entries[FLECS_ENGINE_UNIFORMS_MAX];
    WGPUBindGroupLayoutDescriptor bind_layout_desc = { .entries = layout_entries };
    WGPUBindGroupEntry entries[FLECS_ENGINE_UNIFORMS_MAX];

    ecs_os_zeromem(layout_entries);
    ecs_os_zeromem(entries);

    for (int b = 0; b < FLECS_ENGINE_UNIFORMS_MAX; b ++) {
        ecs_entity_t type = uniform_types[b];
        if (!type) {
            if (!b) {
                return;
            }

            break;
        }

        uint64_t type_size = flecs_type_sizeof(world, type);
        layout_entries[b].binding = b;
        layout_entries[b].visibility = visibility;
        layout_entries[b].buffer = (WGPUBufferBindingLayout){
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = type_size
        };

        WGPUBufferDescriptor uniform_desc = {
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = type_size
        };

        entries[b].binding = b;
        impl->uniform_buffers[b] = entries[b].buffer = 
            wgpuDeviceCreateBuffer(
                engine->device, &uniform_desc);
        entries[b].size = type_size;

        bind_layout_desc.entryCount = b + 1;
    }

    impl->bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device, &bind_layout_desc);

    WGPUBindGroupDescriptor bind_group_desc = {
        .entries = entries,
        .entryCount = bind_layout_desc.entryCount,
        .layout = impl->bind_layout
    };

    impl->bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &bind_group_desc);
}

void FlecsRenderBatch_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsRenderBatch *rb = ecs_field(it, FlecsRenderBatch, 0);
    WGPUVertexAttribute instance_attrs[256] = {0};
    WGPUVertexBufferLayout vertex_buffers[1 + FLECS_ENGINE_INSTANCE_TYPES_MAX] = {0};
    int32_t vertex_buffer_count = 0;

    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        FlecsRenderBatchImpl impl = {};

        if (!rb[i].vertex_type) {
            char *batch_name = ecs_get_path(world, e);
            ecs_err("missing vertex type for render batch %s", batch_name);
            ecs_os_free(batch_name);
            continue;
        }

        if (!rb[i].instance_types[0]) {
            char *batch_name = ecs_get_path(world, e);
            ecs_err("missing instance type for render batch %s", batch_name);
            ecs_os_free(batch_name);
            continue;
        }

        // Setup vertex attributes
        WGPUVertexAttribute vertex_attrs[16];
        int32_t vertex_attr_count = flecsVertexAttrFromType(
            world, rb[i].vertex_type, vertex_attrs, 16, 0);
        if (vertex_attr_count == -1) {
            continue;
        }

        WGPUVertexBufferLayout vertex_layout = {
            .arrayStride = flecs_type_sizeof(world, rb[i].vertex_type),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = vertex_attr_count,
            .attributes = vertex_attrs
        };

        vertex_buffers[0] = vertex_layout;
        vertex_buffer_count ++;

        // Setup instance data bindings
        vertex_buffer_count = flecsSetupInstanceBindings(world, &rb[i], 
            vertex_attr_count, vertex_buffers, vertex_buffer_count, 
            instance_attrs);

        // Setup uniform bindings
        flecsSetupUniformBindings(world, engine, 
            rb[i].uniforms, WGPUShaderStage_Vertex, &impl);

        // Setup shader
        WGPUShaderSourceWGSL wgsl_desc = {
            .chain = { 
                .sType = WGPUSType_ShaderSourceWGSL
            },
            .code = (WGPUStringView){
                .data = rb[i].shader, 
                .length = WGPU_STRLEN
            }
        };

        impl.shader = wgpuDeviceCreateShaderModule(
            engine->device, &(WGPUShaderModuleDescriptor){
                .nextInChain = (WGPUChainedStruct *)&wgsl_desc
            });

        // Setup pipeline layout
        WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &impl.bind_layout
        };

        WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
            engine->device, &pipeline_layout_desc);

        // Color output target: surface format with all color channels enabled.
        WGPUColorTargetState color_target = {
            .format = engine->surface_config.format,
            .writeMask = WGPUColorWriteMask_All
        };

        // Depth testing: write depth and keep nearest fragments.
        WGPUDepthStencilState depth_state = {
            .format = WGPUTextureFormat_Depth24Plus,
            .depthWriteEnabled = WGPUOptionalBool_True,
            .depthCompare = WGPUCompareFunction_Less,
            .stencilReadMask = 0xFFFFFFFF,
            .stencilWriteMask = 0xFFFFFFFF
        };

        // Vertex stage: entry point and vertex buffer layout.
        WGPUVertexState vertex_state = {
            .module = impl.shader,
            .entryPoint = (WGPUStringView){
                .data = "vs_main", .length = WGPU_STRLEN
            },
            .bufferCount = vertex_buffer_count,
            .buffers = vertex_buffers
        };

        // Fragment stage: entry point and color target.
        WGPUFragmentState fragment_state = {
            .module = impl.shader,
            .entryPoint = (WGPUStringView){
                .data = "fs_main", .length = WGPU_STRLEN
            },
            .targetCount = 1,
            .targets = &color_target
        };

        // Render pipeline: shaders, fixed-function state, and depth config.
        WGPURenderPipelineDescriptor pipeline_desc = {
            .layout = pipeline_layout,
            .vertex = vertex_state,
            .fragment = &fragment_state,
            .depthStencil = &depth_state,
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .cullMode = WGPUCullMode_None,
                .frontFace = WGPUFrontFace_CCW
            },
            .multisample = {
                .count = 1
            }
        };

        impl.pipeline = wgpuDeviceCreateRenderPipeline(
            engine->device, &pipeline_desc);

        ecs_set_ptr(world, e, FlecsRenderBatchImpl, &impl);
    }
}

void flecsEngineRenderBatch(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    const FlecsRenderBatch *batch,
    const FlecsRenderBatchImpl *impl)
{
    mat4 mvp; glm_mat4_identity(mvp);
    if (view->camera) {
        const FlecsCameraImpl *cam_impl = ecs_get(
            world, view->camera, FlecsCameraImpl);
        if (!cam_impl) {
            char *cam_name = ecs_get_path(world, view->camera);
            ecs_err("invalid camera '%s' in view", cam_name);
            ecs_os_free(cam_name);
        } else {
            glm_mat4_copy((vec4*)cam_impl->mvp, mvp);
        }
    }

    wgpuQueueWriteBuffer(
        engine->queue, 
        impl->uniform_buffers[0], 0, mvp, sizeof(mat4));

    wgpuRenderPassEncoderSetPipeline(pass, impl->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, impl->bind_group, 0, NULL);

    batch->callback(world, engine, pass, batch);
}
