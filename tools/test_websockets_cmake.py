"""Configures small projects to check the OpenSSL check in cmake/KalshiWebsockets.cmake.

    python3 tools/test_websockets_cmake.py   # `make lint` runs this

The check must warn when libwebsockets links a libssl from another directory,
and stay quiet for what package managers write, such as vcpkg's
per-configuration generator expressions.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WARNING = "Both must use the same OpenSSL"

PROJECT = textwrap.dedent("""\
    cmake_minimum_required(VERSION 3.21)
    project(openssl_check LANGUAGES C)
    find_package(OpenSSL QUIET)
    set(sdk_ssl "")
    if(TARGET OpenSSL::SSL)
        foreach(property IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_DEBUG)
            get_target_property(location OpenSSL::SSL ${property})
            if(location)
                set(sdk_ssl "${location}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT sdk_ssl)
        message(STATUS "KALSHI-TEST no OpenSSL")
        return()
    endif()
    include("@MODULE@")
    if(CASE STREQUAL "same")
        set(links "${sdk_ssl}")
    elseif(CASE STREQUAL "genex")
        set(links "$<$<NOT:$<CONFIG:DEBUG>>:/elsewhere/lib/libssl.a>;$<$<CONFIG:DEBUG>:/elsewhere/debug/lib/libssl.a>")
    elseif(CASE STREQUAL "bare")
        set(links "ssl;crypto")
    elseif(CASE STREQUAL "target")
        set(links "OpenSSL::SSL;OpenSSL::Crypto")
    elseif(CASE STREQUAL "other")
        set(links "/elsewhere/lib/libssl.so")
    endif()
    add_library(fake_websockets INTERFACE IMPORTED)
    set_target_properties(fake_websockets PROPERTIES INTERFACE_LINK_LIBRARIES "${links}")
    _kalshi_check_websockets_openssl(fake_websockets)
""")


class WebsocketsOpenSslCheck(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if shutil.which("cmake") is None:
            raise unittest.SkipTest("cmake is not installed")
        cls.directory = tempfile.TemporaryDirectory()
        source = Path(cls.directory.name) / "source"
        source.mkdir()
        module = (ROOT / "cmake" / "KalshiWebsockets.cmake").as_posix()
        (source / "CMakeLists.txt").write_text(PROJECT.replace("@MODULE@", module))
        cls.source = source
        cls.build = Path(cls.directory.name) / "build"
        if "KALSHI-TEST no OpenSSL" in cls.configure("same"):
            cls.directory.cleanup()
            raise unittest.SkipTest("CMake finds no OpenSSL here")

    @classmethod
    def tearDownClass(cls) -> None:
        cls.directory.cleanup()

    @classmethod
    def configure(cls, case: str) -> str:
        result = subprocess.run(
            ["cmake", "-S", str(cls.source), "-B", str(cls.build), f"-DCASE={case}"],
            capture_output=True, text=True, check=True)
        # CMake wraps message text, so compare with whitespace collapsed.
        return " ".join((result.stdout + result.stderr).split())

    def test_the_sdks_own_libssl_is_accepted(self) -> None:
        self.assertNotIn("CMake Warning", self.configure("same"))

    def test_package_manager_generator_expressions_are_not_compared(self) -> None:
        self.assertNotIn("CMake Warning", self.configure("genex"))

    def test_bare_library_names_are_not_compared(self) -> None:
        self.assertNotIn("CMake Warning", self.configure("bare"))

    def test_the_sdks_openssl_targets_are_accepted(self) -> None:
        self.assertNotIn("CMake Warning", self.configure("target"))

    def test_a_libssl_from_another_directory_warns(self) -> None:
        self.assertIn(WARNING, self.configure("other"))


if __name__ == "__main__":
    unittest.main()
