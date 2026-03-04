#ifndef FLECS_ENGINE_IBL_INTERNAL_H
#define FLECS_ENGINE_IBL_INTERNAL_H

#include "../renderer.h"

bool flecsIblCreateRuntimeBindGroup(
    const FlecsEngineImpl *engine,
    FlecsIblImpl *ibl);

void flecsIblReleaseRuntimeResources(
    FlecsIblImpl *ibl);

#endif
