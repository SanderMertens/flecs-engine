# Flecs engine

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

For asan:
```sh
cmake --build build-asan
./build-asan/flecs_engine
```

If your `wgpu-native` install doesn't provide `webgpu.h`, adjust the include in
`src/main.c`.
