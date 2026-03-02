#define FLECS_ENGINE_CAMERA_IMPL
#include "camera.h"

ECS_COMPONENT_DECLARE(FlecsCameraImpl);
ECS_COMPONENT_DECLARE(FlecsCameraLookAt);

static void FlecsCameraTransformMvp(ecs_iter_t *it) {
    FlecsCamera *cameras = ecs_field(it, FlecsCamera, 0);
    const FlecsCameraLookAt *lookat = ecs_field(it, FlecsCameraLookAt, 1);
    FlecsCameraImpl *impl = ecs_field(it, FlecsCameraImpl, 2);
    const FlecsWorldTransform3 *wt = ecs_field(it, FlecsWorldTransform3, 3);
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

        vec3 eye = {0.0f, 0.0f, 0.0f};
        if (wt) {
            const FlecsWorldTransform3 *cam_wt = ecs_field_is_self(it, 2) ? &wt[i] : wt;
            eye[0] = cam_wt->m[3][0];
            eye[1] = cam_wt->m[3][1];
            eye[2] = cam_wt->m[3][2];
        }

        vec3 center = {lookat->x, lookat->y, lookat->z};
        vec3 up = {0.0f, 1.0f, 0.0f};

        if (eye[0] == center[0] && eye[1] == center[1] && eye[2] == center[2]) {
            center[2] -= 1.0f;
        }

        glm_lookat(eye, center, up, impl[i].view);
        glm_mat4_mul(impl[i].proj, impl[i].view, impl[i].mvp);
    }
}

void FlecsEngineCameraImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineCamera);

    ecs_set_name_prefix(world, "Flecs");
    
    ECS_COMPONENT_DEFINE(world, FlecsCameraImpl);
    ECS_COMPONENT_DEFINE(world, FlecsCameraLookAt);
    ECS_META_COMPONENT(world, FlecsCamera);

    ecs_add_pair(world, 
        ecs_id(FlecsCamera), EcsWith, 
        ecs_id(FlecsCameraImpl));

    ecs_add_pair(world,
        ecs_id(FlecsCamera), EcsWith,
        ecs_id(FlecsCameraLookAt));

    ECS_SYSTEM(world, FlecsCameraTransformMvp, EcsPreStore,
        [in] Camera, 
        [in] CameraLookAt, 
        [out] CameraImpl, 
        [out] flecs.engine.transform3.WorldTransform3);
}
