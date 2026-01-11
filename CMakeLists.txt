cmake_minimum_required(VERSION 3.20)
project(kalshi-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(KALSHI_BUILD_TESTS "Build tests" ON)
option(KALSHI_BUILD_EXAMPLES "Build examples" ON)
option(KALSHI_ENABLE_LTO "Enable Link Time Optimization" ON)
option(KALSHI_NATIVE_ARCH "Use -march=native for CPU-specific tuning" OFF)

# Aggressive optimization flags for Release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Enable LTO if requested and supported
if(KALSHI_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
    if(LTO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        message(STATUS "LTO enabled for Release builds")
    else()
        message(WARNING "LTO not supported: ${LTO_ERROR}")
    endif()
endif()

# CPU-specific tuning (optional, not portable)
if(KALSHI_NATIVE_ARCH)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
    message(STATUS "Using -march=native for CPU-specific optimizations")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mtune=generic")
endif()

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
