#include "flecs_engine.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    .shadow.enabled = true,
    .shadow.pcf_samples = 3
  };

  FlecsRenderBatchSet batch_set = {};

  if (options.frame_output_mode) {
    ecs_singleton_set(world, FlecsFrameOutput, {
      .width = options.width,
      .height = options.height,
      .path = options.frame_output_path,
      .clear_color = {0, 0, 0}
    });
  } else {
    ecs_singleton_set(world, FlecsWindow, {
      .title = "Hello World",
      .width = options.width,
      .height = options.height,
      .clear_color = {0, 0, 0}
    });
  }

  // Camera
  view.camera = ecs_entity(world, { .name = "camera" });
  ecs_set(world, view.camera, FlecsCamera, {
      .fov = glm_rad(60.0f),
      .near_ = 1.0f,
      .far_ = 1000.0f,
      .aspect_ratio = options.width / (float)options.height
  });
  ecs_add(world, view.camera, FlecsCameraController);
  ecs_set(world, view.camera, FlecsPosition3, {0, 10, 10});
  ecs_set(world, view.camera, FlecsLookAt, {0, 8, 0});

  // Light
  view.light = ecs_entity(world, { .name = "light" });
  ecs_set(world, view.light, FlecsPosition3, {8, 5, 5});
  ecs_set(world, view.light, FlecsDirectionalLight, { .intensity = 1.0f });
  ecs_set(world, view.light, FlecsLookAt, { 0, 0, 0 });
  ecs_set(world, view.light, FlecsRgba, {255, 255, 255, 255});

  // HDRI (optional, for image based lighting)
  view.hdri = flecsEngine_createHdri(
    world, view_entity, "hdri", "industrial_sunset_puresky_4k.exr", 1024, 64);

  // RenderBatches (what to render in scene)
  ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
    flecsEngine_createBatch_skybox(world, view_entity, "skybox");
  ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
    flecsEngine_createBatchSet_geometry(world, view_entity, "geometry");

  // Post process effects
  FlecsSSAO ssao_settings = flecsEngine_ssaoSettingsDefault();
  ssao_settings.radius = 1.0;
  ssao_settings.blur = 2;
  FlecsBloom bloom_settings = flecsEngine_bloomSettingsDefault();
  FlecsExponentialHeightFog fog_settings =
    flecsEngine_exponentialHeightFogSettingsDefault();
  fog_settings.density = 0;

  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){ .enabled = true, .effect =
      flecsEngine_createEffect_ssao(world, view_entity,
        "ssaoEffect", 0, &ssao_settings) };
  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){ .enabled = true, .effect =
      flecsEngine_createEffect_bloom(world, view_entity,
        "bloomEffect", 1, &bloom_settings) };
  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){ .enabled = true, .effect =
      flecsEngine_createEffect_exponentialHeightFog(
        world, view_entity, "heightFogEffect", 2, &fog_settings) };
  *ecs_vec_append_t(NULL, &view.effects, flecs_render_view_effect_t) =
    (flecs_render_view_effect_t){ .enabled = true, .effect =
      flecsEngine_createEffect_tonyMcMapFace(world, view_entity,
        "tonyMcMapFaceEffect", 3) };

  ecs_set_ptr(world, view_entity, FlecsRenderView, &view);
  ecs_set_ptr(world, view_entity, FlecsRenderBatchSet, &batch_set);
}

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
  ECS_IMPORT(world, FlecsStats);
  ECS_IMPORT(world, FlecsScriptMath);
  ECS_IMPORT(world, FlecsEngine);

  initEngine(world, options);

  ecs_entity_t s = ecs_script(world, {
    .filename = "city.flecs"
    // .filename = "museum.flecs"
    // .filename = "cube.flecs"
    // .filename = "empty.flecs"
  });
  if (!s) {
    printf("failed to load museum script\n");
  }

  ecs_singleton_set(world, EcsRest, {0});

  double i = 0;
  while (ecs_progress(world, 0)) {
    i -= 0.005;
  }

  return ecs_fini(world);
}
