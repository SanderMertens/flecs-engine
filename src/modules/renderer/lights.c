#include <math.h>
#include <string.h>

#include "renderer.h"
#include "flecs_engine.h"

void flecsEngine_setupLights(
    const ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    int32_t count = 0;

    /* Point lights */
    if (engine->lighting.point_light_query) {
        ecs_iter_t it = ecs_query_iter(world, engine->lighting.point_light_query);
        while (ecs_query_next(&it)) {
            const FlecsPointLight *lights = ecs_field(&it, FlecsPointLight, 0);
            const FlecsWorldTransform3 *transforms = ecs_field(&it, FlecsWorldTransform3, 1);
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

            int32_t needed = count + it.count;
            flecsEngine_cluster_ensureLights(engine, needed);

            for (int32_t i = 0; i < it.count; i ++) {
                FlecsGpuLight *gpu_light = &engine->lighting.cpu_lights[count];
                gpu_light->position[0] = transforms[i].m[3][0];
                gpu_light->position[1] = transforms[i].m[3][1];
                gpu_light->position[2] = transforms[i].m[3][2];
                gpu_light->position[3] = lights[i].range;

                gpu_light->direction[0] = 0.0f;
                gpu_light->direction[1] = 0.0f;
                gpu_light->direction[2] = 0.0f;
                gpu_light->direction[3] = -2.0f; /* sentinel: not a spot light */

                float r = 1.0f, g = 1.0f, b = 1.0f;
                if (colors) {
                    r = flecsEngine_colorChannelToFloat(colors[i].r);
                    g = flecsEngine_colorChannelToFloat(colors[i].g);
                    b = flecsEngine_colorChannelToFloat(colors[i].b);
                }

                gpu_light->color[0] = r * lights[i].intensity;
                gpu_light->color[1] = g * lights[i].intensity;
                gpu_light->color[2] = b * lights[i].intensity;
                gpu_light->color[3] = 0.0f;

                count ++;
            }
        }
    }

    /* Spot lights */
    if (engine->lighting.spot_light_query) {
        ecs_iter_t it = ecs_query_iter(world, engine->lighting.spot_light_query);
        while (ecs_query_next(&it)) {
            const FlecsSpotLight *lights = ecs_field(&it, FlecsSpotLight, 0);
            const FlecsWorldTransform3 *transforms = ecs_field(&it, FlecsWorldTransform3, 1);
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

            int32_t needed = count + it.count;
            flecsEngine_cluster_ensureLights(engine, needed);

            for (int32_t i = 0; i < it.count; i ++) {
                FlecsGpuLight *gpu_light = &engine->lighting.cpu_lights[count];
                gpu_light->position[0] = transforms[i].m[3][0];
                gpu_light->position[1] = transforms[i].m[3][1];
                gpu_light->position[2] = transforms[i].m[3][2];
                gpu_light->position[3] = lights[i].range;

                /* Extract forward direction (-Z axis) from world transform */
                float dx = -transforms[i].m[2][0];
                float dy = -transforms[i].m[2][1];
                float dz = -transforms[i].m[2][2];
                float len = sqrtf(dx * dx + dy * dy + dz * dz);
                if (len > 1e-6f) {
                    dx /= len;
                    dy /= len;
                    dz /= len;
                } else {
                    dx = 0.0f;
                    dy = -1.0f;
                    dz = 0.0f;
                }

                gpu_light->direction[0] = dx;
                gpu_light->direction[1] = dy;
                gpu_light->direction[2] = dz;
                gpu_light->direction[3] = cosf(lights[i].outer_angle * (3.141592653589793f / 180.0f));

                float r = 1.0f, g = 1.0f, b = 1.0f;
                if (colors) {
                    r = flecsEngine_colorChannelToFloat(colors[i].r);
                    g = flecsEngine_colorChannelToFloat(colors[i].g);
                    b = flecsEngine_colorChannelToFloat(colors[i].b);
                }

                gpu_light->color[0] = r * lights[i].intensity;
                gpu_light->color[1] = g * lights[i].intensity;
                gpu_light->color[2] = b * lights[i].intensity;
                gpu_light->color[3] = cosf(lights[i].inner_angle * (3.141592653589793f / 180.0f));

                count ++;
            }
        }
    }

    engine->lighting.light_count = count;
}
