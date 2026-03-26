# Flecs engine
A fast, portable, low footprint, opinionated (but hackable), flecs native game engine.

The project is still WIP and currently only works on MacOS.

## Usage
Build & run the engine:
```sh
cmake -S . -B build
cmake --build build
./build/flecs_engine
```

## Why should I use this?
You should probably not use this, unless:
- you want to quickly prototype ideas
- you want to build an engine but not start from 0
- you're OK with the limitations of the engine

## Features
- Primitive shapes
- Meshes
- GLTF loader
- Movement systems
- Instancing
- PBR materials
- Transparency
- Directional light
- Pointlights
- Spotlights
- Clustered light rendering
- Cascading shadow maps
- Frustum culling
- Image based lighting (support for .exr and .hdr)
- Skybox
- Bloom
- Height based fog
- Tony McMapFace tone mapping
- MSAA
- Infinite grid
- Infinite plane
- Input handling
- Camera controller
- Data-driven engine setup
- Rendering to image

## Dependencies
- cglm
- cgltf
- glfw
- tinyexr
- stb_image

## Assets used
- [Kronos sample assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
- [Niagra bistro](https://github.com/zeux/niagara_bistro)
- [Kenney](https://kenney.nl/)

## Screenshots
<img width="600" height="380" alt="Screenshot 2026-03-26 at 2 18 50 AM" src="https://github.com/user-attachments/assets/bd1069e6-94fa-4a50-8e70-494cb006b164" />

<img width="600" height="379" alt="Screenshot 2026-03-26 at 2 21 08 AM" src="https://github.com/user-attachments/assets/0200713b-909b-47a8-b940-39fa87899656" />

<img width="600" height="379" alt="Screenshot 2026-03-26 at 2 24 22 AM" src="https://github.com/user-attachments/assets/c0dbd260-6e20-4f39-9790-580fac062d82" />

<img width="600" height="378" alt="Screenshot 2026-03-26 at 2 26 36 AM" src="https://github.com/user-attachments/assets/c278f8ed-80c4-4a49-b04a-31e4334e35b1" />

<img width="600" height="380" alt="Screenshot 2026-03-26 at 2 30 26 AM" src="https://github.com/user-attachments/assets/c4886e12-3539-467e-b054-2d463595b3bd" />
