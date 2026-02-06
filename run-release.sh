#!/usr/bin/env sh
set -eu

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel 8
./build-release/flecs_engine "$@"
