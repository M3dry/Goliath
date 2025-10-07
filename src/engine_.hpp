#pragma once

#include "descriptor_pool_.hpp"
#include <volk.h>

namespace engine {
    struct FrameData {
        VkCommandPool cmd_pool;
        VkCommandBuffer cmd_buf;
        VkSemaphore swapchain_semaphore;
        std::size_t render_semaphore{(std::size_t)-1};
        VkFence render_fence;
        DescriptorPool descriptor_pool;

        FrameData();
        ~FrameData();
    };

    static constexpr std::size_t frames_in_flight = 3;
    extern FrameData* frames;

    DescriptorPool& get_frame_descriptor_pool();
}
