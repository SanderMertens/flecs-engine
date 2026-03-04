#include "renderer.h"

#define FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL_IMPL
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderEffect);
ECS_COMPONENT_DECLARE(FlecsVertex);
ECS_COMPONENT_DECLARE(FlecsLitVertex);
ECS_COMPONENT_DECLARE(FlecsInstanceTransform);
ECS_COMPONENT_DECLARE(FlecsInstanceColor);
ECS_COMPONENT_DECLARE(FlecsInstancePbrMaterial);
ECS_COMPONENT_DECLARE(FlecsInstanceMaterialId);
ECS_COMPONENT_DECLARE(FlecsUniform);

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

    ECS_COMPONENT_DEFINE(world, FlecsVertex);
    ECS_COMPONENT_DEFINE(world, FlecsLitVertex);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceTransform);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceColor);
    ECS_COMPONENT_DEFINE(world, FlecsInstancePbrMaterial);
    ECS_COMPONENT_DEFINE(world, FlecsInstanceMaterialId);
    ECS_COMPONENT_DEFINE(world, FlecsUniform);

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
        .entity = ecs_id(FlecsShader),
        .members = {
            { .name = "source", .type = ecs_id(ecs_string_t) },
            { .name = "vertex_entry", .type = ecs_id(ecs_string_t) },
            { .name = "fragment_entry", .type = ecs_id(ecs_string_t) }
        }
    });

    flecsEngine_shader_register(world);
    flecsEngine_renderBatch_register(world);
    flecsEngine_renderEffect_register(world);
    flecsEngine_renderView_register(world);
    flecsEngine_ibl_register(world);
    flecsEngine_tonyMcMapFace_register(world);
    flecsEngine_bloom_register(world);
}
