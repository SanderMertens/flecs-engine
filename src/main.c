#include "flecs_engine.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

typedef struct {
  bool frame_output_mode;
  const char *frame_output_path;
  int32_t width;
  int32_t height;
} FlecsAppOptions;

static void flecsPrintUsage(
  const char *argv0)
{
  printf(
    "Usage: %s [--frame-out <file.ppm>] [--width <px>] [--height <px>] [--size <WxH>]\n"
    "\n"
    "  --frame-out <path>  Render one frame to a PPM image, then quit.\n"
    "  --width <px>        Output width (default: 1280).\n"
    "  --height <px>       Output height (default: 800).\n"
    "  --size <WxH>        Set width and height together.\n"
    "  -h, --help          Show this help.\n",
    argv0);
}

static bool flecsParsePositiveI32(
  const char *arg,
  int32_t *out)
{
  char *end = NULL;
  long value = strtol(arg, &end, 10);
  if (end == arg || *end != '\0' || value <= 0 || value > INT_MAX) {
    return false;
  }

  *out = (int32_t)value;
  return true;
}

static bool flecsParseSize(
  const char *arg,
  int32_t *width,
  int32_t *height)
{
  int32_t w = 0;
  int32_t h = 0;
  char tail = '\0';

  if (sscanf(arg, "%dx%d%c", &w, &h, &tail) != 2 || w <= 0 || h <= 0) {
    return false;
  }

  *width = w;
  *height = h;
  return true;
}

static int flecsParseArgs(
  int argc,
  char *argv[],
  FlecsAppOptions *options)
{
  for (int i = 1; i < argc; i ++) {
    const char *arg = argv[i];

    if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
      flecsPrintUsage(argv[0]);
      return 1;
    }

    if (!strcmp(arg, "--frame-out")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for --frame-out\n");
        return -1;
      }
      options->frame_output_mode = true;
      options->frame_output_path = argv[++ i];
      continue;
    }

    if (!strcmp(arg, "--width")) {
      if (i + 1 >= argc || !flecsParsePositiveI32(argv[i + 1], &options->width)) {
        fprintf(stderr, "Invalid value for --width\n");
        return -1;
      }
      i ++;
      continue;
    }

    if (!strcmp(arg, "--height")) {
      if (i + 1 >= argc || !flecsParsePositiveI32(argv[i + 1], &options->height)) {
        fprintf(stderr, "Invalid value for --height\n");
        return -1;
      }
      i ++;
      continue;
    }

    if (!strcmp(arg, "--size")) {
      if (i + 1 >= argc || !flecsParseSize(argv[i + 1], &options->width, &options->height)) {
        fprintf(stderr, "Invalid value for --size, expected WxH\n");
        return -1;
      }
      i ++;
      continue;
    }

    fprintf(stderr, "Unknown argument: %s\n", arg);
    return -1;
  }

  return 0;
}

