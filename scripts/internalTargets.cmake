list(APPEND geometry_src
    core/Geo_Vertex.cpp
    core/Geo_Mesh.cpp
    core/meshes/Geo_Surface.cpp
    core/meshes/Geo_Volume.cpp
    core/meshes/Geo_Cone.cpp
    core/meshes/Geo_Orboid.cpp
)

if(SUPPORT_FORMS OR SUPPORT_ALGOFORMS) # forms build without external dependencies, OpenVDB is optional
    list(APPEND geometry_src
        plugin/form/Geo_Freeform.cpp
        plugin/form/Geo_Voxelform.cpp
        plugin/form/Geo_Composite.cpp
        plugin/form/Geo_Recursive.cpp
    )
endif()

if(SUPPORT_RECON OR SUPPORT_PHOTOGRAM) # reconstruction builds without external dependencies, OpenMVG is optional
    list(APPEND geometry_src
        plugin/recon/Geo_Heightmap.cpp
        plugin/recon/Geo_Scanmap.cpp
    )
endif()

add_library(GEOLIB SHARED ${geometry_src}) # Geometry Library
target_include_directories(GEOLIB PRIVATE core plugin support)
set_target_properties(GEOLIB PROPERTIES FOLDER "Libraries")
target_include_directories(GEOLIB INTERFACE
    $<BUILD_INTERFACE:${interfaceHeaders}>
    $<INSTALL_INTERFACE:include/Geometron>
)

<<<<<<< HEAD
if(SUPPORT_ALGOFORMS AND OpenVDB_FOUND) # enables OpenVDB sections of plugin/form
    set_target_properties(GEOLIB PROPERTIES CXX_STANDARD 17) # required by OpenVDB headers
    target_compile_definitions(GEOLIB PUBLIC GEO_ENABLE_OPENVDB)
    if(TARGET OpenVDB::openvdb)
        target_link_libraries(GEOLIB PUBLIC OpenVDB::openvdb)
    else()
        target_include_directories(GEOLIB PUBLIC ${OpenVDB_INCLUDE_DIRS})
        target_link_libraries(GEOLIB PUBLIC ${OpenVDB_LIBRARIES})
    endif()
endif()

if(SUPPORT_PHOTOGRAM AND OpenMVG_FOUND) # enables OpenMVG sections of plugin/recon
    target_compile_definitions(GEOLIB PUBLIC GEO_ENABLE_OPENMVG)
    if(TARGET OpenMVG::openMVG_sfm)
        target_link_libraries(GEOLIB PUBLIC OpenMVG::openMVG_sfm)
    endif()
endif()

=======
>>>>>>> da31b76 (Build System Unix Updates)
if(SUPPORT_MODELS AND assimp_FOUND) # linking targets to Assimp
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
