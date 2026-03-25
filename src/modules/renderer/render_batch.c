#include <math.h>
#include <string.h>

#include "renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderBatch);
ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);
ECS_TAG_DECLARE(FlecsSkyboxBatch);
ECS_TAG_DECLARE(FlecsTransparentBatch);

ECS_DTOR(FlecsRenderBatch, ptr, {
    if (ptr->ctx && ptr->free_ctx) {
        ptr->free_ctx(ptr->ctx);
    }
})

ECS_MOVE(FlecsRenderBatch, dst, src, {
    if (dst->ctx && dst->free_ctx) {
        dst->free_ctx(dst->ctx);
    }
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_renderBatch_releaseImpl(
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

    if (ptr->bind_group_materials) {
        wgpuBindGroupRelease(ptr->bind_group_materials);
        ptr->bind_group_materials = NULL;
    }

    ptr->material_buffer_size = 0;

    if (ptr->bind_layout) {
        wgpuBindGroupLayoutRelease(ptr->bind_layout);
        ptr->bind_layout = NULL;
    }

    if (ptr->pipeline_hdr) {
        wgpuRenderPipelineRelease(ptr->pipeline_hdr);
        ptr->pipeline_hdr = NULL;
    }

    if (ptr->pipeline_shadow) {
        wgpuRenderPipelineRelease(ptr->pipeline_shadow);
        ptr->pipeline_shadow = NULL;
    }
}

ECS_DTOR(FlecsRenderBatchImpl, ptr, {
    flecsEngine_renderBatch_releaseImpl(ptr);
})

ECS_MOVE(FlecsRenderBatchImpl, dst, src, {
    flecsEngine_renderBatch_releaseImpl(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COMPONENT_DECLARE(FlecsRenderBatchSet);

ECS_CTOR(FlecsRenderBatchSet, ptr, {
    ecs_vec_init_t(NULL, &ptr->batches, ecs_entity_t, 0);
})

ECS_MOVE(FlecsRenderBatchSet, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderBatchSet, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    dst->batches = ecs_vec_copy_t(NULL, &src->batches, ecs_entity_t);
})

ECS_DTOR(FlecsRenderBatchSet, ptr, {
    ecs_vec_fini_t(NULL, &ptr->batches, ecs_entity_t);
})

static bool flecsEngine_renderBatch_usesMaterialId(
    const FlecsRenderBatch *batch)
{
    for (int32_t i = 0; i < FLECS_ENGINE_INSTANCE_TYPES_MAX; i ++) {
        ecs_entity_t type = batch->instance_types[i];
        if (!type) {
            break;
        }

        if (type == ecs_id(FlecsMaterialId)) {
            return true;
        }
    }

    return false;
}

static uint8_t flecsEngine_renderBatch_uniformCount(
    const ecs_entity_t *uniform_types)
{
    uint8_t result = 0;
    for (int32_t i = 0; i < FLECS_ENGINE_UNIFORMS_MAX; i ++) {
        if (!uniform_types[i]) {
            break;
        }
        result ++;
    }

    return result;
}

static int32_t flecsEngine_renderBatch_setupInstanceBindings(
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
        int32_t count = flecsEngine_vertexAttrFromType(
            world, rb->instance_types[i], &instance_attrs[attr_count], 16, location_offset);

        if (count == -1) {
            continue;
        }

        vertex_buffers[vertex_buffer_count ++] = (WGPUVertexBufferLayout){
            .arrayStride = flecsEngine_type_sizeof(world, type),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = count,
            .attributes = &instance_attrs[attr_count],
        };

        location_offset += count;
        attr_count += count;
    }

    return vertex_buffer_count;
}

static bool flecsEngine_renderBatch_setupBindings(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const ecs_entity_t *uniform_types,
    WGPUShaderStage visibility,
    bool include_material_buffer,
    FlecsRenderBatchImpl *impl)
{
    WGPUBindGroupLayoutEntry layout_entries[FLECS_ENGINE_UNIFORMS_MAX + 1];
    WGPUBindGroupLayoutDescriptor bind_layout_desc = { .entries = layout_entries };
    WGPUBindGroupEntry entries[FLECS_ENGINE_UNIFORMS_MAX];

    ecs_os_memset_n(
        layout_entries, 0, WGPUBindGroupLayoutEntry, FLECS_ENGINE_UNIFORMS_MAX + 1);
    ecs_os_memset_n(entries, 0, WGPUBindGroupEntry, FLECS_ENGINE_UNIFORMS_MAX);

    impl->uniform_count = flecsEngine_renderBatch_uniformCount(uniform_types);
    if (!impl->uniform_count) {
        return false;
    }

    for (uint8_t b = 0; b < impl->uniform_count; b ++) {
        ecs_entity_t type = uniform_types[b];

        uint64_t type_size = flecsEngine_type_sizeof(world, type);
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

        bind_layout_desc.entryCount = (uint32_t)b + 1;
    }

    if (include_material_buffer) {
        uint32_t binding = impl->uniform_count;
        layout_entries[binding].binding = binding;
        layout_entries[binding].visibility = WGPUShaderStage_Fragment;
        layout_entries[binding].buffer = (WGPUBufferBindingLayout){
            .type = WGPUBufferBindingType_ReadOnlyStorage,
            .minBindingSize = sizeof(FlecsGpuMaterial)
        };
        bind_layout_desc.entryCount = binding + 1;
    }

    impl->bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device, &bind_layout_desc);
    if (!impl->bind_layout) {
        return false;
    }

    if (!include_material_buffer) {
        WGPUBindGroupDescriptor bind_group_desc = {
            .entries = entries,
            .entryCount = impl->uniform_count,
            .layout = impl->bind_layout
        };

        impl->bind_group = wgpuDeviceCreateBindGroup(
            engine->device, &bind_group_desc);
        if (!impl->bind_group) {
            return false;
        }
    }

    return true;
}

static WGPURenderPipeline flecsEngine_renderBatch_createShadowPipeline(
    const FlecsEngineImpl *engine,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count)
{
    if (!engine->shadow.shader_module || !engine->shadow.pass_bind_layout) {
        return NULL;
    }

    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &engine->shadow.pass_bind_layout
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Less,
        .depthBias = 2,
        .depthBiasSlopeScale = 2.0f,
        .depthBiasClamp = 0.01f,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };

    WGPUVertexState vertex_state = {
        .module = engine->shadow.shader_module,
        .entryPoint = WGPU_STR("vs_main"),
        .bufferCount = vertex_buffer_count,
        .buffers = vertex_buffers
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = vertex_state,
        .fragment = NULL,
        .depthStencil = &depth_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = WGPU_MULTISAMPLE_DEFAULT
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);

    return pipeline;
}

static WGPURenderPipeline flecsEngine_renderBatch_createPipeline(
    const FlecsEngineImpl *engine,
    const FlecsShader *shader,
    const FlecsShaderImpl *shader_impl,
    WGPUBindGroupLayout bind_layout,
    bool use_ibl,
    bool use_shadow,
    bool use_cluster,
    bool use_textures,
    bool is_skybox,
    bool is_transparent,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count,
    WGPUTextureFormat color_format,
    uint32_t sample_count)
{
    WGPUBindGroupLayout bind_layouts[3] = { bind_layout };
    uint32_t bind_layout_count = 1u;
    if ((use_ibl || use_shadow || use_cluster) &&
        engine->ibl_shadow_bind_layout)
    {
        bind_layouts[bind_layout_count++] = engine->ibl_shadow_bind_layout;
    }
    if (use_textures) {
        WGPUBindGroupLayout tex_layout =
            flecsEngine_pbr_texture_ensureBindLayout((FlecsEngineImpl*)engine);
        if (tex_layout) {
            bind_layouts[bind_layout_count++] = tex_layout;
        }
    }

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = bind_layout_count,
        .bindGroupLayouts = bind_layouts
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &pipeline_layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUBlendState blend_state = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_SrcAlpha,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
        },
        .alpha = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
        }
    };

    WGPUColorTargetState color_target = {
        .format = color_format,
        .writeMask = WGPUColorWriteMask_All,
        .blend = is_transparent ? &blend_state : NULL
    };

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth24Plus,
        .depthWriteEnabled = is_transparent
            ? WGPUOptionalBool_False : WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Less,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };

    if (is_skybox) {
        depth_state.depthWriteEnabled = WGPUOptionalBool_False;
        depth_state.depthCompare = WGPUCompareFunction_LessEqual;
    }

    WGPUVertexState vertex_state = {
        .module = shader_impl->shader_module,
        .entryPoint = WGPU_STR(shader->vertex_entry ? shader->vertex_entry : "vs_main"),
        .bufferCount = vertex_buffer_count,
        .buffers = vertex_buffers
    };

    WGPUFragmentState fragment_state = {
        .module = shader_impl->shader_module,
        .entryPoint = WGPU_STR(shader->fragment_entry ? shader->fragment_entry : "fs_main"),
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
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = WGPU_MULTISAMPLE(sample_count)
    };

    if (is_skybox || is_transparent) {
        pipeline_desc.primitive.cullMode = WGPUCullMode_None;
    }

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);

    return pipeline;
}

