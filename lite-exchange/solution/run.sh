#!/usr/bin/env bash
# Build and run the tests.
#   ./run.sh                    everything except the OPTIONAL parts
#   ./run.sh 'Part2*'           one part (any --gtest_filter pattern)
#   ./run.sh '*'                everything
set -euo pipefail
cd "$(dirname "$0")"
FILTER="${1:--Optional*}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

if command -v cmake >/dev/null 2>&1; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null
  cmake --build build -j 4
  ./build/tests --gtest_filter="$FILTER"
else
  # No CMake: same flags, built directly (uses the bundled gtest shim).
  mkdir -p build
  g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror -g -fno-omit-frame-pointer \
      -fsanitize=address,undefined -pthread -Iinclude -isystem third_party/mini_gtest \
      $(find test -name '*.cc') third_party/mini_gtest/gtest_main.cc -o build/tests
  ./build/tests --gtest_filter="$FILTER"
fi
