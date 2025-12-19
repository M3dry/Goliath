set(NFD_PORTAL ON CACHE BOOL "Use XDG portal backend" FORCE)
add_subdirectory(./nativefiledialog-extended)

SET(EDITOR_SOURCES
    main.cpp
    models.cpp
    scene.cpp
    project.cpp
)

add_executable(editor ${EDITOR_SOURCES})
target_link_libraries(editor PRIVATE goliath nfd)
