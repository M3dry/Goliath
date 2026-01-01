#pragma once

#include "descriptor_pool_.hpp"
#include <Volk/volk.h>

namespace engine {
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

        void cleanup_resources();
        void destroy_buffer(VkBuffer buf, VmaAllocation alloc);
        void destroy_image(VkImage img, VmaAllocation alloc);
        void destroy_view(VkImageView view);
        void destroy_sampler(VkSampler sampler);

        FrameData();
        ~FrameData();
    };

    extern FrameData* frames;

    DescriptorPool& get_frame_descriptor_pool();
    void destroy_buffer(VkBuffer buf, VmaAllocation alloc);
    void destroy_image(VkImage img, VmaAllocation alloc);
    void destroy_view(VkImageView view);
}
