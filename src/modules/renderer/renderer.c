#include "renderer.h"

#define FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL_IMPL
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);
ECS_COMPONENT_DECLARE(FlecsRenderBatch);
ECS_COMPONENT_DECLARE(FlecsRenderView);
ECS_COMPONENT_DECLARE(FlecsVertex);
ECS_COMPONENT_DECLARE(FlecsLitVertex);
ECS_COMPONENT_DECLARE(FlecsInstanceTransform);
ECS_COMPONENT_DECLARE(FlecsInstanceColor);
ECS_COMPONENT_DECLARE(FlecsUniform);
ECS_COMPONENT_DECLARE(FlecsShader);
ECS_COMPONENT_DECLARE(FlecsShaderImpl);

ECS_CTOR(FlecsRenderView, ptr, {
    ecs_vec_init_t(NULL, &ptr->batches, ecs_entity_t, 0);
    ptr->camera = 0;
})

ECS_MOVE(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    dst->batches = ecs_vec_copy_t(NULL, &src->batches, ecs_entity_t);
})

ECS_DTOR(FlecsRenderView, ptr, {
    ecs_vec_fini_t(NULL, &ptr->batches, ecs_entity_t);
})

void flecsEngineRenderViews(
    const ecs_world_t *world,
    const FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    // Color attachment: clear to background and render into swapchain.
    WGPURenderPassColorAttachment color_attachment = {
        .view = view_texture,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){ 0.05, 0.05, 0.08, 1.0 }
    };

    // Depth attachment: clear depth each frame.
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

    // Render pass: bind color and depth attachments.
    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment,
        .depthStencilAttachment = &depth_attachment
    };

    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);

    ecs_iter_t it = ecs_query_iter(world, impl->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngineRenderView(world, impl, pass, &views[i]);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
}

void FlecsEngineRendererImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineRenderer);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsRenderBatch);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchImpl);
    ECS_COMPONENT_DEFINE(world, FlecsRenderView);
    ECS_COMPONENT_DEFINE(world, FlecsVertex);
    ECS_COMPONENT_DEFINE(world, FlecsLitVertex);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceTransform);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceColor);
    ECS_COMPONENT_DEFINE(world, FlecsUniform);
    ECS_COMPONENT_DEFINE(world, FlecsShader);
    ECS_COMPONENT_DEFINE(world, FlecsShaderImpl);

    ecs_set_hooks(world, FlecsRenderBatch, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsRenderBatch_on_set
    });

    ecs_set_hooks(world, FlecsRenderBatchImpl, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(FlecsRenderBatchImpl)
    });

    ecs_set_hooks(world, FlecsShader, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsShader_on_set
    });

    ecs_set_hooks(world, FlecsShaderImpl, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(FlecsShaderImpl)
    });

    ecs_set_hooks(world, FlecsRenderView, {
        .ctor = ecs_ctor(FlecsRenderView),
        .move = ecs_move(FlecsRenderView),
        .copy = ecs_copy(FlecsRenderView),
        .dtor = ecs_dtor(FlecsRenderView)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsVertex),
        .members = {
            { .name = "p", .type = ecs_id(flecs_vec3_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsLitVertex),
        .members = {
            { .name = "p", .type = ecs_id(flecs_vec3_t) },
            { .name = "n", .type = ecs_id(flecs_vec3_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsInstanceTransform),
        .members = {
            { .name = "m", .type = ecs_id(flecs_mat4_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsInstanceColor),
        .members = {
            { .name = "c", .type = ecs_id(flecs_rgba_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsUniform),
        .members = {
            { .name = "vp", .type = ecs_id(flecs_mat4_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsShader),
        .members = {
            { .name = "source", .type = ecs_id(ecs_string_t) },
            { .name = "vertex_entry", .type = ecs_id(ecs_string_t) },
            { .name = "fragment_entry", .type = ecs_id(ecs_string_t) }
        }
    });
}
