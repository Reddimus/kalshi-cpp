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
