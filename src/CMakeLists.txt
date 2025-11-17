find_package(glm REQUIRED CONFIG)
find_package(glfw3 REQUIRED CONFIG)

add_subdirectory(vk-bootstrap)

set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)
add_subdirectory(./tinygltf)

set(XXHASH_BUNDLED_MODE ON CACHE BOOL "" FORCE)
add_subdirectory(xxHash/cmake_unofficial)

set(IMGUI_SOURCES
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_vulkan.cpp
)

set(MIKKTSPACE_SOURCES
    MikkTSpace/mikktspace.c
)

SET(ENGINE_SOURCES
    engine.cpp
    imgui.cpp
    transport.cpp
    descriptor_pool.cpp
    texture_pool.cpp
    event.cpp
    rendering.cpp
    buffer.cpp
    texture.cpp
    synchronization.cpp
    camera.cpp
    model.cpp
    util.cpp
    scene.cpp
    gpu_group.cpp
    visbuffer.cpp
    compute.cpp
    texture_registry.cpp

    ${IMGUI_SOURCES}
    ${MIKKTSPACE_SOURCES}
)

add_library(goliath ${ENGINE_SOURCES})
target_include_directories(goliath
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
target_link_libraries(goliath PUBLIC glm::glm PRIVATE volk vulkan vk-bootstrap::vk-bootstrap glfw nfd xxHash::xxhash)
target_compile_definitions(goliath PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK=1)
