#include <math.h>

#include "renderer.h"
#include "flecs_engine.h"

static float flecsEngineColorChannelToFloat(
    uint8_t value)
{
    return (float)value / 255.0f;
}

static void flecsShaderImplRelease(
    FlecsShaderImpl *ptr)
{
    if (ptr->shader_module) {
        wgpuShaderModuleRelease(ptr->shader_module);
        ptr->shader_module = NULL;
    }
}

static void flecsRenderBatchImplRelease(
    FlecsRenderBatchImpl *ptr)
{
    for (int32_t i = 0; i < FLECS_ENGINE_UNIFORMS_MAX; i ++) {
        if (ptr->uniform_buffers[i]) {
            wgpuBufferRelease(ptr->uniform_buffers[i]);
            ptr->uniform_buffers[i] = NULL;
        }
    }

    if (ptr->bind_group) {
        wgpuBindGroupRelease(ptr->bind_group);
        ptr->bind_group = NULL;
    }

    if (ptr->bind_layout) {
        wgpuBindGroupLayoutRelease(ptr->bind_layout);
        ptr->bind_layout = NULL;
    }

    if (ptr->pipeline_surface) {
        wgpuRenderPipelineRelease(ptr->pipeline_surface);
        ptr->pipeline_surface = NULL;
    }

    if (ptr->pipeline_hdr) {
        wgpuRenderPipelineRelease(ptr->pipeline_hdr);
        ptr->pipeline_hdr = NULL;
    }
}

ECS_DTOR(FlecsShaderImpl, ptr, {
    flecsShaderImplRelease(ptr);
})

ECS_DTOR(FlecsRenderBatchImpl, ptr, {
    flecsRenderBatchImplRelease(ptr);
})

static bool flecsShaderCompile(
    ecs_world_t *world,
    ecs_entity_t shader_entity,
    const FlecsShader *shader,
    FlecsShaderImpl *shader_impl)
{
    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    if (!engine) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("cannot compile shader '%s': engine is not initialized",
            name ? name : "<unnamed>");
        ecs_os_free(name);
        return false;
    }

    if (!shader || !shader->source) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("shader asset '%s' has no source", name ? name : "<unnamed>");
        ecs_os_free(name);
        flecsShaderImplRelease(shader_impl);
        return false;
    }

    flecsShaderImplRelease(shader_impl);

    WGPUShaderSourceWGSL wgsl_desc = {
        .chain = {
            .sType = WGPUSType_ShaderSourceWGSL
        },
        .code = (WGPUStringView){
            .data = shader->source,
            .length = WGPU_STRLEN
        }
    };

    shader_impl->shader_module = wgpuDeviceCreateShaderModule(
        engine->device, &(WGPUShaderModuleDescriptor){
            .nextInChain = (WGPUChainedStruct *)&wgsl_desc
        });

    if (!shader_impl->shader_module) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("failed to compile shader asset '%s'", name ? name : "<unnamed>");
        ecs_os_free(name);
        return false;
    }

    return true;
}

static void flecsMarkShaderUsersDirty(
    ecs_world_t *world,
    ecs_entity_t shader_entity)
{
    ecs_iter_t qit = ecs_each_id(world, ecs_id(FlecsRenderBatch));
    while (ecs_each_next(&qit)) {
        FlecsRenderBatch *batches = ecs_field(&qit, FlecsRenderBatch, 0);
        for (int32_t i = 0; i < qit.count; i ++) {
            if (batches[i].shader == shader_entity) {
                ecs_modified(world, qit.entities[i], FlecsRenderBatch);
            }
        }
    }

    ecs_iter_t eit = ecs_each_id(world, ecs_id(FlecsRenderEffect));
    while (ecs_each_next(&eit)) {
        FlecsRenderEffect *effects = ecs_field(&eit, FlecsRenderEffect, 0);
        for (int32_t i = 0; i < eit.count; i ++) {
            if (effects[i].shader == shader_entity) {
                ecs_modified(world, eit.entities[i], FlecsRenderEffect);
            }
        }
    }
}

void FlecsShader_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsShader *shaders = ecs_field(it, FlecsShader, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t shader_entity = it->entities[i];
        FlecsShaderImpl *shader_impl = ecs_ensure(world, shader_entity, FlecsShaderImpl);
        if (!flecsShaderCompile(world, shader_entity, &shaders[i], shader_impl)) {
            continue;
        }

        flecsMarkShaderUsersDirty(world, shader_entity);
    }
}

