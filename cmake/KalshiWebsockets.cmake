# The SDK calls OpenSSL on the TLS contexts libwebsockets creates, so both must
# use the same OpenSSL; one context handed to another OpenSSL's calls corrupts
# memory. libwebsockets' CMake package names the libssl it links, so this warns
# when that is not in the directory of the OpenSSL found for the SDK. A
# pkg-config file does not name it, and neither does a package manager's
# configuration-dependent path, so those routes go unchecked.
function(_kalshi_check_websockets_openssl target)
    if(NOT TARGET OpenSSL::SSL)
        return()
    endif()
    set(sdk_ssl "")
    foreach(property IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_DEBUG)
        get_target_property(location OpenSSL::SSL ${property})
        if(location)
            set(sdk_ssl "${location}")
            break()
        endif()
    endforeach()
    get_target_property(links ${target} INTERFACE_LINK_LIBRARIES)
    if(NOT sdk_ssl OR NOT links)
        return()
    endif()
    foreach(link IN LISTS links)
        if(link MATCHES "OpenSSL::")
            return() # the same imported target the SDK links
        endif()
        # Only plain absolute paths can be compared. vcpkg writes each library as
        # a per-configuration generator expression, and a package manager builds
        # one OpenSSL for both packages anyway.
        if(link MATCHES "\\$<" OR NOT IS_ABSOLUTE "${link}")
            continue()
        endif()
        get_filename_component(name "${link}" NAME)
        if(name MATCHES "^(lib)?ssl[.-]")
            get_filename_component(websockets_dir "${link}" DIRECTORY)
            get_filename_component(sdk_dir "${sdk_ssl}" DIRECTORY)
            file(REAL_PATH "${websockets_dir}" websockets_dir)
            file(REAL_PATH "${sdk_dir}" sdk_dir)
            if(NOT websockets_dir STREQUAL sdk_dir)
                message(WARNING "libwebsockets links ${link}, but kalshi-cpp found OpenSSL at "
                                "${sdk_ssl}. Both must use the same OpenSSL; point CMake at the "
                                "one libwebsockets uses, for example with CMAKE_PREFIX_PATH.")
            endif()
            return()
        endif()
    endforeach()
endfunction()

# kalshi_find_websockets(<required>)
#
# Finds libwebsockets and sets KALSHI_WEBSOCKETS_TARGET to an imported target
# and KALSHI_WEBSOCKETS_VIA to "cmake" or "pkg-config" in the caller's scope.
#
# libwebsockets' own CMake package (vcpkg, Homebrew, and most distributions ship
# one) calls include_directories() and link_directories() and appends to
# CMAKE_MODULE_PATH and CMAKE_REQUIRED_INCLUDES. Running it inside a function
# drops the variable changes, and the directory properties are restored
# exactly, so nothing leaks into the caller. The headers go on the imported
# target instead, where they count as system headers.
function(kalshi_find_websockets required)
    get_property(include_dirs DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
    get_property(link_dirs DIRECTORY PROPERTY LINK_DIRECTORIES)
    find_package(libwebsockets CONFIG QUIET)
    set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "${include_dirs}")
    set_property(DIRECTORY PROPERTY LINK_DIRECTORIES "${link_dirs}")

    foreach(candidate websockets_shared websockets)
        if(TARGET ${candidate})
            if(DEFINED LIBWEBSOCKETS_INCLUDE_DIRS)
                set_property(TARGET ${candidate} APPEND PROPERTY
                    INTERFACE_INCLUDE_DIRECTORIES ${LIBWEBSOCKETS_INCLUDE_DIRS})
            endif()
            _kalshi_check_websockets_openssl(${candidate})
            set(KALSHI_WEBSOCKETS_TARGET ${candidate} PARENT_SCOPE)
            set(KALSHI_WEBSOCKETS_VIA cmake PARENT_SCOPE)
            return()
        endif()
    endforeach()

    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(WEBSOCKETS QUIET IMPORTED_TARGET GLOBAL libwebsockets)
    endif()
    if(TARGET PkgConfig::WEBSOCKETS)
        set(KALSHI_WEBSOCKETS_TARGET PkgConfig::WEBSOCKETS PARENT_SCOPE)
        set(KALSHI_WEBSOCKETS_VIA pkg-config PARENT_SCOPE)
    elseif(required)
        message(FATAL_ERROR "libwebsockets not found: install it with its CMake package or a "
                            "pkg-config file")
    endif()
endfunction()
