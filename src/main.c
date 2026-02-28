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
  ecs_log_set_level(-1);

  ECS_IMPORT(world, FlecsEngine);

  if (options.frame_output_mode) {
    ecs_singleton_set(world, FlecsFrameOutput, {
      .width = options.width,
      .height = options.height,
      .path = options.frame_output_path
    });
  } else {
    ecs_singleton_set(world, FlecsWindow, {
      .title = "Hello World",
      .width = options.width,
      .height = options.height
    });
  }

  if (ecs_should_quit(world)) {
    return 1;
  }

  ecs_entity_t camera = ecs_new(world);
  ecs_set(world, camera, FlecsCamera, {
      .fov = glm_rad(60.0f),
      .near_ = 0.1f,
      .far_ = 100.0f,
      .aspect_ratio = options.width / (float)options.height
  });

  ecs_entity_t view = ecs_new(world);
  FlecsRenderView *v = ecs_ensure(world, view, FlecsRenderView);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_boxes(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_pyramids(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_quads(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_triangles(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_right_triangles(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_triangle_prisms(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_right_triangle_prisms(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_litColoredGeometry(world);
  v->camera = camera;
  ecs_modified(world, view, FlecsRenderView);

  ecs_entity_t box = ecs_new(world);
  ecs_set(world, box, FlecsBox, {2, 2, 2});
  ecs_set(world, box, FlecsPosition3, {-6, -2, -12});
  ecs_set(world, box, FlecsRgba, {255, 0, 0});

  ecs_entity_t triangle_prism = ecs_new(world);
  ecs_set(world, triangle_prism, FlecsTrianglePrism, {2, 2, 2});
  ecs_set(world, triangle_prism, FlecsPosition3, {-3, -2, -12});
  ecs_set(world, triangle_prism, FlecsRgba, {128, 128, 0});

  ecs_entity_t right_triangle_prism = ecs_new(world);
  ecs_set(world, right_triangle_prism, FlecsRightTrianglePrism, {2, 2, 2});
  ecs_set(world, right_triangle_prism, FlecsPosition3, {0, -2, -12});
  ecs_set(world, right_triangle_prism, FlecsRgba, {0, 128, 0});

  ecs_entity_t pyramid = ecs_new(world);
  ecs_set(world, pyramid, FlecsPyramid, {2, 2, 2});
  ecs_set(world, pyramid, FlecsPosition3, {3, -2, -12});
  ecs_set(world, pyramid, FlecsRgba, {0, 0, 128});

  ecs_entity_t spheres[5];
  for (int i = 0; i < 5; i ++) {
    spheres[i] = ecs_new(world);
    ecs_set(world, spheres[i], FlecsSphere, { .segments = 3 + i, .smooth = false, .radius = 1 });
    ecs_set(world, spheres[i], FlecsPosition3, {-6 + i * 3, 2, -12});
    ecs_set(world, spheres[i], FlecsRgba, {255, 64, 0});
  }

  ecs_entity_t sphere = ecs_new(world);
  ecs_set(world, sphere, FlecsSphere, {.segments = 32, .smooth = true, .radius = 1});
  ecs_set(world, sphere, FlecsPosition3, {6, -2, -12});
  ecs_set(world, sphere, FlecsRgba, {128, 0, 128});

  ecs_entity_t quad = ecs_new(world);
  ecs_set(world, quad, FlecsQuad, {2, 2});
  ecs_set(world, quad, FlecsPosition3, {-6, -3, -9});
  ecs_set(world, quad, FlecsRotation3, {-M_PI / 2, 0, 0});
  ecs_set(world, quad, FlecsRgba, {128, 0, 0});

  ecs_entity_t triangle = ecs_new(world);
  ecs_set(world, triangle, FlecsTriangle, {2, 2});
  ecs_set(world, triangle, FlecsPosition3, {-3, -3, -9});
  ecs_set(world, triangle, FlecsRotation3, {-M_PI / 2, 0, 0});
  ecs_set(world, triangle, FlecsRgba, {64, 64, 0});

  ecs_entity_t right_triangle = ecs_new(world);
  ecs_set(world, right_triangle, FlecsRightTriangle, {2, 2});
  ecs_set(world, right_triangle, FlecsPosition3, {0, -3, -9});
  ecs_set(world, right_triangle, FlecsRotation3, {-M_PI / 2, 0, 0});
  ecs_set(world, right_triangle, FlecsRgba, {0, 128, 0});

  ecs_singleton_set(world, EcsRest, {0});

  double i = 0;
  while (ecs_progress(world, 0)) {
    ecs_set(world, box, FlecsRotation3, {0, i, 0});
    ecs_set(world, triangle_prism, FlecsRotation3, {0, i * 1.4, 0});
    ecs_set(world, right_triangle_prism, FlecsRotation3, {0, i * 2.8, 0});
    ecs_set(world, pyramid, FlecsRotation3, {0, i * 3.2, 0});
    ecs_set(world, sphere, FlecsRotation3, {0, i, 0});

    for (int c = 0; c < 5; c ++) {
      ecs_set(world, spheres[c], FlecsRotation3, {0, i, 0});
    }

    i += 0.01;
  }

  return ecs_fini(world);
}
