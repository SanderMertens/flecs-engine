#include "geometry3.h"
#include <math.h>

static void flecsGeometry3_generateSphereMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = (rings + 1) * (cols + 1);
    const int32_t index_count = rings * cols * 6;
    const float radius = 0.5f;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    int32_t vi = 0;
    for (int32_t y = 0; y <= rings; y ++) {
        float v_t = (float)y / (float)rings;
        float theta = v_t * (float)M_PI;
        float sin_theta = sinf(theta);
        float cos_theta = cosf(theta);

        for (int32_t x = 0; x <= cols; x ++) {
            float u = (float)x / (float)cols;
            float phi = u * 2.0f * (float)M_PI;
            float sin_phi = sinf(phi);
            float cos_phi = cosf(phi);

            float nx = sin_theta * cos_phi;
            float ny = cos_theta;
            float nz = sin_theta * sin_phi;

            vn[vi] = (flecs_vec3_t){nx, ny, nz};
            v[vi] = (flecs_vec3_t){nx * radius, ny * radius, nz * radius};
            vi ++;
        }
    }

    int32_t ii = 0;
    for (int32_t y = 0; y < rings; y ++) {
        for (int32_t x = 0; x < cols; x ++) {
            uint16_t a = (uint16_t)(y * (cols + 1) + x);
            uint16_t b = (uint16_t)(a + cols + 1);
            uint16_t c = (uint16_t)(b + 1);
            uint16_t d = (uint16_t)(a + 1);

            idx[ii ++] = a;
            idx[ii ++] = b;
            idx[ii ++] = d;

            idx[ii ++] = b;
            idx[ii ++] = c;
            idx[ii ++] = d;
        }
    }
}
