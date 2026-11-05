# Geo_DepCheck: small program proving OpenVDB and OpenMVG compile, link and run on this platform

if(OpenVDB_FOUND OR OpenMVG_FOUND)
    add_executable(Geo_DepCheck exporter/Geo_DepCheck.cpp)
    set_target_properties(Geo_DepCheck PROPERTIES FOLDER "Tools")
    target_compile_features(Geo_DepCheck PRIVATE cxx_std_17)
    if(OpenVDB_FOUND)
        target_link_libraries(Geo_DepCheck PRIVATE OpenVDB::openvdb)
        target_compile_definitions(Geo_DepCheck PRIVATE GEO_ENABLE_ALGOFORMS)
    endif()
    if(OpenMVG_FOUND)
        target_link_libraries(Geo_DepCheck PRIVATE OpenMVG::openMVG_sfm OpenMVG::openMVG_multiview OpenMVG::openMVG_image)
        target_compile_definitions(Geo_DepCheck PRIVATE GEO_ENABLE_PHOTOGRAM)
    endif()
    if(MSVC)
        target_compile_options(Geo_DepCheck PRIVATE /bigobj)
        target_compile_definitions(Geo_DepCheck PRIVATE NOMINMAX _USE_MATH_DEFINES)
    endif()
    if(COMMAND geo_copy_dependency_runtime)
        geo_copy_dependency_runtime(Geo_DepCheck)
    endif()

    enable_testing()
    add_test(NAME Geo_DepCheck COMMAND Geo_DepCheck)
endif()

# Geo_Exporter: command line tool encoding Geo_Mesh and Geo_Construct types into 3D model files

if(NOT (SUPPORT_MODELS AND Assimp_FOUND))
    message(WARNING "Geo_Exporter disabled, requires Assimp (SUPPORT_MODELS)")
    return()
endif()

add_executable(Geo_Exporter exporter/Geo_Exporter.cpp)
target_include_directories(Geo_Exporter PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/plugin/model
    ${CMAKE_SOURCE_DIR}/support
)
target_compile_definitions(Geo_Exporter PRIVATE TOPL_ENABLE_MODELS)
target_link_libraries(Geo_Exporter PRIVATE GEOLIB)
set_target_properties(Geo_Exporter PROPERTIES FOLDER "Tools")

if(WIN32) # copies runtime libraries next to the executable so it can run from the build tree
    add_custom_command(TARGET Geo_Exporter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:GEOLIB> $<TARGET_FILE_DIR:Geo_Exporter>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:assimp::assimp> $<TARGET_FILE_DIR:Geo_Exporter>
    )
    if(COMMAND geo_copy_dependency_runtime) # OpenVDB and TBB DLLs used by GEOLIB
        geo_copy_dependency_runtime(Geo_Exporter)
    endif()
endif()