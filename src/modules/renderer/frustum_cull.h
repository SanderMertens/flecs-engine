#ifndef FLECS_ENGINE_FRUSTUM_CULL_H
#define FLECS_ENGINE_FRUSTUM_CULL_H

#include "../../types.h"

/* Extract 6 normalized frustum planes from a view-projection matrix. */
void flecsEngine_frustum_extractPlanes(
    const float m[4][4],
    float planes[6][4]);

/* Compute world-space AABB from local AABB + world transform + scale
 * using the Arvo method (18 multiplies). */
void flecsEngine_computeWorldAABB(
    const FlecsWorldTransform3 *wt,
    const float local_min[3],
    const float local_max[3],
    float sx,
    float sy,
    float sz,
    float world_min[3],
    float world_max[3]);

/* Test a world-space AABB against 6 frustum planes (P-vertex approach).
 * Returns true if the AABB is inside or intersects the frustum. */
bool flecsEngine_testAABBFrustum(
    const float planes[6][4],
    const float world_min[3],
    const float world_max[3]);

#endif
