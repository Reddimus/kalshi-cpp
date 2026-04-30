# Test executable (GoogleTest)
add_executable(kalshi_tests
    test_signer.cpp
    test_models.cpp
    test_features.cpp
    test_api.cpp
    test_version.cpp
    test_ws_parser.cpp
)

target_link_libraries(kalshi_tests PRIVATE
    kalshi_auth kalshi_models kalshi_core kalshi_http kalshi_ws kalshi_api
    GTest::gtest_main GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(kalshi_tests)
