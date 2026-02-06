#define FLECS_ENGINE_CAMERA_IMPL
#include "camera.h"

ECS_COMPONENT_DECLARE(FlecsCameraImpl);

static void FlecsCameraTransformMvp(ecs_iter_t *it) {
    FlecsCamera *cameras = ecs_field(it, FlecsCamera, 0);
    FlecsCameraImpl *impl = ecs_field(it, FlecsCameraImpl, 1);

    for (int32_t i = 0; i < it->count; i ++) {
        FlecsCamera *cam = &cameras[i];

        if (cam->orthographic) {
            glm_ortho_default(
                cam->aspect_ratio, 
                impl[i].proj);
        } else {
            glm_perspective(
                cam->fov, cam->aspect_ratio, cam->near_, cam->far_,
                impl[i].proj);
        }

        glm_mat4_copy(impl[i].proj, impl[i].mvp);
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

    ECS_SYSTEM(world, FlecsCameraTransformMvp, EcsPostLoad,
        Camera, CameraImpl);
}
