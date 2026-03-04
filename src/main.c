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
  FlecsRenderView view = {};
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
      .near_ = 0.1f,
      .far_ = 1000.0f,
      .aspect_ratio = options.width / (float)options.height
  });
  ecs_add(world, view.camera, FlecsCameraController);
  ecs_set(world, view.camera, FlecsPosition3, {0, 10, 24});
  ecs_set(world, view.camera, FlecsLookAt, {0, 8, 0});

  // Light
  view.light = ecs_entity(world, { .name = "light" });
  ecs_set(world, view.light, FlecsPosition3, {8, 0, 5});
  ecs_set(world, view.light, FlecsDirectionalLight, { .intensity = 1.0f });
  ecs_set(world, view.light, FlecsLookAt, { 0, 0, 0 });
  ecs_set(world, view.light, FlecsRgba, {255, 255, 255, 255});

  // HDRI (optional, for image based lighting)
  view.hdri = flecsEngine_createHdri(
    world, view_entity, "hdri", "industrial_sunset_puresky_4k.exr", 1024, 64);

  // RenderBatches (what to render in scene)
  ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
    flecsEngine_createBatch_skybox(world, view_entity, "skyboxBatch");
  ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
    flecsEngine_createBatchSet_primitiveShapes(world, view_entity, "primitiveBatch");
  ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
    flecsEngine_createBatchSet_primitiveShapes_matIndex(world, view_entity, "primitiveWMatIndexBatch");

  // Post process effects
  FlecsBloom bloom_settings = flecsEngine_bloomSettingsDefault();
  ecs_vec_append_t(NULL, &view.effects, ecs_entity_t)[0] =
    flecsEngine_createEffect_bloom(world, view_entity, "bloomEffect", 0, &bloom_settings);
  ecs_vec_append_t(NULL, &view.effects, ecs_entity_t)[0] =
    flecsEngine_createEffect_tonyMcMapFace(world, view_entity, "tonyMcMapFaceEffect", 1);

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
  ECS_IMPORT(world, FlecsEngine);

  initEngine(world, options);

  int numShapes = 7;
  int shapeY = 15;
  int shapeZ = 0;
  const float spinSpeed = 1.0f;

  ecs_entity_t shapes =  ecs_entity(world, { .name = "shapes" });

  // Ground plane
  ecs_entity_t ground = ecs_new_w_pair(world, EcsChildOf, shapes);
  ecs_set(world, ground, FlecsQuad, {2000, 2000});
  ecs_set(world, ground, FlecsPosition3, {0, 0, 0});
  ecs_set(world, ground, FlecsPbrMaterial, {0, 1});
  ecs_set(world, ground, FlecsRgba, {200, 200, 200});
  ecs_set(world, ground, FlecsRotation3, {-M_PI / 2, 0, 0});

  // 3D shapes
  ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, shapes);
  ecs_set(world, box, FlecsBox, {2, 2, 2});
  ecs_set(world, box, FlecsPosition3, {-9, shapeY, shapeZ});
  ecs_set(world, box, FlecsAngularVelocity3, {0.0f, spinSpeed, 0.0f});

  ecs_entity_t triangle_prism = ecs_new_w_pair(world, EcsChildOf, shapes);
  ecs_set(world, triangle_prism, FlecsTrianglePrism, {2, 2, 2});
  ecs_set(world, triangle_prism, FlecsPosition3, {-6, shapeY, shapeZ});
  ecs_set(world, triangle_prism, FlecsAngularVelocity3, {0.0f, spinSpeed, 0.0f});

  ecs_entity_t right_triangle_prism = ecs_new_w_pair(world, EcsChildOf, shapes);
  ecs_set(world, right_triangle_prism, FlecsRightTrianglePrism, {2, 2, 2});
  ecs_set(world, right_triangle_prism, FlecsPosition3, {-3, shapeY, shapeZ});
  ecs_set(world, right_triangle_prism, FlecsAngularVelocity3, {0.0f, spinSpeed, 0.0f});

  ecs_entity_t quad = ecs_new_w_pair(world, EcsChildOf, shapes);
  ecs_set(world, quad, FlecsQuad, {2, 2});
  ecs_set(world, quad, FlecsPosition3, {0, shapeY, shapeZ});

  ecs_entity_t triangle = ecs_new_w_pair(world, EcsChildOf, shapes);
  ecs_set(world, triangle, FlecsTriangle, {2, 2});
  ecs_set(world, triangle, FlecsPosition3, {3, shapeY, shapeZ});

  ecs_entity_t right_triangle = ecs_new_w_pair(world, EcsChildOf, shapes);
  ecs_set(world, right_triangle, FlecsRightTriangle, {2, 2});
  ecs_set(world, right_triangle, FlecsPosition3, {6, shapeY, shapeZ});

  ecs_entity_t ngon = ecs_new_w_pair(world, EcsChildOf, shapes);
  ecs_set(world, ngon, FlecsNGon, { .sides = 6 });
  ecs_set(world, ngon, FlecsPosition3, {9, shapeY, shapeZ});
  ecs_set(world, ngon, FlecsScale3, {2, 2, 2});

  // Spheres
  ecs_entity_t spheres[numShapes];
  for (int i = 0; i < numShapes; i ++) {
    spheres[i] = ecs_new_w_pair(world, EcsChildOf, shapes);
    ecs_set(world, spheres[i], FlecsSphere, { .segments = 3 + i, .smooth = i == (numShapes - 1), .radius = 1 });
    ecs_set(world, spheres[i], FlecsPosition3, {-9 + i * 3, shapeY - 3, shapeZ});
    ecs_set(world, spheres[i], FlecsRgba, {255});
    ecs_set(world, spheres[i], FlecsEmissive, {{255, 0, 0}, 25.0});
    ecs_set(world, spheres[i], FlecsAngularVelocity3, {0.0f, spinSpeed, 0.0f});
  }

  // Icospheres
  ecs_entity_t icospheres[numShapes];
  for (int i = 0; i < numShapes; i ++) {
    icospheres[i] = ecs_new_w_pair(world, EcsChildOf, shapes);
    ecs_set(world, icospheres[i], FlecsIcoSphere, { .segments = i, .smooth = i == (numShapes - 1), .radius = 1 });
    ecs_set(world, icospheres[i], FlecsPosition3, {-9 + i * 3, shapeY - 6, shapeZ});
    ecs_set(world, icospheres[i], FlecsRgba, {128, 128});
    ecs_set(world, icospheres[i], FlecsAngularVelocity3, {0.0f, spinSpeed, 0.0f});
  }

  // Cylinders
  ecs_entity_t cylinders[numShapes];
  for (int i = 0; i < numShapes; i ++) {
    cylinders[i] = ecs_new_w_pair(world, EcsChildOf, shapes);
    ecs_set(world, cylinders[i], FlecsCylinder, { .segments = 3 + i, .smooth = i == (numShapes - 1), .length = 1 });
    ecs_set(world, cylinders[i], FlecsPosition3, {-9 + i * 3, shapeY - 9, shapeZ});
    ecs_set(world, cylinders[i], FlecsScale3, {2, 2, 2});
    ecs_set(world, cylinders[i], FlecsRgba, {0, 255});
    ecs_set(world, cylinders[i], FlecsAngularVelocity3, {0.0f, spinSpeed, 0.0f});
  }

  // Cones
  ecs_entity_t cones[numShapes];
  for (int i = 0; i < numShapes; i ++) {
    cones[i] = ecs_new_w_pair(world, EcsChildOf, shapes);
    ecs_set(world, cones[i], FlecsCone, { .sides = 3 + i, .smooth = i == (numShapes - 1), .length = 1 });
    ecs_set(world, cones[i], FlecsPosition3, {-9 + i * 3, shapeY - 12, shapeZ});
    ecs_set(world, cones[i], FlecsScale3, {2, 2, 2});
    ecs_set(world, cones[i], FlecsRgba, {0, 0, 255});
    ecs_set(world, cones[i], FlecsAngularVelocity3, {0.0f, spinSpeed, 0.0f});
  }

  // Hemispheres
  ecs_entity_t hemispheres[numShapes];
  for (int i = 0; i < numShapes; i ++) {
    hemispheres[i] = ecs_new_w_pair(world, EcsChildOf, shapes);
    ecs_set(world, hemispheres[i], FlecsHemiSphere, { .segments = 3 + i, .smooth = i == (numShapes - 1), .radius = 1 });
    ecs_set(world, hemispheres[i], FlecsPosition3, {-9 + i * 3, shapeY - 15, shapeZ});
    ecs_set(world, hemispheres[i], FlecsRgba, {128, 0, 128});
    ecs_set(world, hemispheres[i], FlecsAngularVelocity3, {0.0f, spinSpeed, 0.0f});
  }

  numShapes = 6;

  // PBR sphere grid (roughness: X axis, metallic: Z axis)
  for (int x = 0; x < numShapes; x ++) {
    for (int y = 0; y < numShapes; y ++) {
      float metallic = ((float)x / (float)(numShapes - 1));
      float roughness = ((float)y / (float)(numShapes - 1));
      ecs_entity_t sphere = ecs_new_w_pair(world, EcsChildOf, shapes);
      ecs_set(world, sphere, FlecsSphere, {
        .segments = 24,
        .smooth = true,
        .radius = 1
      });
      ecs_set(world, sphere, FlecsPosition3, {
        9 + x * 2 + (numShapes * 2) - 2,
        y * 2 + 2,
        0
      });

      ecs_set(world, sphere, FlecsRgba, {200, 200, 200, 255});
      ecs_set(world, sphere, FlecsPbrMaterial, {
        .metallic = metallic,
        .roughness = roughness
      });
    }
  }

  // PBR sphere grid (roughness: X axis, metallic: Z axis)
  for (int x = 0; x < numShapes; x ++) {
    for (int y = 0; y < numShapes; y ++) {
      float metallic = ((float)x / (float)(numShapes - 1));
      float roughness = ((float)y / (float)(numShapes - 1));
      ecs_entity_t sphere = ecs_new_w_pair(world, EcsChildOf, shapes);
      ecs_set(world, sphere, FlecsSphere, {
        .segments = 24,
        .smooth = true,
        .radius = 1
      });
      ecs_set(world, sphere, FlecsPosition3, {
        -12 + x * 2 - (numShapes * 2) - 2,
        y * 2 + 2,
        0
      });

      ecs_entity_t material = ecs_new_w_id(world, EcsPrefab);
      ecs_add_pair(world, material, EcsChildOf, shapes);
      ecs_set(world, material, FlecsRgba, {200, 200, 200, 255});
      ecs_set(world, material, FlecsPbrMaterial, {
        .metallic = metallic,
        .roughness = roughness
      });

      ecs_add_pair(world, sphere, EcsIsA, material);
    }
  }

  ecs_entity_t material = ecs_new_w_id(world, EcsPrefab);
  ecs_add_pair(world, material, EcsChildOf, shapes);
  ecs_set(world, material, FlecsRgba, {0, 0, 0, 255});
  ecs_set(world, material, FlecsPbrMaterial, {
    .metallic = 0,
    .roughness = 1
  });

  numShapes = 100;
  for (int x = 0; x < numShapes; x ++) {
    for (int y = 0; y < numShapes; y ++) {
      float h = (x + y) / 2 + 1;
      ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, shapes);
      ecs_set(world, e, FlecsBox, { 2, h, 2 });
      ecs_set(world, e, FlecsPosition3, { 20 + x * 2, h / 2, -10 - y * 2 });
      ecs_add_pair(world, e, EcsIsA, material);
    }
  }

  // numShapes = 100;

  // for (int x = 0; x < numShapes; x ++) {
  //   for (int y = 0; y < numShapes; y ++) {
  //     ecs_entity_t sphere = ecs_new_w_pair(world, EcsChildOf, shapes);
  //     ecs_set(world, sphere, FlecsIcoSphere, {
  //       .segments = 3,
  //       .smooth = true,
  //       .radius = 1
  //     });
  //     ecs_set(world, sphere, FlecsPosition3, {
  //       -12 + x * 2 - (numShapes * 1) - 2,
  //       0,
  //       y * 2 + 2
  //     });
  //     ecs_add_pair(world, sphere, EcsIsA, material);
  //   }
  // }


  ecs_singleton_set(world, EcsRest, {0});

  double i = 0;
  while (ecs_progress(world, 0)) {
    // ecs_set(world, light, FlecsPosition3, {sin(i) * 5, 0, cos(i) * 5});
    i -= 0.005;
  }

  return ecs_fini(world);
}