static void FlecsRenderBatch_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsRenderBatch *rb = ecs_field(it, FlecsRenderBatch, 0);
    WGPUVertexAttribute instance_attrs[256] = {0};
    WGPUVertexBufferLayout vertex_buffers[1 + FLECS_ENGINE_INSTANCE_TYPES_MAX] = {0};
    FlecsEngineImpl *engine = ecs_singleton_get_mut(world, FlecsEngineImpl);
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
                shader_name, batch_name);
            ecs_os_free(shader_name);
            ecs_os_free(batch_name);
            continue;
        }

        const FlecsShaderImpl *shader_impl = flecsEngine_shader_ensureImpl(world, rb[i].shader);
        if (!shader_impl || !shader_impl->shader_module) {
            continue;
        }

        // Setup vertex attributes
        WGPUVertexAttribute vertex_attrs[16];
        int32_t vertex_attr_count = flecsEngine_vertexAttrFromType(
            world, rb[i].vertex_type, vertex_attrs, 16, 0);
        if (vertex_attr_count == -1) {
            continue;
        }

        WGPUVertexBufferLayout vertex_layout = {
            .arrayStride = flecsEngine_type_sizeof(world, rb[i].vertex_type),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = vertex_attr_count,
            .attributes = vertex_attrs
        };

        vertex_buffers[0] = vertex_layout;
        vertex_buffer_count ++;

        // Setup instance data bindings
        vertex_buffer_count = flecsEngine_renderBatch_setupInstanceBindings(world, &rb[i], 
            vertex_attr_count, vertex_buffers, vertex_buffer_count, 
            instance_attrs);

        bool use_material_buffer = flecsEngine_renderBatch_usesMaterialId(&rb[i]);
        impl.uses_material = use_material_buffer;
        impl.uses_ibl = flecsEngine_shader_usesIbl(shader);
        impl.uses_shadow = flecsEngine_shader_usesShadow(shader);
        impl.uses_cluster = flecsEngine_shader_usesCluster(shader);
        impl.uses_textures = flecsEngine_shader_usesTextures(shader);
        bool is_skybox = ecs_has(world, e, FlecsSkyboxBatch);
        bool is_transparent = ecs_has(world, e, FlecsTransparentBatch);

        if ((impl.uses_ibl || impl.uses_shadow || impl.uses_cluster) &&
            !flecsEngine_ibl_ensureBindLayout(engine))
        {
            char *batch_name = ecs_get_path(world, e);
            ecs_err("failed to create render batch '%s': "
                "scene bind layout is not available", batch_name);
            ecs_os_free(batch_name);
            flecsEngine_renderBatch_releaseImpl(&impl);
            continue;
        }

        // Setup uniform + optional storage buffer bindings
        if (!flecsEngine_renderBatch_setupBindings(world, engine, 
            rb[i].uniforms,
            WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            use_material_buffer,
            &impl))
        {
            flecsEngine_renderBatch_releaseImpl(&impl);
            continue;
        }

        WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);

        uint32_t sample_count = engine->sample_count > 1
            ? (uint32_t)engine->sample_count : 1;

        impl.pipeline_hdr = flecsEngine_renderBatch_createPipeline(
            engine,
            shader,
            shader_impl,
            impl.bind_layout,
            impl.uses_ibl,
            impl.uses_shadow,
            impl.uses_cluster,
            impl.uses_textures,
            is_skybox,
            is_transparent,
            vertex_buffers,
            (uint32_t)vertex_buffer_count,
            hdr_format,
            sample_count);
        if (!impl.pipeline_hdr) {
            flecsEngine_renderBatch_releaseImpl(&impl);
            continue;
        }

        if (impl.uses_shadow && engine->shadow.pass_bind_layout
            && !is_transparent)
        {
            if (impl.uses_textures &&
                rb[i].vertex_type == ecs_id(FlecsLitVertexUv))
            {
                /* For textured batches, build the shadow pipeline with
                 * the non-UV vertex layout so that instance transform
                 * locations match the shadow shader expectations. */
                WGPUVertexAttribute shadow_vert_attrs[16];
                WGPUVertexAttribute shadow_inst_attrs[256] = {0};
                WGPUVertexBufferLayout shadow_vbufs[1 + FLECS_ENGINE_INSTANCE_TYPES_MAX] = {0};

                int32_t sv_count = flecsEngine_vertexAttrFromType(
                    world, ecs_id(FlecsLitVertex), shadow_vert_attrs, 16, 0);

                shadow_vbufs[0] = (WGPUVertexBufferLayout){
                    .arrayStride = flecsEngine_type_sizeof(world, ecs_id(FlecsLitVertex)),
                    .stepMode = WGPUVertexStepMode_Vertex,
                    .attributeCount = sv_count,
                    .attributes = shadow_vert_attrs
                };

                int32_t shadow_vbuf_count = flecsEngine_renderBatch_setupInstanceBindings(
                    world, &rb[i], sv_count,
                    shadow_vbufs, 1, shadow_inst_attrs);

                impl.pipeline_shadow = flecsEngine_renderBatch_createShadowPipeline(
                    engine,
                    shadow_vbufs,
                    (uint32_t)shadow_vbuf_count);
            } else {
                impl.pipeline_shadow = flecsEngine_renderBatch_createShadowPipeline(
                    engine,
                    vertex_buffers,
                    (uint32_t)vertex_buffer_count);
            }
        }

        ecs_set_ptr(world, e, FlecsRenderBatchImpl, &impl);
    }
}

