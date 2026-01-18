#pragma once

#include "descriptor_pool_.hpp"
#include "goliath/engine.hpp"
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

    extern FrameData* frames;

    FrameData& get_current_frame_data();
    DescriptorPool& get_frame_descriptor_pool();
    void destroy_buffer(VkBuffer buf, VmaAllocation alloc);
    void destroy_image(VkImage img, VmaAllocation alloc);
    void destroy_view(VkImageView view);
    void destroy_sampler(VkSampler sampler);

    void new_window_size(uint32_t width, uint32_t height);
}
