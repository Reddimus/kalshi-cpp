# Test executable (GoogleTest)
add_executable(kalshi_tests
    test_signer.cpp
    test_models.cpp
    test_features.cpp
    test_api.cpp
    test_version.cpp
    test_ws_parser.cpp
    test_ws_subscription_registry.cpp
    test_ws_lifecycle.cpp
    test_json_serialize.cpp
    test_response_parsers.cpp
    test_query_builders.cpp
    test_operation_contracts.cpp
    test_fixed_point.cpp
)

target_link_libraries(kalshi_tests PRIVATE
    kalshi_auth kalshi_models kalshi_core kalshi_http kalshi_ws kalshi_api
    GTest::gtest_main GTest::gmock
)
# test_json_serialize.cpp includes shim headers from src/, which in turn
# include glaze. The headers are header-only and the shim structs
# never escape this test TU (mirrors the kalshi_api/kalshi_ws TUs that
# include them privately).
target_include_directories(kalshi_tests PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/api>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/ws>
    $<BUILD_INTERFACE:${glaze_SOURCE_DIR}/include>
)

include(GoogleTest)
# Instrumented binaries can take several seconds to initialize before listing
# tests on macOS runners.
gtest_discover_tests(kalshi_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30)

# Throughput regression guard. It renders a representative 50-order batch and
# parses a representative trade frame under generous per-operation caps.
add_executable(kalshi_parse_benchmark
    parse_benchmark.cpp
)
target_link_libraries(kalshi_parse_benchmark PRIVATE
    kalshi_api kalshi_core kalshi_ws
)
target_include_directories(kalshi_parse_benchmark PRIVATE
    $<BUILD_INTERFACE:${glaze_SOURCE_DIR}/include>
)
if(NOT MSVC)
    target_compile_options(kalshi_parse_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME kalshi_parse_benchmark COMMAND kalshi_parse_benchmark)
set_tests_properties(kalshi_parse_benchmark PROPERTIES TIMEOUT 30)
