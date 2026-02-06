#!/usr/bin/env sh
set -eu

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
./build/flecs_engine "$@"
