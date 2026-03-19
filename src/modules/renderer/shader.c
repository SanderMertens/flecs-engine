#include "renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsShader);
ECS_COMPONENT_DECLARE(FlecsShaderImpl);

WGPUShaderModule flecsEngine_createShaderModule(
    WGPUDevice device,
    const char *wgsl_source)
{
    WGPUShaderSourceWGSL wgsl_desc = {
        .chain = {
            .sType = WGPUSType_ShaderSourceWGSL
        },
        .code = (WGPUStringView){
            .data = wgsl_source,
            .length = WGPU_STRLEN
        }
    };

    return wgpuDeviceCreateShaderModule(
        device, &(WGPUShaderModuleDescriptor){
            .nextInChain = (WGPUChainedStruct *)&wgsl_desc
        });
}

static void flecsShaderImplRelease(
    FlecsShaderImpl *ptr)
{
    if (ptr->shader_module) {
        wgpuShaderModuleRelease(ptr->shader_module);
        ptr->shader_module = NULL;
    }
}

ECS_DTOR(FlecsShaderImpl, ptr, {
    flecsShaderImplRelease(ptr);
})

ECS_MOVE(FlecsShaderImpl, dst, src, {
    flecsShaderImplRelease(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static bool flecsEngine_shader_compile(
    ecs_world_t *world,
    ecs_entity_t shader_entity,
    const FlecsShader *shader,
    FlecsShaderImpl *shader_impl)
{
    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    if (!engine) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("cannot compile shader '%s': engine is not initialized", name);
        ecs_os_free(name);
        return false;
    }

    if (!shader || !shader->source) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("shader asset '%s' has no source", name);
        ecs_os_free(name);
        flecsShaderImplRelease(shader_impl);
        return false;
    }

    flecsShaderImplRelease(shader_impl);

    shader_impl->shader_module = flecsEngine_createShaderModule(
        engine->device, shader->source);

    if (!shader_impl->shader_module) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("failed to compile shader asset '%s'", name);
        ecs_os_free(name);
        return false;
    }

    return true;
}

const FlecsShaderImpl* flecsEngine_shader_ensureImpl(
    ecs_world_t *world,
    ecs_entity_t shader_entity)
{
    const FlecsShader *shader = ecs_get(world, shader_entity, FlecsShader);
    if (!shader) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("entity '%s' does not have FlecsShader", name);
        ecs_os_free(name);
        return NULL;
    }

    FlecsShaderImpl *shader_impl = ecs_ensure(world, shader_entity, FlecsShaderImpl);
    if (!shader_impl->shader_module) {
        if (!flecsEngine_shader_compile(world, shader_entity, shader, shader_impl)) {
            return NULL;
        }
    }

    return shader_impl;
}

ecs_entity_t flecsEngine_shader_ensure(
    ecs_world_t *world,
    const char *name,
    const FlecsShader *shader)
{
    ecs_entity_t renderer_module = ecs_lookup(world, "flecs.engine.renderer");
    ecs_entity_t shader_entity = ecs_entity_init(world, &(ecs_entity_desc_t){
        .name = name,
        .parent = renderer_module
    });

    const FlecsShader *existing = ecs_get(world, shader_entity, FlecsShader);
    if (!existing ||
        ecs_os_strcmp(existing->source, shader->source) ||
        ecs_os_strcmp(existing->vertex_entry, shader->vertex_entry) ||
        ecs_os_strcmp(existing->fragment_entry, shader->fragment_entry))
    {
        ecs_set_ptr(world, shader_entity, FlecsShader, shader);
    }

    return shader_entity;
}

static void FlecsShader_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsShader *shaders = ecs_field(it, FlecsShader, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t shader_entity = it->entities[i];
        FlecsShaderImpl *shader_impl = ecs_ensure(world, shader_entity, FlecsShaderImpl);
        if (!flecsEngine_shader_compile(world, shader_entity, &shaders[i], shader_impl)) {
            continue;
        }
    }
}

void flecsEngine_shader_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsShader);
    ECS_COMPONENT_DEFINE(world, FlecsShaderImpl);

    ecs_set_hooks(world, FlecsShader, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsShader_on_set
    });

    ecs_set_hooks(world, FlecsShaderImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsShaderImpl),
        .dtor = ecs_dtor(FlecsShaderImpl)
    });
}