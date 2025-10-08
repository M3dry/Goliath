#include "goliath/synchronization.hpp"
#include "goliath/engine.hpp"
#include <vulkan/vulkan_core.h>

namespace engine::synchronization {
    std::vector<VkImageMemoryBarrier2> image_barriers{};
    std::vector<VkBufferMemoryBarrier2> buffer_barriers{};
    std::vector<VkMemoryBarrier2> memory_barriers{};

    void begin_barriers() {
        image_barriers.clear();
        buffer_barriers.clear();
        memory_barriers.clear();
    }

    void end_barriers() {
        VkDependencyInfo dep_info;
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.pNext = nullptr;
        dep_info.imageMemoryBarrierCount = (uint32_t)image_barriers.size();
        dep_info.pImageMemoryBarriers = image_barriers.data();
        dep_info.bufferMemoryBarrierCount = (uint32_t)buffer_barriers.size();
        dep_info.pBufferMemoryBarriers = buffer_barriers.data();
        dep_info.memoryBarrierCount = (uint32_t)memory_barriers.size();
        dep_info.pMemoryBarriers = memory_barriers.data();

        vkCmdPipelineBarrier2(get_cmd_buf(), &dep_info);
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
}