static const FlecsShaderImpl* flecsEnsureShaderImpl(
    ecs_world_t *world,
    ecs_entity_t shader_entity)
{
    const FlecsShader *shader = ecs_get(world, shader_entity, FlecsShader);
    if (!shader) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("entity '%s' does not have FlecsShader", name ? name : "<unnamed>");
        ecs_os_free(name);
        return NULL;
    }

    FlecsShaderImpl *shader_impl = ecs_ensure(world, shader_entity, FlecsShaderImpl);
    if (!shader_impl->shader_module) {
        if (!flecsShaderCompile(world, shader_entity, shader, shader_impl)) {
            return NULL;
        }
    }

    return shader_impl;
}

ecs_entity_t flecsEngineEnsureShader(
    ecs_world_t *world,
    const char *name,
    const FlecsShader *shader)
{
    ecs_entity_t renderer_module = ecs_lookup(world, "flecs.engine.renderer");
    ecs_entity_t shader_entity = ecs_entity_init(world, &(ecs_entity_desc_t){
        .name = name,
        .parent = renderer_module
    });

    const FlecsShader *existing = ecs_get(world, shader_entity, FlecsShader);
    if (!existing ||
        !ecs_os_strcmp(existing->source, shader->source) ||
        !ecs_os_strcmp(existing->vertex_entry, shader->vertex_entry) ||
        !ecs_os_strcmp(existing->fragment_entry, shader->fragment_entry))
    {
        ecs_set_ptr(world, shader_entity, FlecsShader, shader);
    }

    return shader_entity;
}

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

        } else if (members[i].type == ecs_id(ecs_f32_t)) {
            attrs[attr].format = WGPUVertexFormat_Float32;
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

static WGPURenderPipeline flecsCreateRenderBatchPipeline(
    const FlecsEngineImpl *engine,
    const FlecsShader *shader,
    const FlecsShaderImpl *shader_impl,
    WGPUBindGroupLayout bind_layout,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count,
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

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth24Plus,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Less,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };

    WGPUVertexState vertex_state = {
        .module = shader_impl->shader_module,
        .entryPoint = (WGPUStringView){
            .data = shader->vertex_entry ? shader->vertex_entry : "vs_main",
            .length = WGPU_STRLEN
        },
        .bufferCount = vertex_buffer_count,
        .buffers = vertex_buffers
    };

    WGPUFragmentState fragment_state = {
        .module = shader_impl->shader_module,
        .entryPoint = (WGPUStringView){
            .data = shader->fragment_entry ? shader->fragment_entry : "fs_main",
            .length = WGPU_STRLEN
        },
        .targetCount = 1,
        .targets = &color_target
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = vertex_state,
        .fragment = &fragment_state,
        .depthStencil = &depth_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_Back,
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

void FlecsRenderBatch_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsRenderBatch *rb = ecs_field(it, FlecsRenderBatch, 0);
    WGPUVertexAttribute instance_attrs[256] = {0};
    WGPUVertexBufferLayout vertex_buffers[1 + FLECS_ENGINE_INSTANCE_TYPES_MAX] = {0};
    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    if (!engine) {
        ecs_err("cannot build render batches: engine is not initialized");
        return;
    }
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        int32_t vertex_buffer_count = 0;

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

        if (!rb[i].shader) {
            char *batch_name = ecs_get_path(world, e);
            ecs_err("missing shader asset for render batch %s", batch_name);
            ecs_os_free(batch_name);
            continue;
        }

        const FlecsShader *shader = ecs_get(world, rb[i].shader, FlecsShader);
        if (!shader) {
            char *batch_name = ecs_get_path(world, e);
            char *shader_name = ecs_get_path(world, rb[i].shader);
            ecs_err("invalid shader asset '%s' for render batch %s",
                shader_name ? shader_name : "<unnamed>",
                batch_name ? batch_name : "<unnamed>");
            ecs_os_free(shader_name);
            ecs_os_free(batch_name);
            continue;
        }

        const FlecsShaderImpl *shader_impl = flecsEnsureShaderImpl(world, rb[i].shader);
        if (!shader_impl || !shader_impl->shader_module) {
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
            rb[i].uniforms,
            WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            &impl);

        impl.pipeline_surface = flecsCreateRenderBatchPipeline(
            engine,
            shader,
            shader_impl,
            impl.bind_layout,
            vertex_buffers,
            (uint32_t)vertex_buffer_count,
            engine->surface_config.format);
        if (!impl.pipeline_surface) {
            flecsRenderBatchImplRelease(&impl);
            continue;
        }

        WGPUTextureFormat hdr_format = engine->hdr_color_format;
        if (hdr_format == WGPUTextureFormat_Undefined) {
            hdr_format = engine->surface_config.format;
        }

        impl.pipeline_hdr = flecsCreateRenderBatchPipeline(
            engine,
            shader,
            shader_impl,
            impl.bind_layout,
            vertex_buffers,
            (uint32_t)vertex_buffer_count,
            hdr_format);
        if (!impl.pipeline_hdr) {
            flecsRenderBatchImplRelease(&impl);
            continue;
        }

        ecs_set_ptr(world, e, FlecsRenderBatchImpl, &impl);
    }
}

