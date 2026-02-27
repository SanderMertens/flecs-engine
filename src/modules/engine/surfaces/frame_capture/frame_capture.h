#include "../../../../types.h"
#include "../../engine.h"

#ifndef FLECS_ENGINE_FRAME_CAPTURE_H
#define FLECS_ENGINE_FRAME_CAPTURE_H

typedef struct {
    const char *path;
} FlecsEngineFrameCaptureOutputConfig;

extern const FlecsEngineSurfaceInterface flecsEngineFrameCaptureOutputOps;

void FlecsEngineFrameCaptureImport(
    ecs_world_t *world);

#endif
