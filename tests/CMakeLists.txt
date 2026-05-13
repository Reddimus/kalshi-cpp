# Test executable (GoogleTest)
add_executable(kalshi_tests
    test_signer.cpp
    test_models.cpp
    test_features.cpp
    test_api.cpp
    test_version.cpp
    test_ws_parser.cpp
    test_ws_lifecycle.cpp
    test_json_serialize.cpp
    test_response_parsers.cpp
    test_query_builders.cpp
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
    $<BUILD_INTERFACE:${glaze_SOURCE_DIR}/include>
)

include(GoogleTest)
gtest_discover_tests(kalshi_tests)

# Serialize-throughput regression guard. Renders a representative
# 50-order batch-create body 1k times and asserts us/op stays under
# a cap. ctest --timeout = 30s; actual wall-clock is ~50-100ms.
add_executable(kalshi_parse_benchmark
    parse_benchmark.cpp
)
target_link_libraries(kalshi_parse_benchmark PRIVATE
    kalshi_api kalshi_core
)
target_include_directories(kalshi_parse_benchmark PRIVATE
    $<BUILD_INTERFACE:${glaze_SOURCE_DIR}/include>
)
if(NOT MSVC)
    target_compile_options(kalshi_parse_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME kalshi_parse_benchmark COMMAND kalshi_parse_benchmark)
set_tests_properties(kalshi_parse_benchmark PROPERTIES TIMEOUT 30)