void flecsEngineRenderBatch_setupCamera(
    const ecs_world_t *world,
    FlecsUniform *uniforms,
    ecs_entity_t entity)
{
    uniforms->camera_pos[0] = 0.0f;
    uniforms->camera_pos[1] = 0.0f;
    uniforms->camera_pos[2] = 0.0f;
    uniforms->camera_pos[3] = 1.0f;

    glm_mat4_identity(uniforms->mvp);
    const FlecsCameraImpl *camera = ecs_get(
        world, entity, FlecsCameraImpl);
    if (!camera) {
        char *cam_name = ecs_get_path(world, entity);
        ecs_err("invalid camera '%s' in view", cam_name);
        ecs_os_free(cam_name);
        return;
    }

    glm_mat4_copy((vec4*)camera->mvp, uniforms->mvp);

    const FlecsWorldTransform3 *camera_transform = ecs_get(
        world, entity, FlecsWorldTransform3);
    if (camera_transform) {
        uniforms->camera_pos[0] = camera_transform->m[3][0];
        uniforms->camera_pos[1] = camera_transform->m[3][1];
        uniforms->camera_pos[2] = camera_transform->m[3][2];
    }
}

void flecsEngineRenderBatch_setupLight(
    const ecs_world_t *world,
    FlecsUniform *uniforms,
    ecs_entity_t entity)
{
    uniforms->light_color[0] = 1.0f;
    uniforms->light_color[1] = 1.0f;
    uniforms->light_color[2] = 1.0f;
    uniforms->light_color[3] = 1.0f;

    const FlecsDirectionalLight *light = ecs_get(
        world, entity, FlecsDirectionalLight);
    if (!light) {
        char *light_name = ecs_get_path(world, entity);
        ecs_err("invalid directional light '%s'", light_name);
        ecs_os_free(light_name);
        return;
    }

    const FlecsRotation3 *rotation = ecs_get(world, entity, FlecsRotation3);
    if (!rotation) {
        char *light_name = ecs_get_path(world, entity);
        ecs_err("directional light '%s' is missing Rotation3", light_name);
        ecs_os_free(light_name);
        return;
    }

    float pitch = rotation->x;
    float yaw = rotation->y;
    vec3 ray_dir = {
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    };

    float ray_len = glm_vec3_norm(ray_dir);
    if (ray_len <= 1e-6f) {
        char *light_name = ecs_get_path(world, entity);
        ecs_err(
            "directional light '%s' has invalid Rotation3", light_name);
        ecs_os_free(light_name);
        return;
    }

    glm_vec3_scale(ray_dir, 1.0f / ray_len, ray_dir);
    uniforms->light_ray_dir[0] = ray_dir[0];
    uniforms->light_ray_dir[1] = ray_dir[1];
    uniforms->light_ray_dir[2] = ray_dir[2];

    FlecsRgba rgb = {255, 255, 255, 255};
    const FlecsRgba *light_rgb = ecs_get(world, entity, FlecsRgba);
    if (light_rgb) {
        rgb = *light_rgb;
    }

    uniforms->light_color[0] =
        flecsEngineColorChannelToFloat(rgb.r) * light->intensity;
    uniforms->light_color[1] =
        flecsEngineColorChannelToFloat(rgb.g) * light->intensity;
    uniforms->light_color[2] =
        flecsEngineColorChannelToFloat(rgb.b) * light->intensity;
}

void flecsEngineRenderBatch(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    ecs_entity_t batch_entity)
{
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    const FlecsRenderBatchImpl *impl = ecs_get(
        world, batch_entity, FlecsRenderBatchImpl);

    FlecsUniform uniforms = {0};
    uniforms.camera_pos[3] = 1.0f;

    if (view->camera) {
        flecsEngineRenderBatch_setupCamera(world, &uniforms, view->camera);
    }

    if (view->light) {
        flecsEngineRenderBatch_setupLight(world, &uniforms, view->light);
    }

    flecsEngineGetClearColorVec4(engine, uniforms.clear_color);

    wgpuQueueWriteBuffer(
        engine->queue, 
        impl->uniform_buffers[0], 0, &uniforms, sizeof(FlecsUniform));

    WGPURenderPipeline pipeline = impl->pipeline_hdr;
    ecs_assert(pipeline != NULL, ECS_INTERNAL_ERROR, NULL);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, impl->bind_group, 0, NULL);

    batch->callback(world, engine, pass, batch);
}
