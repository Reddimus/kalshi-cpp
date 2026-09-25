# Targets are layered: core <- auth <- http <- api, and core <- models <- ws, api.
# kalshi::kalshi links all of them.

# Header-only: errors, fixed-point values, environments, and the version.
add_library(kalshi_core INTERFACE)
target_compile_features(kalshi_core INTERFACE cxx_std_23)
target_include_directories(kalshi_core INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    # version.hpp is generated into this project's binary dir, which differs
    # from CMAKE_BINARY_DIR when the SDK is consumed through FetchContent.
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(kalshi_auth STATIC auth/signer.cpp)
target_link_libraries(kalshi_auth PUBLIC kalshi_core PRIVATE OpenSSL::Crypto)

add_library(kalshi_http STATIC
    http/client.cpp
    http/rate_limit.cpp
    http/rate_limited_transport.cpp
    http/retry.cpp)
target_link_libraries(kalshi_http
    PUBLIC kalshi_auth
    PRIVATE CURL::libcurl $<BUILD_INTERFACE:glaze::glaze>)

# Generated REST models and the helpers that convert their values.
add_library(kalshi_models STATIC models/helpers.cpp)
target_link_libraries(kalshi_models PUBLIC kalshi_core)

if(WIN32)
    set(KALSHI_WEBSOCKETS_TARGET websockets_shared)
else()
    set(KALSHI_WEBSOCKETS_TARGET PkgConfig::WEBSOCKETS)
endif()

add_library(kalshi_ws STATIC ws/websocket.cpp)
target_link_libraries(kalshi_ws
    PUBLIC kalshi_auth kalshi_models
    PRIVATE ${KALSHI_WEBSOCKETS_TARGET} OpenSSL::SSL $<BUILD_INTERFACE:glaze::glaze>)
if(WIN32)
    # <libwebsockets.h> pulls in <windows.h>, whose min/max macros break
    # std::numeric_limits<T>::max() in Glaze.
    target_compile_definitions(kalshi_ws PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

file(GLOB KALSHI_GENERATED_OPERATIONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/api/operations/*.cpp)
add_library(kalshi_api STATIC
    api/client.cpp
    api/support.cpp
    ${KALSHI_GENERATED_OPERATIONS})
target_link_libraries(kalshi_api
    PUBLIC kalshi_http kalshi_models
    PRIVATE $<BUILD_INTERFACE:glaze::glaze>)

add_library(kalshi INTERFACE)
add_library(kalshi::kalshi ALIAS kalshi)
target_link_libraries(kalshi INTERFACE kalshi_core kalshi_auth kalshi_http kalshi_models kalshi_ws
    kalshi_api)

foreach(target kalshi_auth kalshi_http kalshi_models kalshi_ws kalshi_api)
    kalshi_configure_target(${target})
endforeach()
