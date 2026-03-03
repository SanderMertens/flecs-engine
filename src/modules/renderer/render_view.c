#include "renderer.h"
#include "flecs_engine.h"

static void flecsEngineRenderBatchSet(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    const FlecsRenderBatchSet *batch_set,
    WGPUTextureFormat color_format)
{
    int32_t i, count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested_batch_set = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested_batch_set) {
            flecsEngineRenderBatchSet(
                world,
                engine,
                pass,
                view,
                nested_batch_set,
                color_format);
            continue;
        }

        flecsEngineRenderBatch(
            world, engine, pass, view, batch_entity, color_format);
    }
}

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

    flecsEngineRenderBatchSet(
        world,
        engine,
        pass,
        view,
        batch_set,
        color_format);
}
