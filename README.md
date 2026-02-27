# Flecs engine

## Batching model
- Scalable primitives (`FlecsBox`) use a shared unit mesh asset and apply primitive size in per-instance transform data.
- Non-scalable primitives (`FlecsSphere`, `FlecsCylinder`) use a geometry cache keyed by tessellation settings (`segments`) and batch instances per cached asset.
- Mesh assets are rendered by grouping instances with `(IsA, asset)`.
- Direct meshes (`FlecsMesh3` on a renderable entity) are auto-promoted to internal assets so they can use the same `IsA` batching path.
- Color resolution is `instance FlecsRgba` override, then inherited asset `FlecsRgba`, then white fallback.

## Dependencies (macOS)
- `wgpu-native` (provides `webgpu.h` + `libwgpu_native`)
- `glfw`
- `pkg-config`

## Build
```sh
cmake -S . -B build
cmake --build build
./build/flecs_engine
```

Single-frame image output mode (no window):
```sh
./build/flecs_engine --frame-out /tmp/frame.ppm --size 1280x800
```

For asan:
```sh
cmake --build build-asan
./build-asan/flecs_engine
```

If your `wgpu-native` install doesn't provide `webgpu.h`, adjust the include in
`src/main.c`.
