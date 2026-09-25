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
#include <memory>
#include <string_view>

namespace {
class OfflineTransport final : public kalshi::HttpTransport {
public:
	kalshi::Result<kalshi::HttpResponse> request(kalshi::HttpMethod, std::string_view,
												 std::string_view) const override {
		return std::unexpected(kalshi::Error::network("offline"));
	}
};
} // namespace

// Calls into every static library so a missing link dependency fails here.
int main() {
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem("id", "not a key");
	kalshi::KalshiClient client{std::make_shared<OfflineTransport>()};
	const kalshi::Result<kalshi::ExchangeStatus> status = client.get_exchange_status();
	// Storing through a volatile keeps the reference, so the linker must find it.
	kalshi::Result<void> (kalshi::WebSocketClient::*volatile connect)() =
		&kalshi::WebSocketClient::connect;
	const kalshi::Result<kalshi::Timestamp> time = kalshi::parse_timestamp("2026-01-01T00:00:00Z");
	const kalshi::HttpClient http{kalshi::ClientConfig::for_environment(kalshi::Environment::Demo)};
	if (signer || status || connect == nullptr || !time || http.config().base_url.empty())
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
cmake_minimum_required(VERSION 3.21)
project(kalshi_installed_consumer LANGUAGES CXX)
find_package(kalshi $minor CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE kalshi::kalshi)
# Public headers must stay warning-free for projects that build with -Werror.
target_compile_options(consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
CMAKE
cmake -S "$scratch_dir/installed" -B "$scratch_dir/installed-build" \
  -DCMAKE_PREFIX_PATH="$scratch_dir/prefix"
cmake --build "$scratch_dir/installed-build" --parallel
test "$("$scratch_dir/installed-build/consumer")" = "$version"

# Tests and examples must stay off by default when consumed as a subproject.
write_consumer "$scratch_dir/fetched"
cat > "$scratch_dir/fetched/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.21)
project(kalshi_fetch_consumer LANGUAGES CXX)
include(FetchContent)
FetchContent_Declare(kalshi SOURCE_DIR "$repo_dir")
FetchContent_MakeAvailable(kalshi)
if(TARGET kalshi_tests OR TARGET example_market_data)
  message(FATAL_ERROR "kalshi-cpp built tests or examples as a subproject")
endif()
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE kalshi::kalshi)
# Public headers must stay warning-free for projects that build with -Werror.
target_compile_options(consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
CMAKE
cmake -S "$scratch_dir/fetched" -B "$scratch_dir/fetched-build"
cmake --build "$scratch_dir/fetched-build" --parallel
test "$("$scratch_dir/fetched-build/consumer")" = "$version"

echo "Installed and FetchContent consumers link kalshi-cpp $version"
