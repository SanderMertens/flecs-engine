#include "renderer.h"

#define FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL_IMPL
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);
ECS_COMPONENT_DECLARE(FlecsRenderEffectImpl);
ECS_COMPONENT_DECLARE(FlecsTonyImpl);
ECS_COMPONENT_DECLARE(FlecsBloom);
ECS_COMPONENT_DECLARE(FlecsBloomImpl);
ECS_COMPONENT_DECLARE(FlecsRenderBatch);
ECS_COMPONENT_DECLARE(FlecsRenderEffect);
ECS_COMPONENT_DECLARE(FlecsRenderBatchSet);
ECS_COMPONENT_DECLARE(FlecsRenderView);
ECS_COMPONENT_DECLARE(FlecsVertex);
ECS_COMPONENT_DECLARE(FlecsLitVertex);
ECS_COMPONENT_DECLARE(FlecsInstanceTransform);
ECS_COMPONENT_DECLARE(FlecsInstanceColor);
ECS_COMPONENT_DECLARE(FlecsInstancePbrMaterial);
ECS_COMPONENT_DECLARE(FlecsInstanceMaterialId);
ECS_COMPONENT_DECLARE(FlecsUniform);
ECS_COMPONENT_DECLARE(FlecsShader);
ECS_COMPONENT_DECLARE(FlecsShaderImpl);

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

ECS_DTOR(FlecsRenderEffect, ptr, {
    if (ptr->ctx && ptr->free_ctx) {
        ptr->free_ctx(ptr->ctx);
    }
})

ECS_MOVE(FlecsRenderEffect, dst, src, {
    if (dst->ctx && dst->free_ctx) {
        dst->free_ctx(dst->ctx);
    }
    *dst = *src;
    ecs_os_zeromem(src);
})

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

ECS_CTOR(FlecsRenderView, ptr, {
    ecs_vec_init_t(NULL, &ptr->effects, ecs_entity_t, 0);
    ptr->camera = 0;
    ptr->light = 0;
})

ECS_MOVE(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, ecs_entity_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, ecs_entity_t);
    dst->camera = src->camera;
    dst->light = src->light;
    dst->effects = ecs_vec_copy_t(NULL, &src->effects, ecs_entity_t);
})

ECS_DTOR(FlecsRenderView, ptr, {
    ecs_vec_fini_t(NULL, &ptr->effects, ecs_entity_t);
})

static float flecsEngineColorChannelToFloat(
    uint8_t value)
{
    return (float)value / 255.0f;
}

WGPUColor flecsEngineGetClearColor(
    const FlecsEngineImpl *impl)
{
    return (WGPUColor){
        .r = (double)flecsEngineColorChannelToFloat(impl->clear_color.r),
        .g = (double)flecsEngineColorChannelToFloat(impl->clear_color.g),
        .b = (double)flecsEngineColorChannelToFloat(impl->clear_color.b),
        .a = (double)flecsEngineColorChannelToFloat(impl->clear_color.a)
    };
}

void flecsEngineGetClearColorVec4(
    const FlecsEngineImpl *impl,
    float out[4])
{
    out[0] = flecsEngineColorChannelToFloat(impl->clear_color.r);
    out[1] = flecsEngineColorChannelToFloat(impl->clear_color.g);
    out[2] = flecsEngineColorChannelToFloat(impl->clear_color.b);
    out[3] = flecsEngineColorChannelToFloat(impl->clear_color.a);
}

void flecsEngineRenderViews(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    flecsEngineUploadMaterialBuffer(world, impl);

    flecsEngineRenderViewsWithEffects(
        world,
        impl,
        view_texture,
        encoder);
    impl->last_pipeline = NULL;
}

void FlecsEngineRendererImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineRenderer);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsRenderBatch);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchImpl);
    ECS_COMPONENT_DEFINE(world, FlecsRenderEffect);
    ECS_COMPONENT_DEFINE(world, FlecsRenderEffectImpl);
    ECS_COMPONENT_DEFINE(world, FlecsTonyImpl);
    ECS_COMPONENT_DEFINE(world, FlecsBloom);
    ECS_COMPONENT_DEFINE(world, FlecsBloomImpl);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchSet);
    ECS_COMPONENT_DEFINE(world, FlecsRenderView);
    ECS_COMPONENT_DEFINE(world, FlecsVertex);
    ECS_COMPONENT_DEFINE(world, FlecsLitVertex);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceTransform);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceColor);
    ECS_COMPONENT_DEFINE(world, FlecsInstancePbrMaterial);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceMaterialId);
    ECS_COMPONENT_DEFINE(world, FlecsUniform);
    ECS_COMPONENT_DEFINE(world, FlecsShader);
    ECS_COMPONENT_DEFINE(world, FlecsShaderImpl);

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

    ecs_set_hooks(world, FlecsRenderEffect, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderEffect),
        .dtor = ecs_dtor(FlecsRenderEffect),
        .on_set = FlecsRenderEffect_on_set
    });

    ecs_set_hooks(world, FlecsRenderEffectImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderEffectImpl),
        .dtor = ecs_dtor(FlecsRenderEffectImpl)
    });

    ecs_set_hooks(world, FlecsTonyImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsTonyImpl),
        .dtor = ecs_dtor(FlecsTonyImpl)
    });

    ecs_set_hooks(world, FlecsBloomImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsBloomImpl),
        .dtor = ecs_dtor(FlecsBloomImpl)
    });

    ecs_set_hooks(world, FlecsShader, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsShader_on_set
    });

    ecs_set_hooks(world, FlecsShaderImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsShaderImpl),
        .dtor = ecs_dtor(FlecsShaderImpl)
    });

    ecs_set_hooks(world, FlecsRenderBatchSet, {
        .ctor = ecs_ctor(FlecsRenderBatchSet),
        .move = ecs_move(FlecsRenderBatchSet),
        .copy = ecs_copy(FlecsRenderBatchSet),
        .dtor = ecs_dtor(FlecsRenderBatchSet)
    });

    ecs_set_hooks(world, FlecsRenderView, {
        .ctor = ecs_ctor(FlecsRenderView),
        .move = ecs_move(FlecsRenderView),
        .copy = ecs_copy(FlecsRenderView),
        .dtor = ecs_dtor(FlecsRenderView)
    });

    ecs_entity_t entity_vector_t = ecs_vector(world, {
        .type = ecs_id(ecs_entity_t)
    });
    ecs_entity_t bloom_prefilter_t = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "FlecsBloomPrefilter" }),
        .members = {
            { .name = "threshold", .type = ecs_id(ecs_f32_t) },
            { .name = "threshold_softness", .type = ecs_id(ecs_f32_t) }
        }
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
            { .name = "c0", .type = ecs_id(flecs_vec3_t) },
            { .name = "c1", .type = ecs_id(flecs_vec3_t) },
            { .name = "c2", .type = ecs_id(flecs_vec3_t) },
            { .name = "c3", .type = ecs_id(flecs_vec3_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsInstanceColor),
        .members = {
            { .name = "color", .type = ecs_id(flecs_rgba_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsInstancePbrMaterial),
        .members = {
            { .name = "metallic", .type = ecs_id(ecs_f32_t) },
            { .name = "roughness", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsInstanceMaterialId),
        .members = {
            { .name = "value", .type = ecs_id(ecs_u32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsUniform),
        .members = {
            { .name = "mvp", .type = ecs_id(flecs_mat4_t) },
            { .name = "clear_color", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "light_ray_dir", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "light_color", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "camera_pos", .type = ecs_id(ecs_f32_t), .count = 4 },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBloom),
        .members = {
            { .name = "intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "low_frequency_boost", .type = ecs_id(ecs_f32_t) },
            { .name = "low_frequency_boost_curvature", .type = ecs_id(ecs_f32_t) },
            { .name = "high_pass_frequency", .type = ecs_id(ecs_f32_t) },
            { .name = "prefilter", .type = bloom_prefilter_t },
            { .name = "max_mip_dimension", .type = ecs_id(ecs_u32_t) },
            { .name = "scale_x", .type = ecs_id(ecs_f32_t) },
            { .name = "scale_y", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderBatchSet),
        .members = {
            { .name = "batches", .type = entity_vector_t }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderView),
        .members = {
            { .name = "camera", .type = ecs_id(ecs_entity_t) },
            { .name = "light", .type = ecs_id(ecs_entity_t) },
            { .name = "effects", .type = entity_vector_t }
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
