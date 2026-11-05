include(ExternalProject)

### 3D Models using Assimp

set(EXTERNAL_PROJ_DIR "${CMAKE_BINARY_DIR}/PROJECTS")

if(SUPPORT_MODELS)
	ExternalProject_Add(ASSIMP # 3D Model loading
		GIT_REPOSITORY "https://github.com/assimp/assimp.git"
		GIT_TAG "f6ee58e2489f3cbaee328655599f50c5c386a08e"

		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX} -DCMAKE_POLICY_VERSION_MINIMUM=3.5

		PREFIX ${EXTERNAL_PROJ_DIR}/Assimp
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
	)
        find_package(Assimp PATHS ${CMAKE_INSTALL_PREFIX}/lib/scripts/assimp-6.0)
endif()

if(SUPPORT_MODELS OR Assimp_FOUND)
	if(Assimp_FOUND)
		message(STATUS "Assimp loading success!")
		list(APPEND geometry_src plugin/model/Geo_Model_Node.cpp)
	else()
		message(WARNING "Assimp loading failure, 3D models support disabled")
	endif()
endif()

### OpenVDB and OpenMVG are built while configuring (see extBuilder.cmake) so they are found on the first configure

set(GEO_BUILD_DEPENDENCIES TRUE CACHE BOOL "Download and build OpenVDB (with oneTBB) and OpenMVG automatically. Turn off to use your own installs (vcpkg, OpenVDB_ROOT, OpenMVG_DIR)")
set(GEO_OPENMVG_OPENMP TRUE CACHE BOOL "Build OpenMVG with OpenMP parallelization")

get_property(GEO_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG) # Visual Studio, Xcode, Ninja Multi-Config

if((SUPPORT_ALGOFORMS OR SUPPORT_PHOTOGRAM) AND GEO_BUILD_DEPENDENCIES)
    include("${CMAKE_CURRENT_LIST_DIR}/extBuilder.cmake")
    if(GEO_IS_MULTI_CONFIG) # Debug and Release libraries share one prefix, so Debug files get a suffix
        set(GEO_DEPS_DEBUG_POSTFIX "-DCMAKE_DEBUG_POSTFIX:STRING=_d")
    endif()
endif()

### Algorithmic Formations using OpenVDB

if(SUPPORT_ALGOFORMS)
    if(GEO_BUILD_DEPENDENCIES)
        geo_build_dependency(TBB # threading library, the one required dependency of OpenVDB 13
            GIT_REPOSITORY "https://github.com/uxlfoundation/oneTBB.git"
            GIT_TAG "v2022.3.0"
            CMAKE_ARGS
                -DTBB_TEST=OFF
                -DTBB_EXAMPLES=OFF
                -DTBB_STRICT=OFF # do not turn warnings into errors on newer compilers
                -DTBB_DISABLE_HWLOC_AUTOMATIC_SEARCH=ON
        )
        geo_build_dependency(OpenVDB
            GIT_REPOSITORY "https://github.com/AcademySoftwareFoundation/openvdb.git"
            GIT_TAG "62798c70fcad6e1e0e5a5df49027cd77c38dd629" # OpenVDB 13.1.1, needs CMake 3.24 and Visual Studio 2022
            CMAKE_ARGS
                -DOPENVDB_BUILD_CORE=ON
                -DOPENVDB_CORE_SHARED=ON
                -DOPENVDB_CORE_STATIC=OFF # on Windows the static and shared libraries would both be openvdb.lib
                -DOPENVDB_BUILD_BINARIES=OFF
                -DOPENVDB_BUILD_PYTHON_MODULE=OFF
                -DOPENVDB_BUILD_UNITTESTS=OFF
                -DOPENVDB_BUILD_DOCS=OFF
                -DOPENVDB_BUILD_AX=OFF
                -DOPENVDB_BUILD_NANOVDB=OFF
                -DOPENVDB_INSTALL_CMAKE_MODULES=ON # installs FindOpenVDB.cmake used below
                -DOPENVDB_ENABLE_UNINSTALL=OFF
                -DOPENVDB_FUTURE_DEPRECATION=OFF
                -DUSE_BLOSC=OFF # Blosc and zlib only compress .vdb files; leaving them out avoids two more dependencies
                -DUSE_ZLIB=OFF
                -DUSE_EXR=OFF
                -DUSE_IMATH_HALF=OFF # uses the half type bundled with OpenVDB
                -DUSE_PNG=OFF
                -DUSE_LOG4CPLUS=OFF
                -DUSE_PKGCONFIG=OFF
                -DUSE_CCACHE=OFF
                -DDISABLE_CMAKE_SEARCH_PATHS=ON # only take TBB from TBB_ROOT, never a copy on PATH (Anaconda, oneAPI)
                "-DTBB_ROOT:PATH=${GEO_DEPS_PREFIX}"
                "-DMSVC_MP_THREAD_COUNT:STRING=${GEO_DEPS_JOBS}" # parallel compilation inside each MSVC project
                ${GEO_DEPS_DEBUG_POSTFIX}
        )
        set(TBB_ROOT "${GEO_DEPS_PREFIX}")
        set(OpenVDB_ROOT "${GEO_DEPS_PREFIX}")
        set(DISABLE_CMAKE_SEARCH_PATHS ON)
    endif()

    # FindOpenVDB.cmake is installed with OpenVDB rather than shipped with CMake
    if(OpenVDB_ROOT)
        list(PREPEND CMAKE_MODULE_PATH "${OpenVDB_ROOT}/lib/cmake/OpenVDB" "${OpenVDB_ROOT}/lib64/cmake/OpenVDB")
    endif()
    find_package(OpenVDB QUIET COMPONENTS openvdb)
    unset(DISABLE_CMAKE_SEARCH_PATHS)

    # FindOpenVDB only knows one library file, so register the Debug one (openvdb_d) for Visual Studio Debug builds
    if(OpenVDB_FOUND AND GEO_IS_MULTI_CONFIG AND TARGET OpenVDB::openvdb)
        get_target_property(_vdb_release OpenVDB::openvdb IMPORTED_LOCATION)
        get_filename_component(_vdb_libdir "${_vdb_release}" DIRECTORY)
        find_library(_vdb_debug NAMES openvdb_d PATHS "${_vdb_libdir}" NO_DEFAULT_PATH NO_CACHE)
        if(_vdb_debug)
            set_target_properties(OpenVDB::openvdb PROPERTIES
                IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
                IMPORTED_LOCATION_RELEASE "${_vdb_release}"
                IMPORTED_LOCATION_DEBUG "${_vdb_debug}"
            )
        else()
            message(WARNING "OpenVDB Debug library not found in ${_vdb_libdir}, Debug builds will link the Release library (mismatched MSVC runtimes)")
        endif()
    endif()
