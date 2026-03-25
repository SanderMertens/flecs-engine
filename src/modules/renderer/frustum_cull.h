#ifndef FLECS_ENGINE_FRUSTUM_CULL_H
#define FLECS_ENGINE_FRUSTUM_CULL_H

#include "../../types.h"

void flecsEngine_frustum_extractPlanes(
    const float m[4][4],
    float planes[6][4]);

bool flecsEngine_frustumTestAABB(
    const float planes[6][4],
    const FlecsWorldTransform3 *wt,
    const float local_min[3],
    const float local_max[3],
    float sx,
    float sy,
    float sz);

#endif
