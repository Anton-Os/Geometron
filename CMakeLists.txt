cmake_minimum_required(VERSION 3.24...3.31) # 3.24 is required by OpenVDB, which is built with the same CMake
project(Geometron DESCRIPTION "Geometron Mesh Library")

set (CMAKE_CXX_STANDARD 11)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/INSTALL" CACHE PATH "Install Path" FORCE)
    message("Install Prefix ${CMAKE_INSTALL_PREFIX}")
endif()

set(SUPPORT_MODELS TRUE CACHE BOOL "Include mesh from model loading with Assimp" FORCE)
set(SUPPORT_ALGOFORMS FALSE CACHE BOOL "Include mesh from algo support with OpenVDB")
set(SUPPORT_PHOTOGRAM FALSE CACHE BOOL "Include mesh from image reconstruction with OpenMVG")

include("scripts/extTargets.cmake") # Builds external dependencies
include("scripts/internalTargets.cmake") # Builds GEOLIB
include("scripts/demoTargets.cmake") # Builds demo exporter
include("scripts/installTargets.cmake") # Installs GEOLIB and headers
