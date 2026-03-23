#include "gltf.h"

#include <cgltf.h>
#include <stb_image.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

ECS_COMPONENT_DECLARE(FlecsGltf);

static char* flecsEngine_gltf_resolvePath(
    const char *gltf_path,
    const char *uri)
{
    if (!uri || !gltf_path) {
        return NULL;
    }

    /* Find directory of the gltf file */
    const char *last_slash = strrchr(gltf_path, '/');
    if (!last_slash) {
        last_slash = strrchr(gltf_path, '\\');
    }

    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - gltf_path + 1);
        size_t uri_len = strlen(uri);
        char *result = ecs_os_malloc((ecs_size_t)(dir_len + uri_len + 1));
        memcpy(result, gltf_path, dir_len);
        memcpy(result + dir_len, uri, uri_len);
        result[dir_len + uri_len] = '\0';
        return result;
    }

    return ecs_os_strdup(uri);
}

static int32_t flecsEngine_gltf_readAccessor_f32(
    const cgltf_accessor *accessor,
    float *out,
    int32_t component_count)
{
    if (!accessor) {
        return 0;
    }

    int32_t count = (int32_t)accessor->count;
    for (int32_t i = 0; i < count; i++) {
        cgltf_accessor_read_float(accessor, (cgltf_size)i,
            &out[i * component_count], (cgltf_size)component_count);
    }

    return count;
}

static int32_t flecsEngine_gltf_readIndices(
    const cgltf_accessor *accessor,
    uint32_t *out)
{
    if (!accessor) {
        return 0;
    }

    int32_t count = (int32_t)accessor->count;
    for (int32_t i = 0; i < count; i++) {
        out[i] = (uint32_t)cgltf_accessor_read_index(accessor, (cgltf_size)i);
    }

    return count;
}

static const cgltf_accessor* flecsEngine_gltf_findAttribute(
    const cgltf_primitive *prim,
    cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < prim->attributes_count; i++) {
        if (prim->attributes[i].type == type) {
            return prim->attributes[i].data;
        }
    }
    return NULL;
}

static bool flecsEngine_gltf_readMesh(
    FlecsMesh3 *mesh3,
    const cgltf_primitive *prim)
{
    const cgltf_accessor *pos_acc = flecsEngine_gltf_findAttribute(
        prim, cgltf_attribute_type_position);
    const cgltf_accessor *nrm_acc = flecsEngine_gltf_findAttribute(
        prim, cgltf_attribute_type_normal);
    const cgltf_accessor *uv_acc = flecsEngine_gltf_findAttribute(
        prim, cgltf_attribute_type_texcoord);
    const cgltf_accessor *idx_acc = prim->indices;

    if (!pos_acc || !idx_acc) {
        return false;
    }

    int32_t vert_count = (int32_t)pos_acc->count;
    int32_t idx_count = (int32_t)idx_acc->count;

    ecs_vec_init_t(NULL, &mesh3->vertices, flecs_vec3_t, vert_count);
    ecs_vec_init_t(NULL, &mesh3->normals, flecs_vec3_t, vert_count);
    ecs_vec_init_t(NULL, &mesh3->uvs, flecs_vec2_t, vert_count);
    ecs_vec_init_t(NULL, &mesh3->indices, uint32_t, idx_count);
    ecs_vec_set_count_t(NULL, &mesh3->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh3->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh3->uvs, flecs_vec2_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh3->indices, uint32_t, idx_count);

    flecsEngine_gltf_readAccessor_f32(
        pos_acc,
        (float*)ecs_vec_first_t(&mesh3->vertices, flecs_vec3_t), 3);

    if (nrm_acc) {
        flecsEngine_gltf_readAccessor_f32(
            nrm_acc,
            (float*)ecs_vec_first_t(&mesh3->normals, flecs_vec3_t), 3);
    } else {
        flecs_vec3_t *normals = ecs_vec_first_t(&mesh3->normals, flecs_vec3_t);
        for (int32_t i = 0; i < vert_count; i++) {
            normals[i] = (flecs_vec3_t){0, 1, 0};
        }
    }

    if (uv_acc) {
        flecsEngine_gltf_readAccessor_f32(
            uv_acc,
            (float*)ecs_vec_first_t(&mesh3->uvs, flecs_vec2_t), 2);
    } else {
        ecs_vec_set_count_t(NULL, &mesh3->uvs, flecs_vec2_t, 0);
    }

    flecsEngine_gltf_readIndices(
        idx_acc, ecs_vec_first_t(&mesh3->indices, uint32_t));

    return true;
}

