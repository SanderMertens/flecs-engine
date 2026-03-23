cmake_minimum_required(VERSION 3.20)
project(flecs_engine C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if(NOT EMSCRIPTEN)
  include(ExternalProject)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WGPU QUIET wgpu-native)
endif()

# -- GLFW (native only; emscripten uses -sUSE_GLFW=3) --
if(NOT EMSCRIPTEN)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.9
  )
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
endif()

# -- cglm --
FetchContent_Declare(
  cglm
  GIT_REPOSITORY https://github.com/recp/cglm.git
  GIT_TAG v0.9.2
)
set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cglm)

# -- flecs --
FetchContent_Declare(
  flecs
  GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
  GIT_TAG master
)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(flecs)
target_compile_definitions(flecs_static PUBLIC FLECS_SCRIPT_MATH)

# -- stb (header-only, no CMakeLists.txt) --
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 28d546d5eb77d4585506a20480f4de2e706dff4c
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(stb)

# -- tinyexr (we compile it ourselves) --
FetchContent_Declare(
  tinyexr
  GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
  GIT_TAG v1.0.9
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(tinyexr)

# -- cgltf (header-only) --
FetchContent_Declare(
  cgltf
  GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
  GIT_TAG v1.15
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(cgltf)

# -- Sources --
if(EMSCRIPTEN)
  # Exclude Objective-C (.m) files on emscripten
  file(GLOB_RECURSE FLECS_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  )
else()
  file(GLOB_RECURSE FLECS_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m"
  )
endif()

# -- tinyexr implementation + miniz --
list(APPEND FLECS_ENGINE_SOURCES
  ${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/deps.cc
)

add_executable(flecs_engine ${FLECS_ENGINE_SOURCES})

if(NOT EMSCRIPTEN)
  target_compile_definitions(flecs_engine PRIVATE GLFW_EXPOSE_NATIVE_COCOA)
endif()

# -- wgpu (native only; emscripten uses -sUSE_WEBGPU=1) --
if(NOT EMSCRIPTEN)
  if(WGPU_FOUND)
    target_include_directories(flecs_engine PRIVATE ${WGPU_INCLUDE_DIRS})
    target_link_directories(flecs_engine PRIVATE ${WGPU_LIBRARY_DIRS})
    target_link_libraries(flecs_engine PRIVATE ${WGPU_LIBRARIES})
  else()
    set(WGPU_NATIVE_DIR ${CMAKE_BINARY_DIR}/_deps/wgpu_native-src)
    set(WGPU_NATIVE_LIB ${WGPU_NATIVE_DIR}/target/release/libwgpu_native.dylib)

    ExternalProject_Add(
      wgpu_native_ep
      GIT_REPOSITORY https://github.com/gfx-rs/wgpu-native.git
      GIT_TAG v27.0.4.0
      UPDATE_COMMAND ""
      CONFIGURE_COMMAND ""
      BUILD_COMMAND cargo build --release
      BUILD_IN_SOURCE 1
      INSTALL_COMMAND ""
      SOURCE_DIR ${WGPU_NATIVE_DIR}
      BUILD_BYPRODUCTS ${WGPU_NATIVE_LIB}
    )

    add_library(wgpu_native_lib SHARED IMPORTED)
    set_target_properties(wgpu_native_lib PROPERTIES IMPORTED_LOCATION ${WGPU_NATIVE_LIB})
    add_dependencies(wgpu_native_lib wgpu_native_ep)

    target_include_directories(flecs_engine PRIVATE
      ${WGPU_NATIVE_DIR}/ffi
      ${WGPU_NATIVE_DIR}/ffi/webgpu-headers
    )
    target_link_libraries(flecs_engine PRIVATE wgpu_native_lib)
  endif()
endif()

target_include_directories(flecs_engine PUBLIC
  ${flecs_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(flecs_engine PRIVATE
  ${stb_SOURCE_DIR}
  ${tinyexr_SOURCE_DIR}
  ${tinyexr_SOURCE_DIR}/deps/miniz
  ${cgltf_SOURCE_DIR}
)

if(EMSCRIPTEN)
  target_link_libraries(flecs_engine PRIVATE cglm flecs_static)

  target_link_options(flecs_engine PRIVATE
    -sUSE_GLFW=3
    -sUSE_WEBGPU=1
    -sALLOW_MEMORY_GROWTH=1
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    "-sASYNCIFY_IMPORTS=['emscripten_sleep']"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/city.flecs@city.flecs"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/cube.flecs@cube.flecs"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/empty.flecs@empty.flecs"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/museum.flecs@museum.flecs"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
  )

  set_target_properties(flecs_engine PROPERTIES
    SUFFIX ".html"
  )
else()
  target_link_libraries(flecs_engine PRIVATE glfw cglm flecs_static)

  if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    target_link_libraries(flecs_engine PRIVATE
      ${COCOA_FRAMEWORK}
      ${QUARTZCORE_FRAMEWORK}
    )
  endif()
endif()
