#include "flecs_engine.h"

int main(void) {
  ecs_world_t *world = ecs_init();
  ecs_log_set_level(-1);

  ECS_IMPORT(world, FlecsEngine);

  ecs_singleton_set(world, FlecsWindow, { .title = "Hello World" });

  ecs_entity_t camera = ecs_new(world);
  ecs_set(world, camera, FlecsCamera, {
      .fov = glm_rad(60.0f),
      .near_ = 0.1f,
      .far_ = 100.0f,
      .aspect_ratio = 1280.0f / 800.0f
  });

  ecs_entity_t view = ecs_new(world);
  FlecsRenderView *s = ecs_ensure(world, view, FlecsRenderView);
  ecs_vec_append_t(NULL, &s->batches, ecs_entity_t)[0] = flecsEngine_createBatch_litColoredBoxes(world);
  ecs_vec_append_t(NULL, &s->batches, ecs_entity_t)[0] = flecsEngine_createBatch_litColoredGeometry(world);
  s->camera = camera;
  ecs_modified(world, view, FlecsRenderView);

  for (int x = 0; x < 100; x ++) {
    for (int y = 0; y < 50; y ++) {
      ecs_entity_t box = ecs_new(world);
      ecs_set(world, box, FlecsBox, {1.0f, 3 * (rand() / (float)RAND_MAX), 1.0f});
      ecs_set(world, box, FlecsPosition3, {(x - 50) * 2, -2, -10 - y * 2});
      ecs_set(world, box, FlecsRotation3, {0, 0, 0});
      ecs_set(world, box, FlecsRgba, {0, 64 * ((50 - y) / 50.0), 64, 255});
    }
  }

  return ecs_app_run(world, &(ecs_app_desc_t) {
    .enable_rest = true,
    .enable_stats = true
  });
}