void flecsEngine_renderBatch_setupCamera(
    const ecs_world_t *world,
    FlecsUniform *uniforms,
    ecs_entity_t entity)
{
    uniforms->camera_pos[0] = 0.0f;
    uniforms->camera_pos[1] = 0.0f;
    uniforms->camera_pos[2] = 0.0f;
    uniforms->camera_pos[3] = 1.0f;

    glm_mat4_identity(uniforms->mvp);
    glm_mat4_identity(uniforms->inv_vp);
    const FlecsCameraImpl *camera = ecs_get(
        world, entity, FlecsCameraImpl);
    if (!camera) {
        char *cam_name = ecs_get_path(world, entity);
        ecs_err("invalid camera '%s' in view", cam_name);
        ecs_os_free(cam_name);
        return;
    }

    glm_mat4_copy((vec4*)camera->mvp, uniforms->mvp);
    glm_mat4_inv(uniforms->mvp, uniforms->inv_vp);

    const FlecsWorldTransform3 *camera_transform = ecs_get(
        world, entity, FlecsWorldTransform3);
    if (camera_transform) {
        uniforms->camera_pos[0] = camera_transform->m[3][0];
        uniforms->camera_pos[1] = camera_transform->m[3][1];
        uniforms->camera_pos[2] = camera_transform->m[3][2];
    }
}

