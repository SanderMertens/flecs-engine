#include "renderer.h"
#include "flecs_engine.h"

void flecsEngineRenderView(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view)
{
    int32_t i, count = ecs_vec_count(&view->batches);
    ecs_entity_t *batches = ecs_vec_first(&view->batches);
    for (i = 0; i < count; i ++) {
        const FlecsRenderBatch *batch = ecs_get(
            world, batches[i], FlecsRenderBatch);
        const FlecsRenderBatchImpl *batch_impl = ecs_get(
            world, batches[i], FlecsRenderBatchImpl);
        if (batch) {
            flecsEngineRenderBatch(
                world, engine, pass, view, batch, batch_impl);
        }
    }
}
