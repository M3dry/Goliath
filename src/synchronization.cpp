#include "goliath/synchronization.hpp"
#include "goliath/engine.hpp"
#include <vulkan/vulkan_core.h>

namespace engine::synchronization {
    std::vector<VkImageMemoryBarrier2> image_barriers{};
    std::vector<VkBufferMemoryBarrier2> buffer_barriers{};
    std::vector<VkMemoryBarrier2> memory_barriers{};

    void begin_barriers() {
    }

    void end_barriers() {
        if (drawing_prepared()) {
            VkDependencyInfo dep_info{};
            dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_info.pNext = nullptr;
            dep_info.imageMemoryBarrierCount = (uint32_t)image_barriers.size();
            dep_info.pImageMemoryBarriers = image_barriers.data();
            dep_info.bufferMemoryBarrierCount = (uint32_t)buffer_barriers.size();
            dep_info.pBufferMemoryBarriers = buffer_barriers.data();
            dep_info.memoryBarrierCount = (uint32_t)memory_barriers.size();
            dep_info.pMemoryBarriers = memory_barriers.data();

            vkCmdPipelineBarrier2(get_cmd_buf(), &dep_info);
            image_barriers.clear();
            buffer_barriers.clear();
            memory_barriers.clear();
        } else {
            submit_from_another_thread(buffer_barriers, image_barriers, memory_barriers, std::nullopt, std::nullopt);
            image_barriers.clear();
            buffer_barriers.clear();
            memory_barriers.clear();
        }
    }

    void apply_barrier(VkImageMemoryBarrier2 barrier) {
        image_barriers.emplace_back(barrier);
    }

    void apply_barrier(VkBufferMemoryBarrier2 barrier) {
        buffer_barriers.emplace_back(barrier);
    }

    void apply_barrier(VkMemoryBarrier2 barrier) {
        memory_barriers.emplace_back(barrier);
    }

    void submit_from_another_thread(std::span<VkBufferMemoryBarrier2> bufs, std::span<VkImageMemoryBarrier2> images,
                                    std::span<VkMemoryBarrier2> general, std::optional<VkSemaphoreSubmitInfo> wait_info,
                                    std::optional<VkSemaphoreSubmitInfo> signal_info) {
        std::lock_guard lock{graphics_queue_lock};

        VK_CHECK(vkWaitForFences(device, 1, &barriers_cmd_buf_fence, true, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &barriers_cmd_buf_fence));
        VK_CHECK(vkResetCommandBuffer(barriers_cmd_buf, 0));

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(barriers_cmd_buf, &begin_info));

        VkDependencyInfo dep_info{};
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.bufferMemoryBarrierCount = bufs.size();
        dep_info.pBufferMemoryBarriers = bufs.data();
        dep_info.imageMemoryBarrierCount = images.size();
        dep_info.pImageMemoryBarriers = images.data();
        dep_info.memoryBarrierCount = general.size();
        dep_info.pMemoryBarriers = general.data();
        vkCmdPipelineBarrier2(barriers_cmd_buf, &dep_info);

        VK_CHECK(vkEndCommandBuffer(barriers_cmd_buf));

        VkCommandBufferSubmitInfo cmd_info{};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmd_info.commandBuffer = barriers_cmd_buf;

        VkSubmitInfo2 submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit_info.waitSemaphoreInfoCount = wait_info ? 1 : 0;
        submit_info.pWaitSemaphoreInfos = wait_info ? &*wait_info : nullptr;
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &cmd_info;
        submit_info.signalSemaphoreInfoCount = signal_info ? 1 : 0;
        submit_info.pSignalSemaphoreInfos = signal_info ? &*signal_info : nullptr;
        VK_CHECK(vkQueueSubmit2(graphics_queue, 1, &submit_info, barriers_cmd_buf_fence));
    }
}
