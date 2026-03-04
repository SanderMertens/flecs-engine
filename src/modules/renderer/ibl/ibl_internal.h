#ifndef FLECS_ENGINE_IBL_INTERNAL_H
#define FLECS_ENGINE_IBL_INTERNAL_H

#include "../renderer.h"

bool flecsIblCreateRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecHdriImpl *ibl);

void flecsIblReleaseRuntimeResources(
    FlecHdriImpl *ibl);

#endif
