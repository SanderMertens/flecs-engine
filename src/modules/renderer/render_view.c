#include "renderer.h"
#include "flecs_engine.h"

void flecsEngineRenderView(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    ecs_entity_t view_entity,
    const FlecsRenderView *view,
    WGPUTextureFormat color_format)
{
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    if (!batch_set) {
        return;
    }

    int32_t i, count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);
    
    for (i = 0; i < count; i ++) {
        flecsEngineRenderBatch(
            world, engine, pass, view, batches[i], color_format);
    }
}
