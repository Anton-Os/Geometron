include(ExternalProject)

ExternalProject_Add(ASSIMP # 3D Model loading
    GIT_REPOSITORY "https://github.com/assimp/assimp.git"
    GIT_TAG "f6ee58e2489f3cbaee328655599f50c5c386a08e"

    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX} -DCMAKE_POLICY_VERSION_MINIMUM=3.5

    PREFIX ${EXTERNAL_PROJ_DIR}/Assimp
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
)
find_package(Assimp PATHS ${CMAKE_INSTALL_PREFIX}/lib/scripts/assimp-5.0)
if(Assimp_FOUND)
    message(STATUS "Assimp loading success!")
else()
    message(WARNING "Assimp loading failure, 3D models support disabled")
endif()

if(SUPPORT_MODELS AND Assimp_FOUND)
    list(APPEND geometry_src plugin/model/Geo_Model_Node.cpp)
endif()