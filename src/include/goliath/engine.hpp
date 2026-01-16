#pragma once

#include <filesystem>

#include <volk.h>
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <vector>

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

    extern GLFWwindow* window;

    extern VkInstance instance;
    extern VkPhysicalDevice physical_device;
    extern VkPhysicalDeviceProperties physical_device_properties;
    extern VkDevice device;
    extern VmaAllocator allocator;
    extern VkSurfaceKHR surface;

    constexpr VkFormat swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    extern VkExtent2D swapchain_extent;
    extern VkSwapchainKHR swapchain;
    extern std::vector<VkImage> swapchain_images;
    extern std::vector<VkImageView> swapchain_image_views;

    extern VkQueue graphics_queue;
    extern uint32_t graphics_queue_family;
    extern VkQueue transport_queue;
    extern uint32_t transport_queue_family;

    struct Init{
        const char* window_name;
        uint32_t texture_capacity = 1000;
        bool fullscreen = true;

        std::optional<std::filesystem::path> textures_directory{};
        std::optional<std::filesystem::path> models_directory{};
    };

    void init(Init opts);
    void destroy();

    VkCommandBuffer get_cmd_buf();
    VkImage get_swapchain();
    VkImageView get_swapchain_view();
    bool prepare_frame();
    void prepare_draw();
    bool next_frame();

    uint32_t get_current_frame();

    bool models_to_save();
    bool materials_to_save();
    bool textures_to_save();
}
