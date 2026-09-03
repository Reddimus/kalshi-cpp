#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
scratch_dir=$(mktemp -d "${TMPDIR:-/tmp}/kalshi-consumers.XXXXXX")
trap 'rm -rf -- "$scratch_dir"' EXIT HUP INT TERM

cmake -S "$repo_dir" -B "$scratch_dir/sdk-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DKALSHI_BUILD_TESTS=OFF \
  -DKALSHI_BUILD_EXAMPLES=OFF
cmake --build "$scratch_dir/sdk-build" --parallel
cmake --install "$scratch_dir/sdk-build" --prefix "$scratch_dir/prefix"

mkdir -p "$scratch_dir/consumer"
printf '%s\n' \
  'cmake_minimum_required(VERSION 3.20)' \
  'project(kalshi_consumer LANGUAGES CXX)' \
  'set(CMAKE_CXX_STANDARD 23)' \
  'find_package(kalshi 0.5 CONFIG REQUIRED)' \
  'add_executable(consumer main.cpp)' \
  'target_link_libraries(consumer PRIVATE kalshi::kalshi)' \
  > "$scratch_dir/consumer/CMakeLists.txt"
printf '%s\n' \
  '#include <kalshi/kalshi.hpp>' \
  '#include <iostream>' \
  'int main() { std::cout << kalshi::VERSION; }' \
  > "$scratch_dir/consumer/main.cpp"
cmake -S "$scratch_dir/consumer" -B "$scratch_dir/consumer-build" \
  -DCMAKE_PREFIX_PATH="$scratch_dir/prefix"
cmake --build "$scratch_dir/consumer-build" --parallel
test "$("$scratch_dir/consumer-build/consumer")" = "0.5.0"

mkdir -p "$scratch_dir/fetch-consumer"
printf '%s\n' \
  'cmake_minimum_required(VERSION 3.20)' \
  'project(kalshi_fetch_consumer LANGUAGES CXX)' \
  'set(CMAKE_CXX_STANDARD 23)' \
  'include(FetchContent)' \
  "FetchContent_Declare(kalshi SOURCE_DIR \"$repo_dir\")" \
  'set(KALSHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)' \
  'set(KALSHI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)' \
  'FetchContent_MakeAvailable(kalshi)' \
  'add_executable(fetch_consumer main.cpp)' \
  'target_link_libraries(fetch_consumer PRIVATE kalshi)' \
  > "$scratch_dir/fetch-consumer/CMakeLists.txt"
cp "$scratch_dir/consumer/main.cpp" "$scratch_dir/fetch-consumer/main.cpp"
cmake -S "$scratch_dir/fetch-consumer" -B "$scratch_dir/fetch-build"
cmake --build "$scratch_dir/fetch-build" --parallel
test "$("$scratch_dir/fetch-build/fetch_consumer")" = "0.5.0"
