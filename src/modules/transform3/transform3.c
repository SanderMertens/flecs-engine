#define FLECS_ENGINE_TRANSFORM3_IMPL
#include "transform3.h"

ECS_COMPONENT_DECLARE(FlecsPosition3);
ECS_COMPONENT_DECLARE(FlecsRotation3);
ECS_COMPONENT_DECLARE(FlecsScale3);

typedef struct {
    ecs_query_t *q_childof;
    ecs_query_t *q_parent;
} flecs_transform3_queries_t;

static void flecsTransform3QueriesFree(void *ptr) {
    ecs_os_free(ptr);
}

static void flecsTransform3RotationAndScale(
    ecs_iter_t *it,
    FlecsWorldTransform3 *t) 
{
    FlecsRotation3 *r = ecs_field(it, FlecsRotation3, 2);
    FlecsScale3 *s = ecs_field(it, FlecsScale3, 3);
    int i;

    if (r) {
        if (ecs_field_is_self(it, 2)) {
            for (i = 0; i < it->count; i ++) {
                glm_rotate(t[i].m, r[i].x, (vec3){1.0, 0.0, 0.0});
                glm_rotate(t[i].m, r[i].y, (vec3){0.0, 1.0, 0.0});
                glm_rotate(t[i].m, r[i].z, (vec3){0.0, 0.0, 1.0});
            }
        } else {
            for (i = 0; i < it->count; i ++) {
                glm_rotate(t[i].m, r->x, (vec3){1.0, 0.0, 0.0});
                glm_rotate(t[i].m, r->y, (vec3){0.0, 1.0, 0.0});
                glm_rotate(t[i].m, r->z, (vec3){0.0, 0.0, 1.0});
            }
        }
    }

    if (s) {
        for (i = 0; i < it->count; i ++) {
            glm_scale(t[i].m, *(vec3*)&s[i]);
        }
    }
}

static bool flecsTransform3ChildOf(ecs_iter_t *it) {
    bool has_results = false;

    while (ecs_query_next(it)) {
        FlecsWorldTransform3 *t = ecs_field(it, FlecsWorldTransform3, 0);
        FlecsPosition3 *p = ecs_field(it, FlecsPosition3, 1);
        FlecsWorldTransform3 *t_parent = ecs_field(it, FlecsWorldTransform3, 4);
        int i;

        if (!t_parent) {
            if (ecs_field_is_self(it, 1)) {
                for (i = 0; i < it->count; i ++) {
                    glm_translate_make(t[i].m, *(vec3*)&p[i]);
                }
            } else {
                for (i = 0; i < it->count; i ++) {
                    glm_translate_make(t[i].m, *(vec3*)p);
                }
            }
        } else {
            if (ecs_field_is_self(it, 1)) {
                for (i = 0; i < it->count; i ++) {
                    glm_translate_to(t_parent[0].m, *(vec3*)&p[i], t[i].m);
                }
            } else {
                for (i = 0; i < it->count; i ++) {
                    glm_translate_to(t_parent[0].m, *(vec3*)p, t[i].m);
                }
            }
        }

        flecsTransform3RotationAndScale(it, t);

        has_results = true;
    }

    return has_results;
}

static bool flecsTransform3Parent(ecs_iter_t *it) {
    bool has_results = false;
    ecs_world_t *world = it->world;

    while (ecs_query_next(it)) {
        FlecsWorldTransform3 *t = ecs_field(it, FlecsWorldTransform3, 0);
        FlecsPosition3 *p = ecs_field(it, FlecsPosition3, 1);
        EcsParent *parents = ecs_field(it, EcsParent, 4);
        int i;

        for (i = 0; i < it->count; i ++) {
            ecs_entity_t parent = parents[i].value;
            const FlecsWorldTransform3 *t_parent = ecs_get_mut(
                world, parent, FlecsWorldTransform3);

            while (!t_parent) {
                parent = ecs_get_parent(world, parent);
                if (!parent) {
                    break;
                }
                t_parent = ecs_get_mut(world, parent, FlecsWorldTransform3);
            }

            if (!t_parent) {
                if (ecs_field_is_self(it, 2)) {
                    glm_translate_make((vec4*)t[i].m, *(vec3*)&p[i]);
                } else {
                    glm_translate_make((vec4*)t[i].m, *(vec3*)p);
                }
            } else {
                if (ecs_field_is_self(it, 2)) {
                    glm_translate_to((vec4*)t_parent[0].m, *(vec3*)&p[i], t[i].m);
                } else {
                    glm_translate_to((vec4*)t_parent[0].m, *(vec3*)p, t[i].m);
                }
            }
        }

        flecsTransform3RotationAndScale(it, t);

        has_results = true;
    }

    return has_results;
}

