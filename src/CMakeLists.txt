# Targets are layered: core <- auth <- http <- api, and core <- models <- ws.
# kalshi::kalshi links all of them.

add_library(kalshi_core STATIC
    core/rate_limit.cpp
    core/retry.cpp)
target_compile_features(kalshi_core PUBLIC cxx_std_23)
target_include_directories(kalshi_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    # version.hpp is generated into this project's binary dir, which differs
    # from CMAKE_BINARY_DIR when the SDK is consumed through FetchContent.
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(kalshi_auth STATIC auth/signer.cpp)
target_link_libraries(kalshi_auth PUBLIC kalshi_core PRIVATE OpenSSL::Crypto)

add_library(kalshi_http STATIC http/client.cpp)
target_link_libraries(kalshi_http PUBLIC kalshi_auth PRIVATE CURL::libcurl)

add_library(kalshi_models INTERFACE)
target_link_libraries(kalshi_models INTERFACE kalshi_core)

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

add_library(kalshi_api STATIC api/client.cpp)
target_link_libraries(kalshi_api
    PUBLIC kalshi_http kalshi_models
    PRIVATE $<BUILD_INTERFACE:glaze::glaze>)

add_library(kalshi INTERFACE)
add_library(kalshi::kalshi ALIAS kalshi)
target_link_libraries(kalshi INTERFACE kalshi_core kalshi_auth kalshi_http kalshi_models kalshi_ws
    kalshi_api)

foreach(target kalshi_core kalshi_auth kalshi_http kalshi_ws kalshi_api)
    kalshi_configure_target(${target})
endforeach()
