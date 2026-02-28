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
  ECS_IMPORT(world, FlecsStats);
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
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_quads(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_triangles(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_right_triangles(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_triangle_prisms(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_right_triangle_prisms(world);
  ecs_vec_append_t(NULL, &v->batches, ecs_entity_t)[0] = flecsEngine_createBatch_litColoredGeometry(world);
  ecs_vec_append_t(NULL, &v->effects, ecs_entity_t)[0] =
    flecsEngine_createEffect_tonyMcMapFace(world, 0 /* input */);
  // ecs_vec_append_t(NULL, &v->effects, ecs_entity_t)[0] =
  //   flecsEngine_createEffect_invert(world, 0 /* input */);
  v->camera = camera;
  ecs_modified(world, view, FlecsRenderView);

  int NumShapes = 7;

  // First row: spheres
  ecs_entity_t spheres[NumShapes];
  for (int i = 0; i < NumShapes; i ++) {
    spheres[i] = ecs_new(world);
    ecs_set(world, spheres[i], FlecsSphere, { .segments = 3 + i, .smooth = i == (NumShapes - 1), .radius = 1 });
    ecs_set(world, spheres[i], FlecsPosition3, {-9 + i * 3, 3, -14});
    ecs_set(world, spheres[i], FlecsRgba, {255});
  }

  // Second row: icospheres
  ecs_entity_t icospheres[NumShapes];
  for (int i = 0; i < NumShapes; i ++) {
    icospheres[i] = ecs_new(world);
    ecs_set(world, icospheres[i], FlecsIcoSphere, { .segments = i, .smooth = i == (NumShapes - 1), .radius = 1 });
    ecs_set(world, icospheres[i], FlecsPosition3, {-9 + i * 3, 0, -14});
    ecs_set(world, icospheres[i], FlecsRgba, {128, 128});
  }

  // Third row: cylinders
  ecs_entity_t cylinders[NumShapes];
  for (int i = 0; i < NumShapes; i ++) {
    cylinders[i] = ecs_new(world);
    ecs_set(world, cylinders[i], FlecsCylinder, { .segments = 3 + i, .smooth = i == (NumShapes - 1), .length = 1 });
    ecs_set(world, cylinders[i], FlecsPosition3, {-9 + i * 3, -3, -14});
    ecs_set(world, cylinders[i], FlecsScale3, {2, 2, 2});
    ecs_set(world, cylinders[i], FlecsRgba, {0, 255});
  }

  // Fourth row: pyramids
  ecs_entity_t pyramids[NumShapes];
  for (int i = 0; i < NumShapes; i ++) {
    pyramids[i] = ecs_new(world);
    ecs_set(world, pyramids[i], FlecsPyramid, { .sides = 3 + i, .smooth = i == (NumShapes - 1), .length = 1 });
    ecs_set(world, pyramids[i], FlecsPosition3, {-9 + i * 3, -6, -14});
    ecs_set(world, pyramids[i], FlecsScale3, {2, 2, 2});
    ecs_set(world, pyramids[i], FlecsRgba, {0, 0, 255});
  }

  // Fourth row: 3D shapes
  ecs_entity_t box = ecs_new(world);
  ecs_set(world, box, FlecsBox, {2, 2, 2});
  ecs_set(world, box, FlecsPosition3, {-9, 6, -14});
  ecs_set(world, box, FlecsRgba, {255, 0, 0});

  ecs_entity_t triangle_prism = ecs_new(world);
  ecs_set(world, triangle_prism, FlecsTrianglePrism, {2, 2, 2});
  ecs_set(world, triangle_prism, FlecsPosition3, {-6, 6, -14});
  ecs_set(world, triangle_prism, FlecsRgba, {128, 128, 0});

  ecs_entity_t right_triangle_prism = ecs_new(world);
  ecs_set(world, right_triangle_prism, FlecsRightTrianglePrism, {2, 2, 2});
  ecs_set(world, right_triangle_prism, FlecsPosition3, {-3, 6, -14});
  ecs_set(world, right_triangle_prism, FlecsRgba, {0, 128, 0});

  ecs_entity_t quad = ecs_new(world);
  ecs_set(world, quad, FlecsQuad, {2, 2});
  ecs_set(world, quad, FlecsPosition3, {0, 6, -14});
  ecs_set(world, quad, FlecsRgba, {255, 0, 0});

  ecs_entity_t triangle = ecs_new(world);
  ecs_set(world, triangle, FlecsTriangle, {2, 2});
  ecs_set(world, triangle, FlecsPosition3, {3, 6, -14});
  ecs_set(world, triangle, FlecsRgba, {128, 128, 0});

  ecs_entity_t right_triangle = ecs_new(world);
  ecs_set(world, right_triangle, FlecsRightTriangle, {2, 2});
  ecs_set(world, right_triangle, FlecsPosition3, {6, 6, -14});
  ecs_set(world, right_triangle, FlecsRgba, {0, 255, 0});

  ecs_entity_t ngon = ecs_new(world);
  ecs_set(world, ngon, FlecsNGon, { .sides = 6 });
  ecs_set(world, ngon, FlecsPosition3, {9, 6, -14});
  ecs_set(world, ngon, FlecsScale3, {2, 2, 2});
  ecs_set(world, ngon, FlecsRgba, {0, 0, 255});

  ecs_singleton_set(world, EcsRest, {0});

  double i = 0;
  while (ecs_progress(world, 0)) {
    ecs_set(world, box, FlecsRotation3, {0, i, 0});
    ecs_set(world, triangle_prism, FlecsRotation3, {0, i, 0});
    ecs_set(world, right_triangle_prism, FlecsRotation3, {0, i, 0});

    for (int c = 0; c < NumShapes; c ++) {
      ecs_set(world, spheres[c], FlecsRotation3, {0, i, 0});
      ecs_set(world, icospheres[c], FlecsRotation3, {0, i, 0});
      ecs_set(world, cylinders[c], FlecsRotation3, {0, i, 0});
      ecs_set(world, pyramids[c], FlecsRotation3, {0, i, 0});
    }

    i += 0.01;
  }

  return ecs_fini(world);
}
