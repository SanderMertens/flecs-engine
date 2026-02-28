#define FLECS_ENGINE_CAMERA_IMPL
#include "camera.h"

ECS_COMPONENT_DECLARE(FlecsCameraImpl);
static int32_t g_camera_log_count = 0;
static const int32_t kCameraLogLimit = 60;

static void FlecsCameraTransformMvp(ecs_iter_t *it) {
    FlecsCamera *cameras = ecs_field(it, FlecsCamera, 0);
    FlecsCameraImpl *impl = ecs_field(it, FlecsCameraImpl, 1);
    const FlecsWorldTransform3 *wt = ecs_field(it, FlecsWorldTransform3, 2);
    const FlecsEngineImpl *engine = ecs_singleton_get(it->world, FlecsEngineImpl);
    float window_aspect = 0.0f;
    if (engine && engine->width > 0 && engine->height > 0) {
        window_aspect = (float)engine->width / (float)engine->height;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        FlecsCamera *cam = &cameras[i];
        if (window_aspect > 0.0f) {
            cam->aspect_ratio = window_aspect;
        }

        if (cam->orthographic) {
            glm_ortho_default(
                cam->aspect_ratio, 
                impl[i].proj);
        } else {
            glm_perspective(
                cam->fov, cam->aspect_ratio, cam->near_, cam->far_,
                impl[i].proj);
        }

        if (wt) {
            const FlecsWorldTransform3 *cam_wt = ecs_field_is_self(it, 2) ? &wt[i] : wt;
            mat4 cam_world;
            for (int32_t r = 0; r < 4; r ++) {
                for (int32_t c = 0; c < 4; c ++) {
                    cam_world[r][c] = cam_wt->m[r][c];
                }
            }
            glm_mat4_inv(cam_world, impl[i].view);
        } else {
            glm_mat4_identity(impl[i].view);
        }

        glm_mat4_mul(impl[i].proj, impl[i].view, impl[i].mvp);

        if (g_camera_log_count < kCameraLogLimit) {
            ecs_log(0,
                "[camera] entity=%llu fov=%.3f aspect=%.3f near=%.3f far=%.3f ortho=%d mvp00=%.3f mvp11=%.3f",
                (unsigned long long)it->entities[i],
                cameras[i].fov,
                cameras[i].aspect_ratio,
                cameras[i].near_,
                cameras[i].far_,
                cameras[i].orthographic,
                impl[i].mvp[0][0],
                impl[i].mvp[1][1]);
            g_camera_log_count ++;
        }
    }
}

void FlecsEngineCameraImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineCamera);

    ecs_set_name_prefix(world, "Flecs");
    
    ECS_COMPONENT_DEFINE(world, FlecsCameraImpl);
    ECS_META_COMPONENT(world, FlecsCamera);

    ecs_add_pair(world, 
        ecs_id(FlecsCamera), EcsWith, 
        ecs_id(FlecsCameraImpl));

    ECS_SYSTEM(world, FlecsCameraTransformMvp, EcsPreStore,
        Camera, CameraImpl, ?flecs.engine.transform3.WorldTransform3);
}
