#pragma once

#include "goliath/texture.hpp"
#include <filesystem>

#include <volk.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cassert>

#define VK_CHECK(x)                                                                                                    \
    do {                                                                                                               \
        VkResult err = x;                                                                                              \
        if (err) {                                                                                                     \
            fprintf(stderr, "Detected Vulkan error: %s @%d in %s\n", string_VkResult(err), __LINE__, __FILE__);        \
            assert(false);                                                                                             \
        }                                                                                                              \
    } while (0)

namespace engine {
    static constexpr std::size_t frames_in_flight = 2;
    static constexpr VkFormat swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;

    struct Init {
        const char* window_name;
        uint32_t texture_capacity = 1000;
        bool fullscreen = true;

        std::optional<std::filesystem::path> textures_directory{};
        std::optional<std::filesystem::path> models_directory{};
    };

    void init(Init opts);
    void destroy();

    bool shared();

    VkDevice device();
    VmaAllocator allocator();
    GLFWwindow* window();
    VkDescriptorSetLayout empty_set();

    VkCommandBuffer get_cmd_buf();

    VkFormat get_swapchain_format();
    size_t get_swapchain_count();
    VkImage get_swapchain();
    VkImageView get_swapchain_view();
    VkExtent2D get_swapchain_extent();

    bool prepare_frame();
    void prepare_draw();
    bool next_frame(std::span<VkSemaphoreSubmitInfo> extra_waits);
    void increment_frame();

    uint32_t get_current_frame();

    bool models_to_save();
    bool materials_to_save();
    bool textures_to_save();
    bool scenes_to_save();

    bool drawing_prepared();

    struct ForeignSwapchainState {
        VkFormat format;
        VkExtent2D extent;
        GPUImage* images;
        VkImageView* views;
    };

    void* get_internal_state();
}
