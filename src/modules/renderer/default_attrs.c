#include "renderer.h"

FlecsDefaultAttrCache* flecsEngine_defaultAttrCache_create(void) {
    return ecs_os_calloc_t(FlecsDefaultAttrCache);
}

void flecsEngine_defaultAttrCache_free(
    FlecsDefaultAttrCache *ptr)
{
    ecs_os_free(ptr->color);
    ecs_os_free(ptr->material);
    ecs_os_free(ptr->emissive);
    ecs_os_free(ptr);
}

FlecsRgba* flecsEngine_defaultAttrCache_getColor(
    const FlecsEngineImpl *engine,
    int32_t count)
{
    FlecsDefaultAttrCache *cache = engine->default_attr_cache;
    if (count > cache->color_count) {
        ecs_os_free(cache->color);
        cache->color = ecs_os_malloc_n(FlecsRgba, count);
        cache->color_count = count;

        for (int i = 0; i < count; i ++) {
            cache->color[i] = (FlecsRgba){230, 230, 230};
        }
    }

    return cache->color;
}

FlecsPbrMaterial* flecsEngine_defaultAttrCache_getMaterial(
    const FlecsEngineImpl *engine,
    int32_t count)
{
    FlecsDefaultAttrCache *cache = engine->default_attr_cache;
    if (count > cache->material_count) {
        ecs_os_free(cache->material);
        cache->material = ecs_os_malloc_n(FlecsPbrMaterial, count);
        cache->material_count = count;

        for (int i = 0; i < count; i ++) {
            cache->material[i] = (FlecsPbrMaterial){
                .metallic = 0, .roughness = 1
            };
        }
    }

    return cache->material;
}

FlecsEmissive* flecsEngine_defaultAttrCache_getEmissive(
    const FlecsEngineImpl *engine,
    int32_t count)
{
    FlecsDefaultAttrCache *cache = engine->default_attr_cache;
    if (count > cache->emissive_count) {
        ecs_os_free(cache->emissive);
        cache->emissive = ecs_os_calloc_n(FlecsEmissive, count);
        cache->emissive_count = count;
    }
    return cache->emissive;
}
