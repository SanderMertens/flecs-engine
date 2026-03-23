#ifndef FLECS_ENGINE_IBL_INTERNAL_H
#define FLECS_ENGINE_IBL_INTERNAL_H

#include "../renderer.h"
#include "hdri_loader.h"

bool flecsIblBuildDefaultImage(
    const flecs_rgba_t *sky_color,
    const flecs_rgba_t *ground_color,
    const flecs_rgba_t *haze_color,
    const flecs_rgba_t *horizon_color,
    FlecsHdriImage *image);

bool flecsEngine_ibl_createRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl);

void flecsEngine_ibl_releaseRuntimeResources(
    FlecsHdriImpl *ibl);

#endif
