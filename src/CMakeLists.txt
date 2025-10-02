find_package(glm REQUIRED CONFIG)
find_package(glfw3 REQUIRED CONFIG)

add_subdirectory(vk-bootstrap)

set(IMGUI_SOURCES
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_vulkan.cpp
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

    ${IMGUI_SOURCES}
)

add_library(engine ${ENGINE_SOURCES})
target_compile_options(engine PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(engine PRIVATE ${COMMON_LINK_OPTIONS})
target_include_directories(engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends")
target_link_libraries(engine PUBLIC glm::glm PRIVATE volk vulkan vk-bootstrap::vk-bootstrap glfw)
target_compile_definitions(engine PUBLIC ENGINE_MODE PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK=1)

add_executable(engine_demo engine_demo.cpp)
target_compile_options(engine_demo PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(engine_demo PRIVATE ${COMMON_LINK_OPTIONS})
target_link_libraries(engine_demo PRIVATE engine)
