set(NFD_PORTAL ON CACHE BOOL "Use XDG portal backend" FORCE)
add_subdirectory(./nativefiledialog-extended)

set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)
add_subdirectory(./tinygltf)

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
    state.cpp
    game.cpp
    textures.cpp
    layout.cpp
    error_stack.cpp
    gltf.cpp
    util.cpp
)

add_executable(editor ${EDITOR_SOURCES} ${IMGUIZMO_SOURCES})
if(WIN32)
    set_target_properties(editor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(editor PRIVATE goliath nfd)

if(MSVC)
    target_compile_options(editor PRIVATE /FS /Zc:preprocessor)
    set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>")
else()
    target_compile_options(editor PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    )
endif()
