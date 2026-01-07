# wgpu rotating cube (C)

This is a minimal wgpu-native + GLFW project that renders a rotating cube on
macOS using the C binding.

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

If your `wgpu-native` install doesn't provide `webgpu.h`, adjust the include in
`src/main.c`.
