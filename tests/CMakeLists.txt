# Test executable
add_executable(kalshi_tests
    test_main.cpp
    test_signer.cpp
    test_models.cpp
    test_features.cpp
    test_api.cpp
)

target_link_libraries(kalshi_tests PRIVATE kalshi_auth kalshi_models kalshi_core kalshi_http kalshi_ws kalshi_api)

add_test(NAME kalshi_tests COMMAND kalshi_tests)
