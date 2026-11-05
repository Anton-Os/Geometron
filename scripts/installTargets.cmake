install(TARGETS GEOLIB
     EXPORT Geometron
     RUNTIME DESTINATION bin
     LIBRARY DESTINATION lib
     ARCHIVE DESTINATION lib
     FRAMEWORK DESTINATION lib
)

file(GLOB_RECURSE interfaceHeaders 
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.h
)

install(FILES ${interfaceHeaders} DESTINATION include/Geometron)
install(EXPORT Geometron DESTINATION lib/Geometron FILE GeometronConfig.cmake)

# For Exporter

if(SUPPORT_MODELS AND Assimp_FOUND)
    install(TARGETS Geo_Exporter RUNTIME DESTINATION bin)
endif()

# Shared libraries built by extBuilder.cmake (OpenVDB, TBB) are needed next to GEOLIB at runtime

if(COMMAND geo_dependency_runtime_files)
    geo_dependency_runtime_files(geoDependencyRuntime)
    if(WIN32)
        install(FILES ${geoDependencyRuntime} DESTINATION bin)
    else()
        install(FILES ${geoDependencyRuntime} DESTINATION lib)
    endif()
endif()
