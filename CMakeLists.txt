# PaintFromRef - Nuke NDK plugin (Nuke 14.1 - 17.1; Windows/VS2019, Linux/gcc)
#
# One build dir per Nuke version (see scripts/build_all.ps1). Windows:
#   cmake -G "Visual Studio 16 2019" -A x64 ^
#         -DNuke_DIR="C:/Program Files/Nuke16.0v8/cmake" -S . -B build-16.0v8
#   cmake --build build-16.0v8 --config Release
#   cmake --install build-16.0v8 --config Release --prefix "%USERPROFILE%/.nuke"
# Linux (gcc 9 for Nuke 14, gcc 11 for 15-17):
#   cmake -DNuke_DIR=/usr/local/Nuke16.0v8/cmake -S . -B build-16.0v8
#   cmake --build build-16.0v8 && cmake --install build-16.0v8 --prefix ~/.nuke
#
# The binary installs into PaintFromRef/<major.minor>/ and the shipped
# init.py picks the folder matching the running Nuke at startup.

cmake_minimum_required(VERSION 3.16)
project(PaintFromRef CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Multiple Nukes are installed here; newer ones register themselves in the
# CMake user-package registry and would hijack find_package (NDK_NOTES 11.2).
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
find_package(Nuke REQUIRED)

# major.minor of the target Nuke, for the per-version install subfolder.
# Parsed from Nuke_DIR (".../Nuke16.0v8/cmake"); override with -DPFR_NUKE_MM.
if(NOT PFR_NUKE_MM)
    string(REGEX MATCH "Nuke([0-9]+\\.[0-9]+)v" _pfr_match "${Nuke_DIR}")
    set(PFR_NUKE_MM "${CMAKE_MATCH_1}")
endif()
if(NOT PFR_NUKE_MM)
    message(FATAL_ERROR
        "Cannot parse Nuke major.minor from Nuke_DIR='${Nuke_DIR}'; "
        "pass -DPFR_NUKE_MM=<major.minor>")
endif()
message(STATUS "PaintFromRef: targeting Nuke ${PFR_NUKE_MM}")

add_library(PaintFromRef MODULE
    src/PaintFromRef.cpp
    src/Painterly.cpp
)

# Loadable name must exactly match the Op::Description name on every
# platform: no 'lib' prefix on Linux (NDK_NOTES 1.7, 57)
set_target_properties(PaintFromRef PROPERTIES PREFIX "")

target_compile_definitions(PaintFromRef PRIVATE
    NOMINMAX
    _USE_MATH_DEFINES
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(PaintFromRef PRIVATE /W3 /EHsc)
else()
    target_compile_options(PaintFromRef PRIVATE -Wall -Wreorder)
endif()

target_link_libraries(PaintFromRef PRIVATE Nuke::NDK)

install(TARGETS PaintFromRef DESTINATION PaintFromRef/${PFR_NUKE_MM})
install(FILES python/pfr_builder.py python/menu.py python/init.py
        DESTINATION PaintFromRef)
foreach(_doc IN ITEMS LICENSE README.md)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_doc}")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_doc}"
                DESTINATION PaintFromRef)
    endif()
endforeach()
install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/install_user_menu.cmake")
