#include <math.h>
#include "frustum_cull.h"

/* Extract 6 frustum planes from a view-projection matrix (Gribb-Hartmann).
 * Each plane (a,b,c,d) satisfies: ax+by+cz+d >= 0 for points inside.
 * Planes are normalized so that (a,b,c) is a unit normal.
 * Order: left, right, bottom, top, near, far. */
void flecsEngine_frustum_extractPlanes(
    const float m[4][4],
    float planes[6][4])
{
    /* cglm is column-major: m[col][row].
     * Row i of the matrix = m[0][i], m[1][i], m[2][i], m[3][i] */

    /* Left:   row3 + row0 */
    planes[0][0] = m[0][3] + m[0][0];
    planes[0][1] = m[1][3] + m[1][0];
    planes[0][2] = m[2][3] + m[2][0];
    planes[0][3] = m[3][3] + m[3][0];

    /* Right:  row3 - row0 */
    planes[1][0] = m[0][3] - m[0][0];
    planes[1][1] = m[1][3] - m[1][0];
    planes[1][2] = m[2][3] - m[2][0];
    planes[1][3] = m[3][3] - m[3][0];

    /* Bottom: row3 + row1 */
    planes[2][0] = m[0][3] + m[0][1];
    planes[2][1] = m[1][3] + m[1][1];
    planes[2][2] = m[2][3] + m[2][1];
    planes[2][3] = m[3][3] + m[3][1];

    /* Top:    row3 - row1 */
    planes[3][0] = m[0][3] - m[0][1];
    planes[3][1] = m[1][3] - m[1][1];
    planes[3][2] = m[2][3] - m[2][1];
    planes[3][3] = m[3][3] - m[3][1];

    /* Near:   row3 + row2 */
    planes[4][0] = m[0][3] + m[0][2];
    planes[4][1] = m[1][3] + m[1][2];
    planes[4][2] = m[2][3] + m[2][2];
    planes[4][3] = m[3][3] + m[3][2];

    /* Far:    row3 - row2 */
    planes[5][0] = m[0][3] - m[0][2];
    planes[5][1] = m[1][3] - m[1][2];
    planes[5][2] = m[2][3] - m[2][2];
    planes[5][3] = m[3][3] - m[3][2];

    /* Normalize each plane */
    for (int i = 0; i < 6; i ++) {
        float len = sqrtf(
            planes[i][0] * planes[i][0] +
            planes[i][1] * planes[i][1] +
            planes[i][2] * planes[i][2]);
        if (len > 1e-8f) {
            float inv = 1.0f / len;
            planes[i][0] *= inv;
            planes[i][1] *= inv;
            planes[i][2] *= inv;
            planes[i][3] *= inv;
        }
    }
}

/* Compute world-space AABB from local AABB + world transform + scale
 * using the Arvo method.
 * world_pos = M * local_pos, where M is column-major m[col][row].
 * world[i] = m[0][i]*p.x + m[1][i]*p.y + m[2][i]*p.z + m[3][i] */
void flecsEngine_computeWorldAABB(
    const FlecsWorldTransform3 *wt,
    const float local_min[3],
    const float local_max[3],
    float sx,
    float sy,
    float sz,
    float world_min[3],
    float world_max[3])
{
    float smin[3] = { local_min[0] * sx, local_min[1] * sy, local_min[2] * sz };
    float smax[3] = { local_max[0] * sx, local_max[1] * sy, local_max[2] * sz };

    for (int i = 0; i < 3; i ++) {
        world_min[i] = world_max[i] = wt->m[3][i];
        for (int j = 0; j < 3; j ++) {
            float e = wt->m[j][i] * smin[j];
            float f = wt->m[j][i] * smax[j];
            if (e < f) {
                world_min[i] += e;
                world_max[i] += f;
            } else {
                world_min[i] += f;
                world_max[i] += e;
            }
        }
    }
}

/* Test a world-space AABB against 6 frustum planes (P-vertex approach).
 * For each plane, the corner most aligned with the plane normal is tested.
 * If that corner is behind the plane, the AABB is fully outside. */
bool flecsEngine_testAABBFrustum(
    const float planes[6][4],
    const float world_min[3],
    const float world_max[3])
{
    for (int p = 0; p < 6; p ++) {
        float a = planes[p][0];
        float b = planes[p][1];
        float c = planes[p][2];
        float d = planes[p][3];

        float px = (a >= 0.0f) ? world_max[0] : world_min[0];
        float py = (b >= 0.0f) ? world_max[1] : world_min[1];
        float pz = (c >= 0.0f) ? world_max[2] : world_min[2];

        if (a * px + b * py + c * pz + d < 0.0f) {
            return false;
        }
    }

    return true;
}
