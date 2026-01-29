if(DEFINED ENV{VULKAN_SDK})
    add_library(glm::glm INTERFACE IMPORTED)
    target_include_directories(glm::glm INTERFACE
        "$ENV{VULKAN_SDK}/Include"
    )
else()
    find_package(glm REQUIRED CONFIG)
endif()

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

add_subdirectory(vk-bootstrap)

find_package(Vulkan REQUIRED)

if(WIN32 AND NOT TARGET volk::volk)
    message(STATUS "volk::volk not provided by Vulkan package, creating compatibility target")

    add_library(volk::volk INTERFACE IMPORTED)
    target_include_directories(volk::volk INTERFACE
        "$ENV{VULKAN_SDK}/Include/volk"
    )
    target_link_libraries(volk::volk INTERFACE Vulkan::Vulkan)
endif()

if(UNIX)
    find_package(volk REQUIRED CONFIG)
endif()

if(WIN32 AND NOT TARGET GPUOpen::VulkanMemoryAllocator)
    add_library(GPUOpen::VulkanMemoryAllocator INTERFACE IMPORTED)
    target_include_directories(
        GPUOpen::VulkanMemoryAllocator
        INTERFACE "$ENV{VULKAN_SDK}/Include/vma"
    )
else()
    find_package(VulkanMemoryAllocator REQUIRED CONFIG)
endif()

set(XXHASH_BUNDLED_MODE ON CACHE BOOL "" FORCE)
add_subdirectory(xxHash/cmake_unofficial)

add_subdirectory(./json)

set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)
add_subdirectory(./tinygltf)

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
    transport2.cpp
    descriptor_pool.cpp
    texture_pool.cpp
    event.cpp
    rendering.cpp
    buffer.cpp
    texture.cpp
    synchronization.cpp
    camera.cpp
    model.cpp
    models.cpp
    gltf.cpp
    util.cpp
    gpu_group.cpp
    visbuffer.cpp
    compute.cpp
    samplers.cpp
    textures.cpp
    material.cpp
    materials.cpp
    culling.cpp
    exvar.cpp
    dyn_module.cpp

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
target_link_libraries(goliath PUBLIC glm::glm nlohmann_json::nlohmann_json glfw GPUOpen::VulkanMemoryAllocator volk::volk PRIVATE vk-bootstrap::vk-bootstrap xxHash::xxhash)
if(UNIX AND NOT APPLE)
    target_link_libraries(goliath PRIVATE X11) # needed for ImGui GLFW layer
endif()

target_compile_definitions(goliath PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK=1)

if(MSVC)
    target_compile_options(goliath PRIVATE /FS)
    set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb/$<CONFIG>")
endif()