void flecsEngine_renderBatch_setupLight(
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

    vec3 ray_dir;
    if (!flecsEngine_lightDirFromRotation(rotation, ray_dir)) {
        char *light_name = ecs_get_path(world, entity);
        ecs_err(
            "directional light '%s' has invalid Rotation3", light_name);
        ecs_os_free(light_name);
        return;
    }

    uniforms->light_ray_dir[0] = ray_dir[0];
    uniforms->light_ray_dir[1] = ray_dir[1];
    uniforms->light_ray_dir[2] = ray_dir[2];

    FlecsRgba rgb = {255, 255, 255, 255};
    const FlecsRgba *light_rgb = ecs_get(world, entity, FlecsRgba);
    if (light_rgb) {
        rgb = *light_rgb;
    }

    uniforms->light_color[0] =
        flecsEngine_colorChannelToFloat(rgb.r) * light->intensity;
    uniforms->light_color[1] =
        flecsEngine_colorChannelToFloat(rgb.g) * light->intensity;
    uniforms->light_color[2] =
        flecsEngine_colorChannelToFloat(rgb.b) * light->intensity;
}

static bool flecsEngine_renderBatch_ensureMaterialBindings(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    FlecsRenderBatchImpl *impl)
{
    if (!engine->materials.buffer || !engine->materials.count) {
        return false;
    }

    uint64_t material_buffer_size =
        (uint64_t)engine->materials.count * sizeof(FlecsGpuMaterial);

    if (impl->bind_group_materials &&
        impl->material_buffer_size == material_buffer_size)
    {
        return true;
    }

    if (impl->bind_group_materials) {
        wgpuBindGroupRelease(impl->bind_group_materials);
        impl->bind_group_materials = NULL;
    }

    WGPUBindGroupEntry entries[FLECS_ENGINE_UNIFORMS_MAX + 1] = {0};
    for (uint8_t i = 0; i < impl->uniform_count; i ++) {
        entries[i] = (WGPUBindGroupEntry){
            .binding = i,
            .buffer = impl->uniform_buffers[i],
            .size = flecsEngine_type_sizeof(world, batch->uniforms[i])
        };
    }

    uint32_t storage_binding = impl->uniform_count;
    entries[storage_binding] = (WGPUBindGroupEntry){
        .binding = storage_binding,
        .buffer = engine->materials.buffer,
        .size = material_buffer_size
    };

    WGPUBindGroupDescriptor bind_group_desc = {
        .entries = entries,
        .entryCount = storage_binding + 1,
        .layout = impl->bind_layout
    };

    impl->bind_group_materials = wgpuDeviceCreateBindGroup(
        engine->device, &bind_group_desc);
    if (!impl->bind_group_materials) {
        impl->material_buffer_size = 0;
        return false;
    }

    impl->material_buffer_size = material_buffer_size;
    return true;
}

