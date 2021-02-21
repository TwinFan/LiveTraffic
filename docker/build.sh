#!/bin/bash
set -eu

function build() {
  local src_dir="$1"
  export platform="$2"
  echo "----------------- Building for $platform -----------------"

  local build_dir="$src_dir/build-$platform"

  echo "pwd       = $(pwd)"
  echo "src_dir   = $src_dir"
  echo "build_dir = $build_dir"

  local flags=()
  local cmake="cmake"
  case "$platform" in
    win)
      flags+=('-DCMAKE_TOOLCHAIN_FILE=../docker/Toolchain-mingw-w64-x86-64.cmake')
      ;;
    mac)
      flags+=('-DCMAKE_TOOLCHAIN_FILE=../docker/Toolchain-ubuntu-osxcross.cmake')
      ;;
  esac

  mkdir -p "$build_dir" && cd "$build_dir"
  "$cmake" -G Ninja "${flags[@]}" ..
  ninja
}

src_dir="$(pwd)"

for platform in $@; do
  build "$src_dir" "$platform"
done
