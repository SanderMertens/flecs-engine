#ifndef FLECS_ENGINE_GEOMETRY_PRIMITIVES3_H
#define FLECS_ENGINE_GEOMETRY_PRIMITIVES3_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_GEOMETRY_PRIMITIVES3_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsBox, {
    float x;
    float y;
    float z;
});

ECS_STRUCT(FlecsPyramid, {
    float x;
    float y;
    float z;
});

ECS_STRUCT(FlecsQuad, {
    float x;
    float y;
});

ECS_STRUCT(FlecsTriangle, {
    float x;
    float y;
});

ECS_STRUCT(FlecsRightTriangle, {
    float x;
    float y;
});

ECS_STRUCT(FlecsTrianglePrism, {
    float x;
    float y;
    float z;
});

ECS_STRUCT(FlecsRightTrianglePrism, {
    float x;
    float y;
    float z;
});

ECS_STRUCT(FlecsSphere, {
    int32_t segments;
    bool smooth;
    float radius;
});

ECS_STRUCT(FlecsNGon, {
    int32_t sides;
});

ECS_STRUCT(FlecsCylinder, {
    int32_t segments;
    bool smooth;
    float length;
});

#endif
