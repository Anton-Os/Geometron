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
endif()