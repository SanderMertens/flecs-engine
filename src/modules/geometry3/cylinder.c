#include "geometry3.h"
#include <math.h>

static void flecsGeometry3_generateCylinderMesh(
    FlecsMesh3 *mesh,
    int32_t segments)
{
    const float radius = 0.5f;
    const float y_top = 0.5f;
    const float y_bottom = -0.5f;

    const int32_t side_vert_count = (segments + 1) * 2;
    const int32_t cap_vert_count = segments + 2;
    const int32_t vert_count = side_vert_count + cap_vert_count + cap_vert_count;
    const int32_t index_count = (segments * 6) + (segments * 3) + (segments * 3);

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint16_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint16_t *idx = ecs_vec_first_t(&mesh->indices, uint16_t);

    int32_t vi = 0;

    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = t * 2.0f * (float)M_PI;
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){ca * radius, y_top, sa * radius};
        vn[vi] = (flecs_vec3_t){ca, 0.0f, sa};
        vi ++;

        v[vi] = (flecs_vec3_t){ca * radius, y_bottom, sa * radius};
        vn[vi] = (flecs_vec3_t){ca, 0.0f, sa};
        vi ++;
    }

    int32_t top_center = vi;
    v[vi] = (flecs_vec3_t){0.0f, y_top, 0.0f};
    vn[vi] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
    vi ++;

    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = t * 2.0f * (float)M_PI;
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){ca * radius, y_top, sa * radius};
        vn[vi] = (flecs_vec3_t){0.0f, 1.0f, 0.0f};
        vi ++;
    }

    int32_t bottom_center = vi;
    v[vi] = (flecs_vec3_t){0.0f, y_bottom, 0.0f};
    vn[vi] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};
    vi ++;

    for (int32_t s = 0; s <= segments; s ++) {
        float t = (float)s / (float)segments;
        float a = t * 2.0f * (float)M_PI;
        float ca = cosf(a);
        float sa = sinf(a);

        v[vi] = (flecs_vec3_t){ca * radius, y_bottom, sa * radius};
        vn[vi] = (flecs_vec3_t){0.0f, -1.0f, 0.0f};
        vi ++;
    }

    int32_t ii = 0;

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t a = (uint16_t)(s * 2);
        uint16_t b = (uint16_t)(a + 1);
        uint16_t c = (uint16_t)(a + 2);
        uint16_t d = (uint16_t)(a + 3);

        idx[ii ++] = a;
        idx[ii ++] = b;
        idx[ii ++] = c;

        idx[ii ++] = b;
        idx[ii ++] = d;
        idx[ii ++] = c;
    }

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t a = (uint16_t)(top_center + 1 + s);
        uint16_t b = (uint16_t)(top_center + 1 + s + 1);

        idx[ii ++] = (uint16_t)top_center;
        idx[ii ++] = a;
        idx[ii ++] = b;
    }

    for (int32_t s = 0; s < segments; s ++) {
        uint16_t a = (uint16_t)(bottom_center + 1 + s);
        uint16_t b = (uint16_t)(bottom_center + 1 + s + 1);

        idx[ii ++] = (uint16_t)bottom_center;
        idx[ii ++] = b;
        idx[ii ++] = a;
    }
}
