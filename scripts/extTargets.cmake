include(ExternalProject)

### 3D Models using Assimp

set(EXTERNAL_PROJ_DIR "${CMAKE_BINARY_DIR}/PROJECTS")

if(IS_DIRECTORY ${CMAKE_INSTALL_PREFIX}/lib64)
	set(EXTERNAL_LIB_DIR ${CMAKE_INSTALL_PREFIX}/lib64)
else()
	set(EXTERNAL_LIB_DIR ${CMAKE_INSTALL_PREFIX}/lib)
endif()

if(SUPPORT_MODELS)
	ExternalProject_Add(ASSIMP # 3D Model loading
		GIT_REPOSITORY "https://github.com/assimp/assimp.git"
		GIT_TAG "f6ee58e2489f3cbaee328655599f50c5c386a08e"

		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX} -DCMAKE_POLICY_VERSION_MINIMUM=3.5

		PREFIX ${EXTERNAL_PROJ_DIR}/Assimp
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
	)
        find_package(assimp PATHS ${CMAKE_INSTALL_PREFIX}/lib/scripts/assimp-6.0 ${EXTERNAL_LIB_DIR}/cmake/assimp-6.0)
endif()

if(SUPPORT_MODELS AND assimp_FOUND)
	if(assimp_FOUND)
		message(STATUS "Assimp loading success!")
		list(APPEND geometry_src plugin/model/Geo_Model_Node.cpp)
	else()
		message(WARNING "Assimp loading failure, 3D models support disabled")
	endif()
endif()

### Algorithmic Formations using OpenVDB

if(SUPPORT_ALGOFORMS)
	ExternalProject_Add(OpenVDB
		GIT_REPOSITORY "https://github.com/AcademySoftwareFoundation/openvdb.git"
		GIT_TAG "62798c70fcad6e1e0e5a5df49027cd77c38dd629"

		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX} -DCMAKE_POLICY_VERSION_MINIMUM=3.5
		        -DOPENVDB_BUILD_DEPENDENCIES=ON # Force OpenVDB to download and build its own dependencies
			-DOPENVDB_DEPENDENCY_INSTALL_DIR=<INSTALL_DIR> # Tell OpenVDB where to install those internal dependencies
			-DOPENVDB_BUILD_BINARIES=OFF
			-DOPENVDB_BUILD_PYTHON_MODULE=OFF
			-DOPENVDB_BUILD_UNIT_TESTS=OFF

		PREFIX ${EXTERNAL_PROJ_DIR}/OpenVDB
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
	)
        if(EXISTS ${EXTERNAL_LIB_DIR}/cmake/OpenVDB/FindOpenVDB.cmake)
		list(APPEND CMAKE_MODULE_PATH ${EXTERNAL_LIB_DIR}/cmake/OpenVDB)
		# include(${EXTERNAL_LIB_DIR}/cmake/OpenVDB/FindBlosc.cmake)
		include(${EXTERNAL_LIB_DIR}/cmake/OpenVDB/FindOpenVDB.cmake)
		message(STATUS "Ran OpenVDB find script, see headers: ${OpenVDB_INCLUDE_DIR} and libraries: ${OpenVDB_LIBRARIES}")
		set(OpenVDB_FOUND TRUE)
	else()
		message(STATUS "Could not find OpenVDB")
		set(OpenVDB_FOUND FALSE)
	endif()
	# find_package(OpenVDB PATHS ${CMAKE_INSTALL_PREFIX}/lib)
endif()

if(SUPPORT_ALGOFORMS AND OpenVDB_FOUND)
	if(OpenVDB_FOUND)
		message(STATUS "OpenVDB loading success!") # form sources are listed in internalTargets.cmake, OpenVDB adds grid conversions to them
	else()
		message(WARNING "OpenVDB loading failure, algorithmic formation support disabled")
	endif()
endif()

### Photogrammetry Calculations using OpenMVP

if(SUPPORT_PHOTOGRAM)
	ExternalProject_Add(OpenMVG
		GIT_REPOSITORY "https://github.com/openMVG/openMVG.git"
		GIT_TAG "6887e7385400f4cca378507296ccc20c8a0837dd"
		SOURCE_SUBDIR src

		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX} -DCMAKE_POLICY_VERSION_MINIMUM=3.5

		PREFIX ${EXTERNAL_PROJ_DIR}/OpenMVG
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
	)
        find_package(OpenMVG PATHS ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_PREFIX}/lib64)
endif()

if(SUPPORT_PHOTOGRAM AND OpenMVG_FOUND)
	if(OpenMVG_FOUND)
		message(STATUS "OpenMVG loading success!") # recon sources are listed in internalTargets.cmake, OpenMVG adds solved cameras to them
	else()
		message(WARNING "OpenMVG loading failure, photographic reconstruction support disabled")
	endif()
endif()