endif()

if(SUPPORT_ALGOFORMS OR OpenVDB_FOUND)
	if(OpenVDB_FOUND)
		message(STATUS "OpenVDB loading success! (${OpenVDB_VERSION})")
		list(APPEND geometry_src plugin/form/Geo_Freeform.cpp)
	else()
		message(WARNING "OpenVDB loading failure, algorithmic formation support disabled")
	endif()
endif()

### Photogrammetry Calculations using OpenMVG

if(SUPPORT_PHOTOGRAM)
    if(GEO_BUILD_DEPENDENCIES)
        geo_build_dependency(OpenMVG # bundles Eigen, Ceres, cereal, Clp/Osi, lemon and the jpeg/png/tiff/zlib codecs
            GIT_REPOSITORY "https://github.com/openMVG/openMVG.git"
            GIT_TAG "6887e7385400f4cca378507296ccc20c8a0837dd"
            SOURCE_SUBDIR src
            CMAKE_ARGS
                -DOpenMVG_BUILD_SHARED=OFF # OpenMVG does not export DLL symbols, static libraries go inside GEOLIB
                -DOpenMVG_BUILD_TESTS=OFF
                -DOpenMVG_BUILD_DOC=OFF
                -DOpenMVG_BUILD_EXAMPLES=OFF
                -DOpenMVG_BUILD_OPENGL_EXAMPLES=OFF
                -DOpenMVG_BUILD_SOFTWARES=OFF
                -DOpenMVG_BUILD_GUI_SOFTWARES=OFF # would require Qt
                -DOpenMVG_BUILD_COVERAGE=OFF
                -DOpenMVG_USE_RERUN=OFF # would download the Rerun SDK and Apache Arrow at configure time
                -DOpenMVG_USE_OPENCV=OFF
                -DOpenMVG_USE_OCVSIFT=OFF
                "-DOpenMVG_USE_OPENMP:BOOL=${GEO_OPENMVG_OPENMP}"
                -DTARGET_ARCHITECTURE=generic # no host-specific AVX flags that would change Eigen's memory layout
                -DSCHUR_SPECIALIZATIONS=OFF # bundled Ceres: much faster build
                -DLAPACK=OFF # bundled Ceres: no system BLAS/LAPACK
                # Always use the bundled copies so nothing is picked up from PATH (Anaconda, vcpkg, Strawberry Perl...)
                -DCMAKE_DISABLE_FIND_PACKAGE_JPEG=ON
                -DCMAKE_DISABLE_FIND_PACKAGE_PNG=ON
                -DCMAKE_DISABLE_FIND_PACKAGE_TIFF=ON
                -DCMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON
                -DCMAKE_DISABLE_FIND_PACKAGE_Ceres=ON
                -DCMAKE_DISABLE_FIND_PACKAGE_cereal=ON
                # Bundled Eigen otherwise installs to include/eigen3 while OpenMVG's exported targets expect this folder
                "-DEIGEN_INCLUDE_INSTALL_DIR:PATH=${GEO_DEPS_PREFIX}/include/openMVG/third_party/eigen"
                ${GEO_DEPS_DEBUG_POSTFIX}
        )
        set(OpenMVG_DIR "${GEO_DEPS_PREFIX}/lib/openMVG/cmake")
    endif()
    find_package(OpenMVG CONFIG QUIET)
endif()

if(SUPPORT_PHOTOGRAM OR OpenMVG_FOUND)
	if(OpenMVG_FOUND)
		message(STATUS "OpenMVG loading success! (${OPENMVG_VERSION})")
		# list(APPEND geometry_src plugin/recon/Geo_Photogram.cpp) TODO: Add source
	else()
		message(WARNING "OpenMVG loading failure, photographic reconstruction support disabled")
	endif()
endif()
