# Core library
add_library(kalshi_core STATIC
    core/error.cpp
    core/rate_limit.cpp
    core/retry.cpp
)
target_include_directories(kalshi_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Auth library (RSA-PSS signing)
add_library(kalshi_auth STATIC
    auth/signer.cpp
)
target_link_libraries(kalshi_auth PUBLIC kalshi_core OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(kalshi_auth PUBLIC ${CMAKE_SOURCE_DIR}/include)

# HTTP client library
add_library(kalshi_http STATIC
    http/client.cpp
)
target_link_libraries(kalshi_http PUBLIC kalshi_core kalshi_auth CURL::libcurl)
target_include_directories(kalshi_http PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Models library
add_library(kalshi_models STATIC
    models/market.cpp
    models/order.cpp
)
target_link_libraries(kalshi_models PUBLIC kalshi_core)
target_include_directories(kalshi_models PUBLIC ${CMAKE_SOURCE_DIR}/include)

# WebSocket library
add_library(kalshi_ws STATIC
    ws/websocket.cpp
)
target_link_libraries(kalshi_ws PUBLIC kalshi_core kalshi_auth kalshi_models ${WEBSOCKETS_LIBRARIES})
target_include_directories(kalshi_ws PUBLIC ${CMAKE_SOURCE_DIR}/include ${WEBSOCKETS_INCLUDE_DIRS})

# API client library (full REST endpoint coverage)
add_library(kalshi_api STATIC
    api/client.cpp
)
target_link_libraries(kalshi_api PUBLIC kalshi_core kalshi_http kalshi_models)
target_include_directories(kalshi_api PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Main SDK library (combines all)
add_library(kalshi INTERFACE)
target_link_libraries(kalshi INTERFACE kalshi_core kalshi_auth kalshi_http kalshi_models kalshi_ws kalshi_api)
