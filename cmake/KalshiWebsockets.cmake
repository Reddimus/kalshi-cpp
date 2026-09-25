# Finds libwebsockets and sets KALSHI_WEBSOCKETS_TARGET to an imported target.
# Used by kalshi-cpp's build and by its installed package config.
#
# libwebsockets' own CMake package (vcpkg, Homebrew, and most distributions ship
# one) adds its headers with a directory-wide include_directories(), which
# exposes their warnings to every target in the caller's directory. This moves
# them onto the imported target, where they count as system headers. Without
# that package, pkg-config is used.

find_package(libwebsockets CONFIG QUIET)
if(TARGET websockets_shared OR TARGET websockets)
    if(TARGET websockets_shared)
        set(KALSHI_WEBSOCKETS_TARGET websockets_shared)
    else()
        set(KALSHI_WEBSOCKETS_TARGET websockets)
    endif()
    if(DEFINED LIBWEBSOCKETS_INCLUDE_DIRS)
        get_property(_kalshi_include_dirs DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
        list(REMOVE_ITEM _kalshi_include_dirs ${LIBWEBSOCKETS_INCLUDE_DIRS})
        set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "${_kalshi_include_dirs}")
        set_property(TARGET ${KALSHI_WEBSOCKETS_TARGET} APPEND PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES ${LIBWEBSOCKETS_INCLUDE_DIRS})
        unset(_kalshi_include_dirs)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBSOCKETS REQUIRED IMPORTED_TARGET libwebsockets)
    set(KALSHI_WEBSOCKETS_TARGET PkgConfig::WEBSOCKETS)
endif()