static void flecsEngine_gltf_applyNodeTransform(
    FlecsMesh3 *mesh3,
    const float node_transform[16])
{
    int32_t vert_count = ecs_vec_count(&mesh3->vertices);
    flecs_vec3_t *positions = ecs_vec_first_t(&mesh3->vertices, flecs_vec3_t);
    flecs_vec3_t *normals = ecs_vec_first_t(&mesh3->normals, flecs_vec3_t);
    const float *m = node_transform;

    for (int32_t vi = 0; vi < vert_count; vi++) {
        float x = positions[vi].x;
        float y = positions[vi].y;
        float z = positions[vi].z;
        positions[vi].x = m[0]*x + m[4]*y + m[8]*z  + m[12];
        positions[vi].y = m[1]*x + m[5]*y + m[9]*z  + m[13];
        positions[vi].z = m[2]*x + m[6]*y + m[10]*z + m[14];

        x = normals[vi].x;
        y = normals[vi].y;
        z = normals[vi].z;
        float nx = m[0]*x + m[4]*y + m[8]*z;
        float ny = m[1]*x + m[5]*y + m[9]*z;
        float nz = m[2]*x + m[6]*y + m[10]*z;
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (len > 0.0f) {
            normals[vi].x = nx / len;
            normals[vi].y = ny / len;
            normals[vi].z = nz / len;
        }
    }
}

static ecs_entity_t flecsEngine_gltf_getImageEntity(
    ecs_world_t *world,
    ecs_entity_t *image_entities,
    const cgltf_data *data,
    const cgltf_texture_view *tv,
    const char *gltf_path)
{
    if (!tv->texture || !tv->texture->image || !tv->texture->image->uri) {
        return 0;
    }

    ptrdiff_t idx = tv->texture->image - data->images;
    if (image_entities[idx]) {
        return image_entities[idx];
    }

    char *path = flecsEngine_gltf_resolvePath(
        gltf_path, tv->texture->image->uri);
    if (!path) {
        return 0;
    }

    ecs_entity_t assets = ecs_lookup(world, "assets");
    if (!assets) {
        assets = ecs_entity(world, { .name = "assets" });
        ecs_add_id(world, assets, EcsModule);
    }

    ecs_entity_t e = ecs_entity(world, {
        .parent = assets,
        .name = path,
        .sep = "/"
    });
    ecs_add_id(world, e, EcsPrefab);
    ecs_set(world, e, FlecsTexture, { .path = path });
    ecs_os_free(path);

    image_entities[idx] = e;
    return e;
}

static void flecsEngine_gltf_setupMaterial(
    ecs_world_t *world,
    ecs_entity_t entity,
    const cgltf_material *mat,
    const char *gltf_path,
    ecs_entity_t *image_entities,
    const cgltf_data *data)
{
    float em_r = mat->emissive_factor[0];
    float em_g = mat->emissive_factor[1];
    float em_b = mat->emissive_factor[2];
    float em_max = fmaxf(em_r, fmaxf(em_g, em_b));
    flecs_rgba_t em_color = {0};
    if (em_max > 0.0f) {
        em_color.r = (uint8_t)(em_r / em_max * 255.0f);
        em_color.g = (uint8_t)(em_g / em_max * 255.0f);
        em_color.b = (uint8_t)(em_b / em_max * 255.0f);
        em_color.a = 255;
    }
    ecs_set(world, entity, FlecsEmissive, {
        .strength = em_max,
        .color = em_color
    });

    FlecsPbrTextures tex = {0};

    const cgltf_texture_view *albedo_tv = NULL;
    const cgltf_texture_view *roughness_tv = NULL;

    if (mat->has_pbr_specular_glossiness) {
        const cgltf_pbr_specular_glossiness *sg =
            &mat->pbr_specular_glossiness;

        uint8_t r = (uint8_t)(sg->diffuse_factor[0] * 255.0f);
        uint8_t g = (uint8_t)(sg->diffuse_factor[1] * 255.0f);
        uint8_t b = (uint8_t)(sg->diffuse_factor[2] * 255.0f);
        uint8_t a = (uint8_t)(sg->diffuse_factor[3] * 255.0f);
        ecs_set(world, entity, FlecsRgba, { r, g, b, a });

        ecs_set(world, entity, FlecsPbrMaterial, {
            .metallic = 0.0f,
            .roughness = 1.0f - sg->glossiness_factor
        });

        albedo_tv = &sg->diffuse_texture;
        roughness_tv = &sg->specular_glossiness_texture;
    } else {
        const cgltf_pbr_metallic_roughness *pbr =
            &mat->pbr_metallic_roughness;

        uint8_t r = (uint8_t)(pbr->base_color_factor[0] * 255.0f);
        uint8_t g = (uint8_t)(pbr->base_color_factor[1] * 255.0f);
        uint8_t b = (uint8_t)(pbr->base_color_factor[2] * 255.0f);
        uint8_t a = (uint8_t)(pbr->base_color_factor[3] * 255.0f);
        ecs_set(world, entity, FlecsRgba, { r, g, b, a });

        ecs_set(world, entity, FlecsPbrMaterial, {
            .metallic = pbr->metallic_factor,
            .roughness = pbr->roughness_factor
        });

        albedo_tv = &pbr->base_color_texture;
        roughness_tv = &pbr->metallic_roughness_texture;
    }

    tex.albedo = flecsEngine_gltf_getImageEntity(
        world, image_entities, data, albedo_tv, gltf_path);
    tex.emissive = flecsEngine_gltf_getImageEntity(
        world, image_entities, data, &mat->emissive_texture, gltf_path);
    tex.roughness = flecsEngine_gltf_getImageEntity(
        world, image_entities, data, roughness_tv, gltf_path);
    tex.normal = flecsEngine_gltf_getImageEntity(
        world, image_entities, data, &mat->normal_texture, gltf_path);

    bool has_textures = tex.albedo || tex.emissive ||
                        tex.roughness || tex.normal;
    if (has_textures) {
        ecs_set_ptr(world, entity, FlecsPbrTextures, &tex);
    }
}

