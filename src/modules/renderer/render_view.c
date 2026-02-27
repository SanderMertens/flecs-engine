#include "renderer.h"
#include "flecs_engine.h"

static const int32_t kRenderViewLogFrameLimit = 60;
static int32_t g_render_view_log_frames = 0;

static bool flecsRenderViewLogEnabled(void) {
    return g_render_view_log_frames < kRenderViewLogFrameLimit;
}

void flecsEngineRenderView(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view)
{
    int32_t i, count = ecs_vec_count(&view->batches);
    ecs_entity_t *batches = ecs_vec_first(&view->batches);
    if (flecsRenderViewLogEnabled()) {
        ecs_dbg("[view] camera=%llu batches=%d",
            (unsigned long long)view->camera, count);
    }
    for (i = 0; i < count; i ++) {
        const FlecsRenderBatch *batch = ecs_get(
            world, batches[i], FlecsRenderBatch);
        const FlecsRenderBatchImpl *batch_impl = ecs_get(
            world, batches[i], FlecsRenderBatchImpl);
        if (batch) {
            if (flecsRenderViewLogEnabled()) {
                char *batch_name = ecs_get_path(world, batches[i]);
                ecs_dbg("[view] render batch=%s has_impl=%d",
                    batch_name ? batch_name : "<unnamed>", batch_impl != NULL);
                ecs_os_free(batch_name);
            }
            flecsEngineRenderBatch(
                world, engine, pass, view, batch, batch_impl);
        }
    }

    if (flecsRenderViewLogEnabled()) {
        g_render_view_log_frames ++;
    }
}
