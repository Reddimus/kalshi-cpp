#!/bin/sh
# Builds two throwaway consumers: one against an installed package
# (find_package) and one through FetchContent. Both must link every SDK layer
# and print the project version.
set -eu

repo_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
scratch_dir=$(mktemp -d "${TMPDIR:-/tmp}/kalshi-consumers.XXXXXX")
trap 'rm -rf -- "$scratch_dir"' EXIT HUP INT TERM

version=$("$repo_dir/tools/project_version.sh")
minor=${version%.*}

write_consumer() {
  mkdir -p "$1"
  cat > "$1/main.cpp" <<'CPP'
#include <kalshi/kalshi.hpp>
#include <iostream>

int main() {
	// Touch every static library so a missing link dependency fails here.
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem("id", "not a key");
	const auto markets = &kalshi::KalshiClient::get_markets; // auto-ok: member pointer
	const auto connect = &kalshi::WebSocketClient::connect; // auto-ok: member pointer
	const auto request = &kalshi::HttpClient::request;      // auto-ok: member pointer
	if (signer || !markets || !connect || !request)
		return 1;
	std::cout << kalshi::VERSION;
}
CPP
}

cmake -S "$repo_dir" -B "$scratch_dir/sdk-build" -DCMAKE_BUILD_TYPE=Release \
  -DKALSHI_BUILD_TESTS=OFF -DKALSHI_BUILD_EXAMPLES=OFF
cmake --build "$scratch_dir/sdk-build" --parallel
cmake --install "$scratch_dir/sdk-build" --prefix "$scratch_dir/prefix"

write_consumer "$scratch_dir/installed"
cat > "$scratch_dir/installed/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.31)
project(kalshi_installed_consumer LANGUAGES CXX)
find_package(kalshi $minor CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE kalshi::kalshi)
CMAKE
cmake -S "$scratch_dir/installed" -B "$scratch_dir/installed-build" \
  -DCMAKE_PREFIX_PATH="$scratch_dir/prefix"
cmake --build "$scratch_dir/installed-build" --parallel
test "$("$scratch_dir/installed-build/consumer")" = "$version"

# Tests and examples must stay off by default when consumed as a subproject.
write_consumer "$scratch_dir/fetched"
cat > "$scratch_dir/fetched/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.31)
project(kalshi_fetch_consumer LANGUAGES CXX)
include(FetchContent)
FetchContent_Declare(kalshi SOURCE_DIR "$repo_dir")
FetchContent_MakeAvailable(kalshi)
if(TARGET kalshi_tests OR TARGET example_basic_usage)
  message(FATAL_ERROR "kalshi-cpp built tests or examples as a subproject")
endif()
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE kalshi::kalshi)
CMAKE
cmake -S "$scratch_dir/fetched" -B "$scratch_dir/fetched-build"
cmake --build "$scratch_dir/fetched-build" --parallel
test "$("$scratch_dir/fetched-build/consumer")" = "$version"

echo "Installed and FetchContent consumers link kalshi-cpp $version"