static void flecsEngine_gltf_setupPrimitive(
    ecs_world_t *world,
    ecs_entity_t entity,
    const cgltf_primitive *prim,
    const char *gltf_path,
    const float node_transform[16],
    ecs_entity_t *image_entities,
    const cgltf_data *data)
{
    FlecsMesh3 mesh3 = {0};
    if (!flecsEngine_gltf_readMesh(&mesh3, prim)) {
        return;
    }

    flecsEngine_gltf_applyNodeTransform(&mesh3, node_transform);

    ecs_set_ptr(world, entity, FlecsMesh3, &mesh3);

    if (prim->material) {
        flecsEngine_gltf_setupMaterial(
            world, entity, prim->material, gltf_path,
            image_entities, data);
    }
}

static void flecsEngine_gltf_load(
    ecs_world_t *world,
    ecs_entity_t root,
    const char *path)
{
    cgltf_options options = {0};
    cgltf_data *data = NULL;

    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        ecs_err("failed to parse GLTF file: %s", path);
        return;
    }

    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        ecs_err("failed to load GLTF buffers: %s", path);
        cgltf_free(data);
        return;
    }

    /* Pre-allocate image entity lookup table for texture deduplication */
    ecs_entity_t *image_entities = NULL;
    if (data->images_count) {
        image_entities = ecs_os_calloc_n(
            ecs_entity_t, (int32_t)data->images_count);
    }

    /* Count total triangle primitives across all nodes */
    int32_t prim_count = 0;
    for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;
        const cgltf_mesh *mesh = node->mesh;
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            if (mesh->primitives[pi].type == cgltf_primitive_type_triangles) {
                const cgltf_accessor *pos = flecsEngine_gltf_findAttribute(
                    &mesh->primitives[pi], cgltf_attribute_type_position);
                if (pos && mesh->primitives[pi].indices) {
                    prim_count++;
                }
            }
        }
    }

    if (prim_count == 1) {
        /* Single primitive: set mesh/material directly on root */
        for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
            const cgltf_node *node = &data->nodes[ni];
            if (!node->mesh) continue;

            float transform[16];
            cgltf_node_transform_world(node, transform);

            const cgltf_mesh *mesh = node->mesh;
            for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
                if (mesh->primitives[pi].type == cgltf_primitive_type_triangles) {
                    flecsEngine_gltf_setupPrimitive(
                        world, root, &mesh->primitives[pi], path, transform,
                        image_entities, data);
                }
            }
        }
    } else {
        /* Multiple primitives: create child prefab per primitive */
        for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
            const cgltf_node *node = &data->nodes[ni];
            if (!node->mesh) continue;

            float transform[16];
            cgltf_node_transform_world(node, transform);

            const cgltf_mesh *mesh = node->mesh;
            for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
                if (mesh->primitives[pi].type != cgltf_primitive_type_triangles) {
                    continue;
                }

                ecs_entity_t mesh_e = ecs_new_w_parent(world, root, NULL);
                ecs_add_id(world, mesh_e, EcsPrefab);
                ecs_set(world, mesh_e, FlecsPosition3, {0, 0, 0});
                flecsEngine_gltf_setupPrimitive(
                    world, mesh_e, &mesh->primitives[pi], path, transform,
                    image_entities, data);
            }
        }
    }

    ecs_os_free(image_entities);
    cgltf_free(data);

    ecs_dbg("loaded GLTF: %s", path);
}

static void FlecsGltf_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsGltf *gltf = ecs_field(it, FlecsGltf, 0);

    for (int i = 0; i < it->count; i++) {
        ecs_entity_t e = it->entities[i];
        if (!gltf[i].file) {
            continue;
        }

        flecsEngine_gltf_load(world, e, gltf[i].file);
    }
}

void FlecsEngineGltfImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineGltf);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsGltf);
    ecs_struct(world, {
        .entity = ecs_id(FlecsGltf),
        .members = {
            { .name = "file", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_add_pair(world, ecs_id(FlecsGltf), EcsOnInstantiate, EcsInherit);

    ecs_set_hooks(world, FlecsGltf, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsGltf_on_set
    });
}
