# Fetching Glaze needs CMake 3.31; an installed Glaze works with 3.21.
cmake_minimum_required(VERSION 3.21...4.4)
project(kalshi-cpp
    VERSION 0.9.2
    DESCRIPTION "C++23 client for the Kalshi Predictions API"
    HOMEPAGE_URL "https://github.com/Reddimus/kalshi-cpp"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Tests and examples default to ON only when this is the top-level project, so
# FetchContent consumers get just the library.
option(KALSHI_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(KALSHI_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(KALSHI_BUILD_BENCHMARKS "Build Google Benchmark targets" OFF)
option(KALSHI_USE_SYSTEM_GLAZE "Use an installed Glaze instead of fetching it" OFF)
option(KALSHI_ENABLE_LTO "Enable interprocedural optimization for project targets" OFF)
option(KALSHI_NATIVE_ARCH "Tune project targets for this machine's CPU (-march=native)" OFF)
option(KALSHI_ENABLE_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)
option(KALSHI_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(KALSHI_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(KALSHI_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
option(KALSHI_ENABLE_COVERAGE "Enable gcov coverage instrumentation" OFF)
# Off under FetchContent, so a consumer's install does not also install this SDK.
option(KALSHI_INSTALL "Generate install rules and package files" ${PROJECT_IS_TOP_LEVEL})

if(KALSHI_ENABLE_SANITIZERS AND KALSHI_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer require separate builds")
endif()

# -march=native names the build machine's CPU, so compilers reject it when
# targeting another architecture, as cross and universal macOS builds do.
if(KALSHI_NATIVE_ARCH AND NOT MSVC)
    include(CheckCXXCompilerFlag)
    # Probe on every configure: the answer changes with CMAKE_OSX_ARCHITECTURES.
    unset(KALSHI_MARCH_NATIVE_WORKS CACHE)
    check_cxx_compiler_flag(-march=native KALSHI_MARCH_NATIVE_WORKS)
    if(NOT KALSHI_MARCH_NATIVE_WORKS)
        message(FATAL_ERROR "KALSHI_NATIVE_ARCH needs a build for this machine's CPU, "
            "but the compiler rejects -march=native for this target")
    endif()
endif()

if(KALSHI_ENABLE_CLANG_TIDY)
    # CI uses clang-tidy 18. Homebrew's llvm@18 is keg-only, so look there too.
    find_program(KALSHI_CLANG_TIDY_EXECUTABLE NAMES clang-tidy-18 clang-tidy
        HINTS /opt/homebrew/opt/llvm@18/bin /usr/local/opt/llvm@18/bin REQUIRED)
endif()

if(KALSHI_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT KALSHI_LTO_SUPPORTED OUTPUT KALSHI_LTO_ERROR)
    if(NOT KALSHI_LTO_SUPPORTED)
        message(WARNING "LTO is not supported: ${KALSHI_LTO_ERROR}")
    endif()
endif()

# Instrumentation applies to every target in this tree, including fetched
# dependencies, so ASan container checks and TSan see consistent code.
if(KALSHI_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(KALSHI_ENABLE_THREAD_SANITIZER)
    if(MSVC)
        message(FATAL_ERROR "ThreadSanitizer is not supported with MSVC")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
if(KALSHI_ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# Applies warnings and opt-in tuning to targets this project owns.
# -Wmissing-field-initializers is off because every aggregate member has a
# default initializer, and GCC's -Wmaybe-uninitialized reports false positives
# on std::optional at -O2 and above.
function(kalshi_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${KALSHI_WARNINGS_AS_ERRORS}>:/WX>)
        # Standard getenv/gmtime are fine; MSVC's "secure" alternatives are not portable.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
            -Wno-missing-field-initializers
            $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>
            $<$<BOOL:${KALSHI_WARNINGS_AS_ERRORS}>:-Werror>
            $<$<BOOL:${KALSHI_NATIVE_ARCH}>:-march=native>)
    endif()
    if(KALSHI_ENABLE_LTO AND KALSHI_LTO_SUPPORTED)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
    if(KALSHI_ENABLE_CLANG_TIDY)
        set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY "${KALSHI_CLANG_TIDY_EXECUTABLE}")
    endif()
endfunction()

# kalshi::VERSION and the CMake package version both come from PROJECT_VERSION.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kalshi/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/kalshi/version.hpp
    @ONLY)

find_package(OpenSSL 3.0 REQUIRED)
find_package(CURL REQUIRED)
include(cmake/KalshiWebsockets.cmake)
kalshi_find_websockets(TRUE)

# Glaze is a private, header-only build dependency; installed headers never
# include it. src/json.hpp rejects versions older than 8.3 at compile time,
# which also covers a Glaze another project declared through FetchContent.
if(KALSHI_USE_SYSTEM_GLAZE)
    find_package(glaze REQUIRED)
    if(glaze_VERSION VERSION_LESS 8.3)
        message(FATAL_ERROR "kalshi-cpp requires Glaze 8.3 or newer, found ${glaze_VERSION}")
    endif()
else()
    include(FetchContent)
    # Downloads are release archives pinned by SHA-256, here and below. A git
    # tag can be moved to another commit; with the hash, a changed archive
    # fails the download instead of changing what is built. To update one, set
    # the version in the URL and the comment, download that URL over HTTPS,
    # and take the hash from `shasum -a 256`.
    # Glaze v9.0.0 (commit d78832c82289c61a9315bfbc35332cec9f4e93ca)
    FetchContent_Declare(glaze
        URL https://github.com/stephenberry/glaze/archive/refs/tags/v9.0.0.tar.gz
        URL_HASH SHA256=dd7b033c5bf6e4308615bb7834c541291d5b20f64121d9a68222a39414635517)
    FetchContent_MakeAvailable(glaze)
endif()

add_subdirectory(src)

# Replaces operator new to count allocations for kalshi_allocation_tests and
# the benchmarks. It is an object library because the linker may skip a static
# library member that only defines operator new. Sanitizer builds leave it out
# because ASan and TSan replace operator new themselves; coverage builds skip
# it too.
if((KALSHI_BUILD_TESTS OR KALSHI_BUILD_BENCHMARKS) AND NOT KALSHI_ENABLE_SANITIZERS
    AND NOT KALSHI_ENABLE_THREAD_SANITIZER AND NOT KALSHI_ENABLE_COVERAGE)
    add_library(kalshi_allocation_counter OBJECT tests/support/allocation_counter.cpp)
    target_include_directories(kalshi_allocation_counter PUBLIC ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(kalshi_allocation_counter PUBLIC KALSHI_COUNT_ALLOCATIONS)
    kalshi_configure_target(kalshi_allocation_counter)
endif()

if(KALSHI_BUILD_TESTS)
    include(FetchContent)
    # GoogleTest v1.18.0 (commit 063de7e9578f82b369302001269680b4b1553359)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
        URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(KALSHI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(KALSHI_BUILD_BENCHMARKS)
    include(FetchContent)
    # Google Benchmark v1.9.5 (commit 192ef10025eb2c4cdd392bc502f0c852196baa48)
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
        URL_HASH SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(benchmarks)
endif()

if(KALSHI_INSTALL)
    set(KALSHI_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/kalshi")

    install(TARGETS kalshi_core kalshi_auth kalshi_http kalshi_models kalshi_ws kalshi_api kalshi
        EXPORT kalshiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY include/kalshi DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/kalshi/version.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kalshi)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/kalshiConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/kalshiConfig.cmake
        INSTALL_DESTINATION ${KALSHI_INSTALL_CMAKEDIR})
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/kalshiConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion)
    install(EXPORT kalshiTargets
        FILE kalshiTargets.cmake
        NAMESPACE kalshi::
        DESTINATION ${KALSHI_INSTALL_CMAKEDIR})
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/KalshiWebsockets.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/kalshiConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/kalshiConfigVersion.cmake
        DESTINATION ${KALSHI_INSTALL_CMAKEDIR})
endif()
