set(NFD_PORTAL ON CACHE BOOL "Use XDG portal backend" FORCE)
add_subdirectory(./nativefiledialog-extended)

SET(IMGUIZMO_SOURCES
    ImGuizmo/ImGuizmo.h
    ImGuizmo/ImGuizmo.cpp
)

SET(EDITOR_SOURCES
    main.cpp
    scene.cpp
    project.cpp
    ui.cpp
    exvars.cpp
)

add_executable(editor ${EDITOR_SOURCES} ${IMGUIZMO_SOURCES})
if(WIN32)
    set_target_properties(editor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(editor PRIVATE goliath nfd)

if(MSVC)
    target_compile_options(editor PRIVATE /FS)
    set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>")
endif()
