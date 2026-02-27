# Flecs engine
A fast, low footprint, opinionated, flecs native game engine.

## Usage
Build & run the engine:
```sh
cmake -S . -B build
cmake --build build
./build/flecs_engine
```

Write frame to file:
```sh
./build/flecs_engine --frame-out /tmp/frame.ppm --size 1280x800
```
