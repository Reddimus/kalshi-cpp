add_executable(kalshi_tests
    test_api_support.cpp
    test_features.cpp
    test_fixed_point.cpp
    test_helpers.cpp
    test_http_client.cpp
    test_operation_routes.cpp
    test_operations.cpp
    test_signer.cpp
    test_transports.cpp
    test_version.cpp
    test_ws_client.cpp
    test_ws_frames.cpp
    test_ws_lifecycle.cpp
    test_ws_messages.cpp
    test_ws_subscriptions.cpp)
# Tests reach implementation headers under src/, which include Glaze.
target_include_directories(kalshi_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/api
    ${PROJECT_SOURCE_DIR}/src/ws)
target_link_libraries(kalshi_tests PRIVATE
    kalshi::kalshi glaze::glaze OpenSSL::Crypto ${KALSHI_WEBSOCKETS_TARGET} GTest::gtest_main
    GTest::gmock)
kalshi_configure_target(kalshi_tests)
if(WIN32)
    # The WebSocket test server includes <libwebsockets.h>, which pulls in <windows.h>.
    target_compile_definitions(kalshi_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
# The generated route tests also cover operations Kalshi marks deprecated.
set_source_files_properties(test_operation_routes.cpp PROPERTIES COMPILE_OPTIONS
    "$<IF:$<CXX_COMPILER_ID:MSVC>,/wd4996,-Wno-deprecated-declarations>")

include(GoogleTest)
# Instrumented binaries can take several seconds to list tests on macOS runners.
gtest_discover_tests(kalshi_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30)

# Coarse throughput guard: fails only on order-of-magnitude regressions.
# Instrumented builds run it for correctness only.
add_executable(kalshi_parse_benchmark parse_benchmark.cpp)
target_include_directories(kalshi_parse_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/src/api
    ${PROJECT_SOURCE_DIR}/src/ws)
target_link_libraries(kalshi_parse_benchmark PRIVATE kalshi::kalshi glaze::glaze)
kalshi_configure_target(kalshi_parse_benchmark)
if(KALSHI_ENABLE_SANITIZERS OR KALSHI_ENABLE_THREAD_SANITIZER OR KALSHI_ENABLE_COVERAGE)
    add_test(NAME kalshi_parse_benchmark COMMAND kalshi_parse_benchmark --no-timing)
else()
    add_test(NAME kalshi_parse_benchmark COMMAND kalshi_parse_benchmark)
endif()
set_tests_properties(kalshi_parse_benchmark PROPERTIES TIMEOUT 30)