static void FlecsTransform3(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    flecs_transform3_queries_t *ctx = it->ctx;

    for (int depth = 0; depth < 256; depth ++) {
        bool has_results = false;

        {
            ecs_iter_t it = ecs_query_iter(world, ctx->q_childof);
            ecs_iter_set_group(&it, depth);
            has_results |= flecsTransform3ChildOf(&it);
        } {
            ecs_iter_t it = ecs_query_iter(world, ctx->q_parent);
            ecs_iter_set_group(&it, depth);
            has_results |= flecsTransform3Parent(&it); 
        }

        if (!has_results) {
            break;
        }
    }
}

void FlecsEngineTransform3Import(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTransform3);

    ecs_set_name_prefix(world, "Flecs");
    
    ECS_COMPONENT_DEFINE(world, FlecsPosition3);
    ECS_COMPONENT_DEFINE(world, FlecsRotation3);
    ECS_COMPONENT_DEFINE(world, FlecsScale3);
    ECS_META_COMPONENT(world, FlecsWorldTransform3);

    ecs_add_pair(world, ecs_id(FlecsPosition3), EcsWith, ecs_id(FlecsWorldTransform3));
    ecs_add_pair(world, ecs_id(FlecsRotation3), EcsWith, ecs_id(FlecsWorldTransform3));
    ecs_add_pair(world, ecs_id(FlecsScale3),    EcsWith, ecs_id(FlecsWorldTransform3));

    ecs_query_desc_t q_childof = {
        .entity = ecs_entity(world, { .name = "Transform3ChildOf" }),
        .terms = {{ 
            .id = ecs_id(FlecsWorldTransform3),
            .inout = EcsOut,
        }, {
            .id = ecs_id(FlecsPosition3),
            .inout = EcsIn
        }, {
            .id = ecs_id(FlecsRotation3),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(FlecsScale3),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(FlecsWorldTransform3), 
            .inout = EcsIn,
            .oper = EcsOptional,
            .src.id = EcsCascade
        }, {
            .id = ecs_id(EcsParent),
            .oper = EcsNot
        }},
        .cache_kind = EcsQueryCacheAuto
    };

    ecs_query_desc_t q_parent = {
        .entity = ecs_entity(world, { .name = "Transform3Parent" }),
        .terms = {{ 
            .id = ecs_id(FlecsWorldTransform3),
            .inout = EcsOut,
        }, {
            .id = ecs_id(FlecsPosition3),
            .inout = EcsIn
        }, {
            .id = ecs_id(FlecsRotation3),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(FlecsScale3),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(EcsParent), 
            .inout = EcsIn
        }},
        .group_by = EcsParentDepth,
        .cache_kind = EcsQueryCacheAuto
    };

    q_parent.group_by = EcsParentDepth;

    flecs_transform3_queries_t *ctx = ecs_os_malloc_t(flecs_transform3_queries_t);
    ctx->q_childof = ecs_query_init(world, &q_childof);
    ctx->q_parent = ecs_query_init(world, &q_parent);

    ecs_system(world, {
        .entity = ecs_entity(world, { 
            .name = "Transform3",
        }),
        .phase = EcsPreStore,
        .run = FlecsTransform3,
        .ctx = ctx,
        .ctx_free = flecsTransform3QueriesFree
    });
}
