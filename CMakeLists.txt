cmake_minimum_required(VERSION 3.20)
project(kalshi-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(KALSHI_BUILD_TESTS "Build tests" ON)
option(KALSHI_BUILD_EXAMPLES "Build examples" ON)

# Find dependencies
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBSOCKETS REQUIRED libwebsockets)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add subdirectories
add_subdirectory(src)

if(KALSHI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(KALSHI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install targets
install(DIRECTORY include/kalshi DESTINATION include)
