SET(GOIVIEW_SOURCES
    main.cpp
)

add_executable(goiview ${GOIVIEW_SOURCES})
target_link_libraries(goiview PRIVATE goliath)

add_library(goiview_plugin SHARED ${GOIVIEW_SOURCES})
target_compile_definitions(goiview_plugin PRIVATE GOIVIEW_PLUGIN)
target_link_libraries(goiview_plugin PRIVATE goliath)
set_target_properties(goiview_plugin PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(goiview PRIVATE /FS)
    set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>")
else()
    target_compile_options(goiview_plugin PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    )
endif()
