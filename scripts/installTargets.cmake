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