static void flecsEngine_renderBatch_updateUniforms(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    const FlecsRenderBatchImpl *impl)
{
    FlecsUniform uniforms = {0};
    uniforms.camera_pos[3] = 1.0f;

    if (view->camera) {
        flecsEngine_renderBatch_setupCamera(world, &uniforms, view->camera);
    }

    if (view->light) {
        flecsEngine_renderBatch_setupLight(world, &uniforms, view->light);
    }

    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        glm_mat4_copy((vec4*)engine->shadow.current_light_vp[i], uniforms.light_vp[i]);
    }

    memcpy(uniforms.cascade_splits, engine->shadow.cascade_splits,
        sizeof(float) * FLECS_ENGINE_SHADOW_CASCADE_COUNT);

    float bias = view->shadow.bias;
    if (bias <= 0) { bias = 0.0005f; }
    uniforms.shadow_info[1] = bias;

    /* Per-cascade scale factors: the fraction of the texture each cascade
     * actually uses. Distant cascades render at reduced resolution. */
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        uniforms.shadow_cascade_scales[i] = engine->shadow.map_size > 0
            ? (float)engine->shadow.cascade_sizes[i] /
              (float)engine->shadow.map_size
            : 1.0f;
    }

    uniforms.ambient_light[0] = flecsEngine_colorChannelToFloat(view->ambient_light.r);
    uniforms.ambient_light[1] = flecsEngine_colorChannelToFloat(view->ambient_light.g);
    uniforms.ambient_light[2] = flecsEngine_colorChannelToFloat(view->ambient_light.b);
    uniforms.ambient_light[3] = flecsEngine_colorChannelToFloat(view->ambient_light.a);

    uniforms.sky_color[0] = flecsEngine_colorChannelToFloat(view->background.sky_color.r);
    uniforms.sky_color[1] = flecsEngine_colorChannelToFloat(view->background.sky_color.g);
    uniforms.sky_color[2] = flecsEngine_colorChannelToFloat(view->background.sky_color.b);
    uniforms.sky_color[3] = flecsEngine_colorChannelToFloat(view->background.sky_color.a);

    engine->camera_pos[0] = uniforms.camera_pos[0];
    engine->camera_pos[1] = uniforms.camera_pos[1];
    engine->camera_pos[2] = uniforms.camera_pos[2];

    wgpuQueueWriteBuffer(
        engine->queue,
        impl->uniform_buffers[0],
        0,
        &uniforms,
        sizeof(FlecsUniform));
}

