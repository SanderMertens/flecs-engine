#include "flecs_engine.h"

    // const char *shader;
    // ecs_query_t *query;
    // ecs_entity_t vertex_type;
    // ecs_entity_t instance_type;
    // ecs_entity_t vs_uniforms[FLECS_ENGINE_UNIFORMS_MAX];
    // ecs_entity_t fs_uniforms[FLECS_ENGINE_UNIFORMS_MAX];



int main(void) {
  ecs_world_t *world = ecs_init();

  ECS_IMPORT(world, FlecsEngine);

  ecs_singleton_set(world, FlecsWindow, { .title = "Hello World" });

  ecs_entity_t camera = ecs_new(world);
  ecs_set(world, camera, FlecsCamera, {
      .fov = 30,
      .near_ = -100,
      .far_ = 100,
      .aspect_ratio = 1
  });

  ecs_entity_t view = ecs_new(world);
  FlecsRenderView *s = ecs_ensure(world, view, FlecsRenderView);
  ecs_vec_append_t(NULL, &s->batches, ecs_entity_t)[0] = flecsEngine_createBatch_litColoredGeometry(world);
//   s->camera = camera;
  ecs_modified(world, view, FlecsRenderView);

  ecs_entity_t cube = ecs_new(world);
  ecs_set(world, cube, FlecsCube, { 1 });
  ecs_set(world, cube, FlecsPosition3, {0, 0, 0});
  ecs_set(world, cube, FlecsRgba, {255, 0, 0});

  return ecs_app_run(world, &(ecs_app_desc_t) {
    .enable_rest = true,
    .enable_stats = true
  });
}
