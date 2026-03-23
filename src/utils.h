#include "types.h"

void flecsEngine_registerVec3Type(
    ecs_world_t *world,
    ecs_entity_t component);

WGPUColor flecsEngine_getSkyColor(
    const FlecsEngineImpl *impl);

void flecsEngine_getSkyColorVec4(
    const FlecsEngineImpl *impl,
    float out[4]);

uint64_t flecsEngine_type_sizeof(
    const ecs_world_t *world,
    ecs_entity_t type);

float flecsEngine_colorChannelToFloat(
    uint8_t value);

int32_t flecsEngine_vertexAttrFromType(
    const ecs_world_t *world,
    ecs_entity_t type,
    WGPUVertexAttribute *attrs,
    int32_t attr_count,
    int32_t location_offset);

/* Compute a normalized light ray direction from pitch/yaw rotation.
 * Returns true if the direction is valid, false if degenerate. */
bool flecsEngine_lightDirFromRotation(
    const FlecsRotation3 *rotation,
    float out_ray_dir[3]);

/* Returns the effective HDR color format, falling back to the surface
 * format when no HDR format is configured. */
WGPUTextureFormat flecsEngine_getHdrFormat(
    const FlecsEngineImpl *impl);

/* Returns the format for the texture that the effect chain ultimately writes
 * to.  On native this is the surface format; on Emscripten it is the HDR
 * format (the gamma-correction pass converts to the surface format). */
WGPUTextureFormat flecsEngine_getViewTargetFormat(
    const FlecsEngineImpl *impl);

ecs_entity_t flecsEngine_vecEntity(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecVec3(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecU16(
    ecs_world_t *world);
