#include "renderer.h"
#include "flecs_engine.h"

void flecsEngineRenderView(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    WGPUTextureFormat color_format)
{
    int32_t i, count = ecs_vec_count(&view->batches);
    ecs_entity_t *batches = ecs_vec_first(&view->batches);
    
    for (i = 0; i < count; i ++) {
        flecsEngineRenderBatch(
            world, engine, pass, view, batches[i], color_format);
    }
}
