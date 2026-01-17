# Core library
add_library(kalshi_core STATIC
    core/error.cpp
    core/rate_limit.cpp
    core/retry.cpp
)
target_compile_features(kalshi_core PUBLIC cxx_std_23)
target_include_directories(kalshi_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Auth library (RSA-PSS signing)
add_library(kalshi_auth STATIC
    auth/signer.cpp
)
target_link_libraries(kalshi_auth PUBLIC kalshi_core OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(kalshi_auth PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# HTTP client library
add_library(kalshi_http STATIC
    http/client.cpp
)
target_link_libraries(kalshi_http PUBLIC kalshi_core kalshi_auth CURL::libcurl)
target_include_directories(kalshi_http PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Models library
add_library(kalshi_models STATIC
    models/market.cpp
    models/order.cpp
)
target_link_libraries(kalshi_models PUBLIC kalshi_core)
target_include_directories(kalshi_models PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# WebSocket library
add_library(kalshi_ws STATIC
    ws/websocket.cpp
)
target_link_libraries(kalshi_ws PUBLIC kalshi_core kalshi_auth kalshi_models PkgConfig::WEBSOCKETS)
target_include_directories(kalshi_ws PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# API client library (full REST endpoint coverage)
add_library(kalshi_api STATIC
    api/client.cpp
)
target_link_libraries(kalshi_api PUBLIC kalshi_core kalshi_http kalshi_models)
target_include_directories(kalshi_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Main SDK library (combines all)
add_library(kalshi INTERFACE)
target_link_libraries(kalshi INTERFACE kalshi_core kalshi_auth kalshi_http kalshi_models kalshi_ws kalshi_api)
