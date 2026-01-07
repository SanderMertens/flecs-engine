cmake_minimum_required(VERSION 3.20)
project(flecs_engine C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
include(ExternalProject)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WGPU QUIET wgpu-native)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.9
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

add_executable(flecs_engine src/main.c src/macos_surface.m)
target_compile_definitions(flecs_engine PRIVATE GLFW_EXPOSE_NATIVE_COCOA)

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

target_link_libraries(flecs_engine PRIVATE glfw)

if(APPLE)
  find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
  find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
  target_link_libraries(flecs_engine PRIVATE ${COCOA_FRAMEWORK} ${QUARTZCORE_FRAMEWORK})
endif()
