list(APPEND geometry_src
    core/Geo_Vertex.cpp
    core/Geo_Mesh.cpp
    core/meshes/Geo_Surface.cpp
    core/meshes/Geo_Volume.cpp
    core/meshes/Geo_Cone.cpp
    core/meshes/Geo_Orboid.cpp
)

if(SUPPORT_FORMS) # TODO: See if external dependencies or more robust system is needed for forms
    list(APPEND geometry_src plugin/form/Geo_Freeform.cpp)
endif()

add_library(GEOLIB SHARED ${geometry_src}) # Geometry Library
target_include_directories(GEOLIB PRIVATE core plugin maths)
set_target_properties(GEOLIB PROPERTIES FOLDER "Libraries")
target_include_directories(GEOLIB INTERFACE
    $<BUILD_INTERFACE:${interfaceHeaders}>
    $<INSTALL_INTERFACE:include/Geometron>
)

if(SUPPORT_MODELS AND Assimp_FOUND) # linking targets to Assimp
   # target_link_libraries(GEOLIB PUBLIC assimp::assimp ${irrXML_lib} ${zlib_lib}) # for release build
   target_link_libraries(GEOLIB PUBLIC assimp::assimp ${irrXMLd_lib} ${zlibd_lib}) # for debug build
   if(WIN32)
        set(zlib_lib "${CMAKE_INSTALL_PREFIX}/lib/zlibstatic.lib") # locating release lib
        set(zlibd_lib "${CMAKE_INSTALL_PREFIX}/lib/zlibstaticd.lib") # locating debug lib
        message("Assimp libraries zlib set to ${zlibd_lib}") # irrXML set to ${irrXMLd_lib}")

        target_link_libraries(GEOLIB PUBLIC ${zlibd_lib}) # for debug build
    elseif(UNIX AND NOT APPLE) # Unix Specific
        find_package(ZLIB)
        message("Assimp libraries zlib set to ${ZLIB_LIBRARIES}") # , irrXML set to ${irrXML_lib}")

        target_link_libraries(GEOLIB PUBLIC ${ZLIB_LIBRARIES}) # ${irrXML_lib})
    endif()
endif()
