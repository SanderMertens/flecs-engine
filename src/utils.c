#include "private.h"

void flecsEngine_registerVec3Type(
    ecs_world_t *world,
    ecs_entity_t component)
{
    ecs_assert(component != 0, ECS_INVALID_PARAMETER, NULL);
    ecs_struct(world, {
        .entity = component,
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });
}

float flecsEngine_colorChannelToFloat(
    uint8_t value)
{
    return (float)value / 255.0f;
}

uint64_t flecsEngine_type_sizeof(
    const ecs_world_t *world,
    ecs_entity_t type)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, type);
    if (!ti) {
        char *str = ecs_id_str(world, type);
        ecs_err("entity '%s' is not a type", str);
        ecs_os_free(str);
        return 0;
    }

    return ti->size;
}

int32_t flecsEngine_vertexAttrFromType(
    const ecs_world_t *world,
    ecs_entity_t type,
    WGPUVertexAttribute *attrs,
    int32_t attr_count,
    int32_t location_offset)
{
    const EcsStruct *s = ecs_get(world, type, EcsStruct);
    if (!s) {
        char *str = ecs_id_str(world, type);
        ecs_err("cannot derive attributes from non-struct type '%s'",
            str);
        ecs_os_free(str);
        return -1;
    }

    int32_t i, member_count = ecs_vec_count(&s->members);
    if (ecs_vec_count(&s->members) >= attr_count) {
        char *str = ecs_id_str(world, type);
        ecs_err("cannot derive attributes from type '%s': too many "
            "members (%d, max is %d)", str, member_count, attr_count);
        ecs_os_free(str);
        return -1;
    }

    int32_t attr = 0;
    if (type == ecs_id(flecs_rgba_t) || type == ecs_id(FlecsRgba)) {
        attrs[attr].format = WGPUVertexFormat_Unorm8x4;
        attrs[attr].shaderLocation = location_offset + attr;
        attrs[attr].offset = 0;
        attr ++;
    } else {
        ecs_member_t *members = ecs_vec_first(&s->members);
        for (i = 0; i < member_count; i ++) {
            if (members[i].type == ecs_id(flecs_vec2_t)) {
                attrs[attr].format = WGPUVertexFormat_Float32x2;
                attrs[attr].shaderLocation = location_offset + attr;
                attrs[attr].offset = members[i].offset;
                attr ++;

            } else if (members[i].type == ecs_id(flecs_vec3_t)) {
                attrs[attr].format = WGPUVertexFormat_Float32x3;
                attrs[attr].shaderLocation = location_offset + attr;
                attrs[attr].offset = members[i].offset;
                attr ++;

            } else if (members[i].type == ecs_id(ecs_f32_t)) {
                attrs[attr].format = WGPUVertexFormat_Float32;
                attrs[attr].shaderLocation = location_offset + attr;
                attrs[attr].offset = members[i].offset;
                attr ++;

            } else if (members[i].type == ecs_id(flecs_rgba_t) ||
                       members[i].type == ecs_id(FlecsRgba)) 
            {
                attrs[attr].format = WGPUVertexFormat_Unorm8x4;
                attrs[attr].shaderLocation = location_offset + attr;
                attrs[attr].offset = members[i].offset;
                attr ++;

            } else if (members[i].type == ecs_id(ecs_u32_t)) {
                attrs[attr].format = WGPUVertexFormat_Uint32;
                attrs[attr].shaderLocation = location_offset + attr;
                attrs[attr].offset = members[i].offset;
                attr ++;

            } else if (members[i].type == ecs_id(flecs_mat4_t)) {
                if ((attr + 4) >= attr_count) {
                    char *str = ecs_id_str(world, type);
                    ecs_err("cannot derive attributes from type '%s': "
                        "too many attributes (max is %d)", str, attr_count);
                    ecs_os_free(str);
                    return -1;
                }

                for (int32_t col = 0; col < 4; col ++) {
                    attrs[attr].format = WGPUVertexFormat_Float32x4;
                    attrs[attr].shaderLocation = location_offset + attr;
                    attrs[attr].offset = members[i].offset + (sizeof(vec4) * col);
                    attr ++;
                }
            } else {
                char *type_str = ecs_id_str(world, type);
                char *member_type_str = ecs_id_str(world, members[i].type);
                ecs_err("unsupported member type '%s' for attribute '%s' "
                    "in type '%s'", member_type_str, members[i].name, type_str);
                ecs_os_free(member_type_str);
                ecs_os_free(type_str);
                return -1;
            }
        }
    }

    return attr;
}

bool flecsEngine_lightDirFromRotation(
    const FlecsRotation3 *rotation,
    float out_ray_dir[3])
{
    float pitch = rotation->x;
    float yaw = rotation->y;
    out_ray_dir[0] = sinf(yaw) * cosf(pitch);
    out_ray_dir[1] = sinf(pitch);
    out_ray_dir[2] = cosf(yaw) * cosf(pitch);

    float len = sqrtf(
        out_ray_dir[0] * out_ray_dir[0] +
        out_ray_dir[1] * out_ray_dir[1] +
        out_ray_dir[2] * out_ray_dir[2]);
    if (len <= 1e-6f) {
        return false;
    }

    float inv_len = 1.0f / len;
    out_ray_dir[0] *= inv_len;
    out_ray_dir[1] *= inv_len;
    out_ray_dir[2] *= inv_len;
    return true;
}

WGPUTextureFormat flecsEngine_getHdrFormat(
    const FlecsEngineImpl *impl)
{
    if (impl->hdr_color_format != WGPUTextureFormat_Undefined) {
        return impl->hdr_color_format;
    }
    return impl->surface_config.format;
}

WGPUTextureFormat flecsEngine_getViewTargetFormat(
    const FlecsEngineImpl *impl)
{
#ifdef __EMSCRIPTEN__
    return flecsEngine_getHdrFormat(impl);
#else
    return impl->surface_config.format;
#endif
}

ecs_entity_t flecsEngine_vecEntity(
    ecs_world_t *world) 
{
    return ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "::flecs.engine.types.vecEntity",
            .root_sep = "::"
        }),
        .type = ecs_id(ecs_entity_t)
    });
}

ecs_entity_t flecsEngine_vecVec3(
    ecs_world_t *world) 
{
    return ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "::flecs.engine.types.vecVec3",
            .root_sep = "::"
        }),
        .type = ecs_id(flecs_vec3_t)
    });
}

ecs_entity_t flecsEngine_vecU16(
    ecs_world_t *world)
{
    return ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "::flecs.engine.types.vecU16",
            .root_sep = "::"
        }),
        .type = ecs_id(ecs_u16_t)
    });
}

ecs_entity_t flecsEngine_vecVec2(
    ecs_world_t *world)
{
    return ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "::flecs.engine.types.vecVec2",
            .root_sep = "::"
        }),
        .type = ecs_id(flecs_vec2_t)
    });
}

ecs_entity_t flecsEngine_vecU32(
    ecs_world_t *world)
{
    return ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "::flecs.engine.types.vecU32",
            .root_sep = "::"
        }),
        .type = ecs_id(ecs_u32_t)
    });
}
