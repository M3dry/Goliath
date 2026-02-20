#pragma once

#include "descriptor_pool_.hpp"
#include "goliath/engine.hpp"
#include "goliath/texture.hpp"
#include <mutex>
#include <volk.h>
#include <vulkan/vulkan_core.h>

namespace engine {
    struct OldSwapchain {
        VkSwapchainKHR swapchain;
        std::vector<VkSemaphore> semaphores;
        uint64_t last_used_timeline;
    };

    struct FrameData {
        VkCommandPool cmd_pool;
        VkCommandBuffer cmd_buf;
        VkSemaphore swapchain_semaphore;
        std::size_t render_semaphore{(std::size_t)-1};
        VkFence render_fence;
        DescriptorPool descriptor_pool;
        std::vector<std::pair<VkBuffer, VmaAllocation>> buffers_to_free{};
        std::vector<std::pair<VkImage, VmaAllocation>> images_to_free{};
        std::vector<VkImageView> views_to_free{};
        std::vector<VkSampler> samplers_to_free{};
        std::vector<OldSwapchain> swapchains_to_free{};

        void cleanup_resources();
        void destroy_buffer(VkBuffer buf, VmaAllocation alloc);
        void destroy_image(VkImage img, VmaAllocation alloc);
        void destroy_view(VkImageView view);
        void destroy_sampler(VkSampler sampler);
        void destroy_swapchain(OldSwapchain swapchain);

        FrameData();
        ~FrameData();
    };

    struct SwapchainState {
        static constexpr VkFormat format = swapchain_format;

        bool updated_window_size = false;
        VkExtent2D swapchain_extent;
        VkSwapchainKHR swapchain = nullptr;
        std::vector<VkImage> swapchain_images{};
        std::vector<VkImageView> swapchain_image_views{};
        std::vector<VkSemaphore> swapchain_semaphores{};
    };

    struct ForeignSwapchainState {
        VkFormat format;
        VkExtent2D extent;
        GPUImage* images;
        VkImageView* views;
    };

    struct State {
        GLFWwindow* window;

        VkInstance instance;
        VkDebugUtilsMessengerEXT debug_messenger;

        VkPhysicalDevice physical_device;
        VkPhysicalDeviceProperties physical_device_properties;
        VkDevice device;
        VmaAllocator allocator;
        VkSurfaceKHR surface;

        std::mutex graphics_queue_lock{};
        VkQueue graphics_queue;
        uint32_t graphics_queue_family;
        VkQueue transport_queue;
        uint32_t transport_queue_family;

        FrameData* frames;
        uint8_t current_frame = 0;

        uint32_t swapchain_ix;

        bool models_to_save_{false};
        bool materials_to_save_{false};
        bool textures_to_save_{false};

        uint64_t timeline_value = 0;
        uint64_t presented_timeline_value = 0;
        VkSemaphore timeline_semaphore{};

        bool _drawing_prepared = false;

        VkCommandPool barriers_cmd_pool;
        VkFence barriers_cmd_buf_fence;
        VkCommandBuffer barriers_cmd_buf;

        VkDescriptorSetLayout empty_set;
    };

    extern State* state;
    void share_load(State* state_ptr, ForeignSwapchainState* swapchain_ptr);

    FrameData& get_current_frame_data();
    DescriptorPool& get_frame_descriptor_pool();
    void destroy_buffer(VkBuffer buf, VmaAllocation alloc);
    void destroy_image(VkImage img, VmaAllocation alloc);
    void destroy_view(VkImageView view);
    void destroy_sampler(VkSampler sampler);

    void new_window_size(uint32_t width, uint32_t height);
}