void initEngine(
  ecs_world_t *world, 
  FlecsAppOptions options) 
{
  ecs_entity_t view_entity =  ecs_entity(world, { .name = "view" });
  FlecsRenderView view = {
    .shadow = {
      .enabled = true,
      .map_size = 4096,
      .max_range = 150
    },
    .ambient_light = {0, 0, 0, 255},
    .background = {
      .sky_color = {5, 45, 100},
      .haze_color = {250, 130, 0},
      .horizon_color = {250, 255, 255},
      .ground_color = {50, 50, 50},
      .ambient_intensity = 0.2
    }
  };

  FlecsRenderBatchSet batch_set = {};

  ecs_entity_t window = ecs_entity(world, { .name = "window" });

  if (options.frame_output_mode) {
    ecs_set(world, window, FlecsFrameOutput, {
      .width = options.width,
      .height = options.height,
      .path = options.frame_output_path,
    });
  } else {
    ecs_set(world, window, FlecsWindow, {
      .title = "Hello World",
      .width = options.width,
      .height = options.height,
      .resolution_scale = 1,
      .vsync = true,
      .msaa = false
    });
  }

  // Camera
  view.camera = ecs_entity(world, { .name = "camera" });
  ecs_set(world, view.camera, FlecsCamera, {
      .fov = glm_rad(60.0f),
      .near_ = 0.2f,
      .far_ = 1000.0f,
      .aspect_ratio = options.width / (float)options.height
  });
  ecs_add(world, view.camera, FlecsCameraController);
  ecs_set(world, view.camera, FlecsPosition3, {-20.38, 2.023, 16.23});
  ecs_set(world, view.camera, FlecsLookAt, {-19.46, 2, 15.842});

  // Light
  view.light = ecs_entity(world, { .name = "light" });
  ecs_set(world, view.light, FlecsPosition3, {1, 1, 1});
  ecs_set(world, view.light, FlecsDirectionalLight, { .intensity = 2.0f });
  ecs_set(world, view.light, FlecsLookAt, { 0, 0, 0 });
  ecs_set(world, view.light, FlecsRgba, {255, 160, 100, 255});

  // HDRI (optional, for image based lighting)
  view.hdri = flecsEngine_createHdri(
    world, view_entity, "hdri", "etc/assets/hdri/industrial_sunset_puresky_4k.exr", 1024, 64);

  // RenderBatches (what to render in scene)
  ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
    flecsEngine_createBatch_skybox(world, view_entity, "skybox");
  ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
    flecsEngine_createBatch_infinitePlane(world, view_entity, "ground_plane");
  ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
    flecsEngine_createBatchSet_geometry(world, view_entity, "geometry");

  // Post process effects
  FlecsSSAO ssao_settings = flecsEngine_ssaoSettingsDefault();
  ssao_settings.radius = 0.5;
  ssao_settings.blur = 0;
  FlecsBloom bloom_settings = flecsEngine_bloomSettingsDefault();
  FlecsExponentialHeightFog fog_settings =
    flecsEngine_exponentialHeightFogSettingsDefault();
  fog_settings.density = 0.3;
  fog_settings.falloff = 1.3;

  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){ .enabled = true, .effect =
      flecsEngine_createEffect_ssao(world, view_entity,
        "ssao", 0, &ssao_settings) };
  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){ .enabled = true, .effect =
      flecsEngine_createEffect_bloom(world, view_entity,
        "bloom", 1, &bloom_settings) };
  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){ .enabled = false, .effect =
      flecsEngine_createEffect_exponentialHeightFog(
        world, view_entity, "heightFog", 2, &fog_settings) };
  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){ .enabled = true, .effect =
      flecsEngine_createEffect_tonyMcMapFace(world, view_entity,
        "tonyMcMapFace", 3) };

  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){
#ifdef __EMSCRIPTEN__
      .enabled = true,
#endif
      .effect = flecsEngine_createEffect_gammaCorrect(world, view_entity,
        "gammaCorrect", 4) };

  ecs_set_ptr(world, view_entity, FlecsRenderView, &view);
  ecs_set_ptr(world, view_entity, FlecsRenderBatchSet, &batch_set);
}

#ifdef __EMSCRIPTEN__
static void flecsWasmFrame(void *arg) {
  ecs_world_t *world = arg;
  if (!ecs_progress(world, 0)) {
    emscripten_cancel_main_loop();
  }
}
#endif

int main(
  int argc,
  char *argv[])
{
  FlecsAppOptions options = {
    .frame_output_mode = false,
    .frame_output_path = NULL,
    .width = 1280,
    .height = 800
  };

  int parse_result = flecsParseArgs(argc, argv, &options);
  if (parse_result != 0) {
    return parse_result > 0 ? 0 : 1;
  }

  ecs_world_t *world = ecs_init();
#ifndef __EMSCRIPTEN__
  ECS_IMPORT(world, FlecsStats);
#endif
  ECS_IMPORT(world, FlecsScriptMath);
  ECS_IMPORT(world, FlecsEngine);

  initEngine(world, options);

  ecs_log_set_level(0);

  ecs_entity_t s = ecs_script(world, {
    .filename = "etc/assets/scenes/kenney_city.flecs"
    // .filename = "etc/assets/scenes/bistro.flecs"
    // .filename = "etc/assets/scenes/sponza.flecs"
    // .filename = "etc/assets/scenes/a_beautiful_game.flecs"
    // .filename = "etc/assets/scenes/flight_helmet.flecs"
    // .filename = "etc/assets/scenes/damaged_helmet.flecs"
    // .filename = "etc/assets/scenes/city.flecs"
    // .filename = "etc/assets/scenes/museum.flecs"
    // .filename = "etc/assets/scenes/cube.flecs"
    // .filename = "etc/assets/scenes/empty.flecs"
  });
  if (!s) {
    ecs_err("failed to load script\n");
  }

#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop_arg(
      (em_arg_callback_func)flecsWasmFrame, world, 0, 1);
#else
  ecs_singleton_set(world, EcsRest, {0});
  while (ecs_progress(world, 0)) {}
#endif

  ecs_log_set_level(-1);

  return ecs_fini(world);
}
