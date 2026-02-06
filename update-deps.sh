#!/usr/bin/env sh
set -eu

configure_build_dir() {
  build_dir="$1"

  case "$build_dir" in
    build)
      cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug
      ;;
    build-release)
      cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
      ;;
    build-asan)
      cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
      ;;
    *)
      cmake -S . -B "$build_dir"
      ;;
  esac
}

update_repo() {
  repo_dir="$1"

  if [ ! -d "$repo_dir/.git" ]; then
    return 0
  fi

  printf "Updating %s\n" "$repo_dir"
  git -C "$repo_dir" fetch --all --tags --prune

  branch="$(git -C "$repo_dir" symbolic-ref --short -q HEAD || true)"
  if [ -n "$branch" ]; then
    git -C "$repo_dir" pull --ff-only
  else
    printf "  Skipping pull (detached HEAD)\n"
  fi
}

if [ "$#" -gt 0 ]; then
  build_dirs="$*"
else
  build_dirs="build build-release build-asan"
fi

for build_dir in $build_dirs; do
  printf "Preparing %s\n" "$build_dir"
  configure_build_dir "$build_dir"

  deps_dir="$build_dir/_deps"
  if [ ! -d "$deps_dir" ]; then
    continue
  fi

  for repo_dir in "$deps_dir"/*-src; do
    if [ -d "$repo_dir" ]; then
      update_repo "$repo_dir"
    fi
  done
done