void flecsEngine_renderBatch_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    ecs_entity_t batch_entity)
{
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    FlecsRenderBatchImpl *impl = ecs_get_mut(
        world, batch_entity, FlecsRenderBatchImpl);
    if (!batch || !impl) {
        return;
    }

    WGPURenderPipeline pipeline = impl->pipeline_hdr;
    ecs_assert(pipeline != NULL, ECS_INTERNAL_ERROR, NULL);

    if (pipeline != engine->last_pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        engine->last_pipeline = pipeline;
        flecsEngine_renderBatch_updateUniforms(world, engine, view, impl);
    }

    WGPUBindGroup bind_group = impl->bind_group;
    if (impl->uses_material) {
        if (!flecsEngine_renderBatch_ensureMaterialBindings(
            world, engine, batch, impl))
        {
            return;
        }

        bind_group = impl->bind_group_materials;
    }

    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);

    if (impl->uses_ibl || impl->uses_shadow || impl->uses_cluster) {
        bool is_skybox = ecs_has(world, batch_entity, FlecsSkyboxBatch);
        ecs_entity_t hdri = view->hdri;
        if (!hdri) {
            hdri = engine->sky_background_hdri;
        }
        if (!view->background.ibl && !is_skybox) {
            hdri = engine->black_hdri;
        }

        FlecsHdriImpl *ibl = ecs_get_mut(world, hdri, FlecsHdriImpl);
        ecs_assert(ibl != NULL, ECS_INTERNAL_ERROR, NULL);

        /* Recreate combined bind group if scene resources changed */
        if (ibl->scene_bind_version != engine->scene_bind_version) {
            flecsEngine_ibl_createRuntimeBindGroup(engine, ibl);
        }

        wgpuRenderPassEncoderSetBindGroup(
            pass, 1, ibl->ibl_shadow_bind_group, 0, NULL);
    }

    batch->callback(world, engine, pass, batch);
}

void flecsEngine_renderBatch_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t batch_entity)
{
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    if (!batch || !batch->extract_callback) {
        return;
    }

    batch->extract_callback(world, engine, batch);
}

void flecsEngine_renderBatch_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    ecs_entity_t batch_entity)
{
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    const FlecsRenderBatchImpl *impl = ecs_get(
        world, batch_entity, FlecsRenderBatchImpl);
    if (!batch || !impl || !impl->pipeline_shadow || !impl->uses_shadow) {
        return;
    }

    WGPURenderPipeline pipeline = impl->pipeline_shadow;

    if (pipeline != engine->last_pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        engine->last_pipeline = pipeline;
    }

    wgpuRenderPassEncoderSetBindGroup(
        pass, 0, engine->shadow.pass_bind_groups[engine->shadow.current_cascade],
        0, NULL);

    batch->callback(world, engine, pass, batch);
}

void flecsEngine_renderBatch_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatch);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchImpl);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchSet);
    ECS_TAG_DEFINE(world, FlecsSkyboxBatch);
    ECS_TAG_DEFINE(world, FlecsTransparentBatch);

    ecs_set_hooks(world, FlecsRenderBatch, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderBatch),
        .dtor = ecs_dtor(FlecsRenderBatch),
        .on_set = FlecsRenderBatch_on_set
    });

    ecs_set_hooks(world, FlecsRenderBatchImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderBatchImpl),
        .dtor = ecs_dtor(FlecsRenderBatchImpl)
    });

    ecs_set_hooks(world, FlecsRenderBatchSet, {
        .ctor = ecs_ctor(FlecsRenderBatchSet),
        .move = ecs_move(FlecsRenderBatchSet),
        .copy = ecs_copy(FlecsRenderBatchSet),
        .dtor = ecs_dtor(FlecsRenderBatchSet)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderBatchSet),
        .members = {
            { .name = "batches", .type = flecsEngine_vecEntity(world) }
        }
    });
}
