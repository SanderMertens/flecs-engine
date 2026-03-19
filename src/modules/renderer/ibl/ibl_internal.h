#ifndef FLECS_ENGINE_IBL_INTERNAL_H
#define FLECS_ENGINE_IBL_INTERNAL_H

#include "../renderer.h"

bool flecsEngine_ibl_createRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl);

void flecsEngine_ibl_releaseRuntimeResources(
    FlecsHdriImpl *ibl);

#endif
