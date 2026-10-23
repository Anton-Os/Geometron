include(ExternalProject)

### 3D Models using Assimp

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
        find_package(OpenVDB PATHS ${CMAKE_INSTALL_PREFIX}/lib)
endif()

if(SUPPORT_ALGOFORMS OR OpenVDB_FOUND)
	if(OpenVDB_FOUND)
		message(STATUS "OpenVDB loading success!")
		list(APPEND geometry_src plugin/form/Geo_Freeform.cpp)
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
        find_package(OpenMVG PATHS ${CMAKE_INSTALL_PREFIX}/lib)
endif()

if(SUPPORT_PHOTOGRAM OR OpenMVG_FOUND)
	if(OpenMVG_FOUND)
		message(STATUS "OpenMVG loading success!")
		# list(APPEND geometry_src plugin/recon/Geo_Photogram.cpp) TODO: Add source
	else()
		message(WARNING "OpenMVG loading failure, photographic reconstruction support disabled")
	endif()
endif()

