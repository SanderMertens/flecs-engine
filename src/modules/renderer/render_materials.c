#include <limits.h>
#include <string.h>

#include "renderer.h"
#include "flecs_engine.h"

static void flecsEngineEnsureMaterialBufferCapacity(
    FlecsEngineImpl *impl,
    uint32_t required_count)
{
    if (!required_count) {
        return;
    }

    if (impl->material_buffer &&
        impl->cpu_materials &&
        required_count <= impl->material_buffer_capacity)
    {
        return;
    }

    uint32_t new_capacity = impl->material_buffer_capacity;
    if (!new_capacity) {
        new_capacity = 64;
    }

    while (new_capacity < required_count) {
        if (new_capacity > (UINT32_MAX / 2u)) {
            new_capacity = required_count;
            break;
        }
        new_capacity *= 2u;
    }

    FlecsGpuMaterial *new_cpu_materials =
        ecs_os_malloc_n(FlecsGpuMaterial, new_capacity);
    ecs_assert(new_cpu_materials != NULL, ECS_OUT_OF_MEMORY, NULL);

    WGPUBuffer new_material_buffer = wgpuDeviceCreateBuffer(
        impl->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsGpuMaterial)
        });
    ecs_assert(new_material_buffer != NULL, ECS_INTERNAL_ERROR, NULL);

    if (impl->material_buffer) {
        wgpuBufferRelease(impl->material_buffer);
    }

    ecs_os_free(impl->cpu_materials);

    impl->material_buffer = new_material_buffer;
    impl->cpu_materials = new_cpu_materials;
    impl->material_buffer_capacity = new_capacity;
}

void flecsEngineReleaseMaterialBuffer(
    FlecsEngineImpl *impl)
{
    if (impl->material_buffer) {
        wgpuBufferRelease(impl->material_buffer);
        impl->material_buffer = NULL;
    }

    ecs_os_free(impl->cpu_materials);
    impl->cpu_materials = NULL;

    impl->material_buffer_capacity = 0;
    impl->material_count = 0;
}

void flecsEngineUploadMaterialBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    impl->material_count = 0;
    if (!impl->material_query || !impl->queue) {
        return;
    }

redo: {
        uint32_t required_count = 0;
        uint32_t capacity = impl->material_buffer_capacity;

        if (capacity) {
            ecs_os_memset_n(
                impl->cpu_materials, 0, FlecsGpuMaterial, capacity);
        }

        ecs_iter_t it = ecs_query_iter(world, impl->material_query);
        while (ecs_query_next(&it)) {
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 0);
            const FlecsPbrMaterial *materials =
                ecs_field(&it, FlecsPbrMaterial, 1);
            const FlecsMaterialId *material_ids =
                ecs_field(&it, FlecsMaterialId, 2);
            const FlecsEmissive *emissives =
                ecs_field(&it, FlecsEmissive, 3);

            for (int32_t i = 0; i < it.count; i ++) {
                uint32_t index = material_ids[i].value;
                if ((index + 1u) > required_count) {
                    required_count = index + 1u;
                }

                if (index >= capacity) {
                    continue;
                }

                FlecsEmissive emissive = {0};
                if (emissives) {
                    emissive = emissives[i];
                }

                impl->cpu_materials[index] = (FlecsGpuMaterial){
                    .color = colors[i],
                    .metallic = materials[i].metallic,
                    .roughness = materials[i].roughness,
                    .emissive_color = emissive.color,
                    .emissive_strength = emissive.strength,
                    ._pad = {0.0f, 0.0f, 0.0f}
                };
            }
        }

        if (required_count > capacity) {
            flecsEngineEnsureMaterialBufferCapacity(impl, required_count);
            goto redo;
        }

        if (!required_count) {
            return;
        }

        wgpuQueueWriteBuffer(
            impl->queue,
            impl->material_buffer,
            0,
            impl->cpu_materials,
            (uint64_t)required_count * sizeof(FlecsGpuMaterial));
        impl->material_count = required_count;
    }
}
