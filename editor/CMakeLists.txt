set(NFD_PORTAL ON CACHE BOOL "Use XDG portal backend" FORCE)
add_subdirectory(./nativefiledialog-extended)

SET(IMGUIZMO_SOURCES
    ImGuizmo/ImGuizmo.h
    ImGuizmo/ImGuizmo.cpp
)

SET(EDITOR_SOURCES
    main.cpp
    models.cpp
    scene.cpp
    project.cpp
    ui.cpp
)

add_executable(editor ${EDITOR_SOURCES} ${IMGUIZMO_SOURCES})
target_link_libraries(editor PRIVATE goliath nfd)
