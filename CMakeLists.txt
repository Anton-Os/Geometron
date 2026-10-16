cmake_minimum_required(VERSION 3.7...3.10)
project(Geometron DESCRIPTION "Geometron Mesh Library")

set (CMAKE_CXX_STANDARD 11)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/INSTALL" CACHE PATH "Install Path" FORCE)
    message("Install Prefix ${CMAKE_INSTALL_PREFIX}")
endif()

set(SUPPORT_MODELS TRUE CACHE BOOL "Include 3D model loading")
set(SUPPORT_FORMS TRUE CACHE BOOL "Include generative forms support")

if(SUPPORT_MODELS)
include("scripts/extTargets.cmake") # Builds external dependencies
endif()
include("scripts/internalTargets.cmake") # Builds GEOLIB
include("scripts/installTargets.cmake") # Installs GEOLIB and headers