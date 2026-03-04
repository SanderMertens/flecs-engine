#include "renderer.h"

#define FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL_IMPL
#include "flecs_engine.h"

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

    flecsEngine_renderBatch_register(world);
    flecsEngine_renderEffect_register(world);
    flecsEngine_renderView_register(world);
    flecsEngine_tonyMcMapFace_register(world);
    flecsEngine_bloom_register(world);
}
