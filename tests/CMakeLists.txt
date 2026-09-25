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
    test_transports.cpp
    test_http_client.cpp)
# Tests reach implementation headers under src/, which include Glaze.
target_include_directories(kalshi_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/api
    ${PROJECT_SOURCE_DIR}/src/ws)
target_link_libraries(kalshi_tests PRIVATE
    kalshi::kalshi glaze::glaze OpenSSL::Crypto GTest::gtest_main GTest::gmock)
kalshi_configure_target(kalshi_tests)
# Contract tests deliberately call deprecated compatibility overloads.
set_source_files_properties(test_operation_contracts.cpp PROPERTIES COMPILE_OPTIONS
    "$<IF:$<CXX_COMPILER_ID:MSVC>,/wd4996,-Wno-deprecated-declarations>")

include(GoogleTest)
# Instrumented binaries can take several seconds to list tests on macOS runners.
gtest_discover_tests(kalshi_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30)

# Coarse throughput guard: fails only on order-of-magnitude regressions.
# Instrumented builds run it for correctness only.
add_executable(kalshi_parse_benchmark parse_benchmark.cpp)
target_include_directories(kalshi_parse_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/src/api)
target_link_libraries(kalshi_parse_benchmark PRIVATE kalshi::kalshi glaze::glaze)
kalshi_configure_target(kalshi_parse_benchmark)
if(KALSHI_ENABLE_SANITIZERS OR KALSHI_ENABLE_THREAD_SANITIZER OR KALSHI_ENABLE_COVERAGE)
    add_test(NAME kalshi_parse_benchmark COMMAND kalshi_parse_benchmark --no-timing)
else()
    add_test(NAME kalshi_parse_benchmark COMMAND kalshi_parse_benchmark)
endif()
set_tests_properties(kalshi_parse_benchmark PROPERTIES TIMEOUT 30)
