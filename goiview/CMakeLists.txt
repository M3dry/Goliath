SET(GOIVIEW_SOURCES
    main.cpp
)

add_executable(goiview ${GOIVIEW_SOURCES})
target_link_libraries(goiview PRIVATE goliath)

if(MSVC)
    target_compile_options(goiview PRIVATE /FS)
    set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>")
endif()
