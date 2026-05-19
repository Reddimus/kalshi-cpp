cmake_minimum_required(VERSION 3.20)
project(kalshi-cpp VERSION 0.4.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Options
option(KALSHI_BUILD_TESTS "Build tests" ON)
option(KALSHI_BUILD_EXAMPLES "Build examples" ON)
option(KALSHI_ENABLE_LTO "Enable Link Time Optimization" ON)
option(KALSHI_NATIVE_ARCH "Use -march=native for CPU-specific tuning" OFF)

# Aggressive optimization flags for Release builds. MSVC has its own
# Release flags via CMake's default; only override for GCC/Clang.
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

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

# CPU-specific tuning. GCC/Clang only — MSVC uses /arch flags with
# different semantics, so we leave its defaults alone.
if(NOT MSVC)
    if(KALSHI_NATIVE_ARCH)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
        message(STATUS "Using -march=native for CPU-specific optimizations")
    else()
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-march=x86-64-v3" COMPILER_SUPPORTS_X86_64_V3)
        if(COMPILER_SUPPORTS_X86_64_V3)
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=x86-64-v3")
        else()
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mtune=generic")
        endif()
    endif()
endif()

# Sanitizer support
option(KALSHI_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSanitizer" OFF)
if(KALSHI_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers enabled (ASan + UBSan)")
endif()

# Code coverage support
option(KALSHI_ENABLE_COVERAGE "Enable code coverage instrumentation (gcov)" OFF)
if(KALSHI_ENABLE_COVERAGE)
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
    message(STATUS "Code coverage enabled")
endif()

# Generate version header from CMake PROJECT_VERSION. This is the single
# source of truth for the SDK version — kalshi/version.hpp is regenerated
# at configure time, so the C++ constexpr cannot drift from the CMake
# project version (and from there, from the FetchContent tag consumers
# pin to). See include/kalshi/version.hpp.in.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kalshi/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/kalshi/version.hpp
    @ONLY
)

# Find dependencies. Linux/macOS use pkg-config to locate
# libwebsockets (apt/brew packages ship .pc files); Windows/MSVC
# uses vcpkg's CMake config-mode export.
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBSOCKETS REQUIRED IMPORTED_TARGET libwebsockets)
else()
    find_package(libwebsockets CONFIG REQUIRED)
endif()

include(FetchContent)

# Glaze — JSON library (compile-time reflection). Used for outgoing
# request serialization where stable key order matters (Kalshi's API
# rejects unordered subscribe payloads). The REST response path uses
# the hand-rolled scanners in detail/ws_json.hpp + the local helpers
# in api/client.cpp and was deliberately stripped of any JSON library
# in v0.0.7/v0.0.8 — DO NOT reintroduce a runtime parser there.
# License: MIT. Same v7.6.0 pin as open-meteo-cpp.
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v7.6.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glaze)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add subdirectories
add_subdirectory(src)

if(KALSHI_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(KALSHI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install targets
set(KALSHI_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/kalshi")

install(TARGETS kalshi_core kalshi_auth kalshi_http kalshi_models kalshi_ws kalshi_api kalshi
    EXPORT kalshiTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/kalshi DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "*.in" EXCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/kalshi/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kalshi)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/kalshiConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/kalshiConfig.cmake
    INSTALL_DESTINATION ${KALSHI_INSTALL_CMAKEDIR}
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/kalshiConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
export(EXPORT kalshiTargets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/kalshiTargets.cmake
    NAMESPACE kalshi::
)
install(EXPORT kalshiTargets
    FILE kalshiTargets.cmake
    NAMESPACE kalshi::
    DESTINATION ${KALSHI_INSTALL_CMAKEDIR}
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/kalshiConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/kalshiConfigVersion.cmake
    DESTINATION ${KALSHI_INSTALL_CMAKEDIR}
)
