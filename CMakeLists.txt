cmake_minimum_required(VERSION 3.20)
project(kalshi-cpp VERSION 0.0.2 LANGUAGES CXX)

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
pkg_check_modules(WEBSOCKETS REQUIRED IMPORTED_TARGET libwebsockets)

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
set(KALSHI_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/kalshi")

install(TARGETS kalshi_core kalshi_auth kalshi_http kalshi_models kalshi_ws kalshi_api kalshi
    EXPORT kalshiTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/kalshi DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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
