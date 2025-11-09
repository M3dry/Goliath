#include "goliath/transport.hpp"
#include "transport_.hpp"

#include "goliath/engine.hpp"

#include <cassert>
#include <cstring>
#include <vulkan/vulkan_core.h>

#include <chrono>
#include <iostream>

struct FormatInfo {
    uint32_t blockWidth;
    uint32_t blockHeight;
    uint32_t bytesPerBlock;
};

FormatInfo get_format_info(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM: return {1, 1, 1};
        case VK_FORMAT_R8G8_UNORM: return {1, 1, 2};
        case VK_FORMAT_R8G8B8_UNORM: return {1, 1, 3};
        case VK_FORMAT_R8G8B8A8_UNORM: return {1, 1, 4};
        case VK_FORMAT_R16_UNORM: return {1, 1, 2};
        case VK_FORMAT_R16G16_UNORM: return {1, 1, 4};
        case VK_FORMAT_R16G16B16_UNORM: return {1, 1, 6};
        case VK_FORMAT_R16G16B16A16_UNORM: return {1, 1, 8};
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return {4, 4, 8};
        case VK_FORMAT_R32G32B32_SFLOAT: return {1, 1, 12};
        default: fprintf(stderr, "invalid format: %d", format); assert(false && "Unsupported format");
    }
}

namespace engine::transport {
    inline constexpr std::size_t BUFFER_SIZE = 256000000;

    VkDeviceSize alignment = 1;

    VkCommandBufferAllocateInfo cmd_buf_alloc_info{};
    VkCommandPool pool;
    std::array<VkCommandBuffer, 10> cmd_bufs;
    uint32_t current_cmd_buf = 0;

    bool coherent = false;
    uint8_t* ring_buffer_data;
    VkBuffer ring_buffer;
    VmaAllocation ring_buffer_allocation;

    uint64_t cached_signal = 0;
    uint64_t current_signal = 0;
    VkSemaphore timeline;

    uint64_t ring_buffer_start_index = 0;
    bool transport_block = false;

    inline VkDeviceSize align(VkDeviceSize addr) {
        return (addr + (alignment - 1)) / alignment * alignment;
    }

    void init() {
        alignment = physical_device_properties.limits.optimalBufferCopyOffsetAlignment;

        VkCommandPoolCreateInfo transport_pool_info{};
        transport_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        transport_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        transport_pool_info.queueFamilyIndex = transport_queue_family;
        vkCreateCommandPool(device, &transport_pool_info, nullptr, &pool);

        cmd_buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buf_alloc_info.commandBufferCount = 1;
        cmd_buf_alloc_info.commandPool = pool;
        cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        for (std::size_t i = 0; i < cmd_bufs.size(); i++) {
            vkAllocateCommandBuffers(device, &cmd_buf_alloc_info, &cmd_bufs[i]);
        }

        VkBufferCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        create_info.size = BUFFER_SIZE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo out_alloc_info;
        vmaCreateBuffer(allocator, &create_info, &alloc_info, &ring_buffer, &ring_buffer_allocation, &out_alloc_info);

        ring_buffer_data = (uint8_t*)out_alloc_info.pMappedData;

        VkMemoryPropertyFlags props;
        vmaGetMemoryTypeProperties(allocator, out_alloc_info.memoryType, &props);
        coherent = props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VkSemaphoreTypeCreateInfo semaphore_type{};
        semaphore_type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        semaphore_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        semaphore_type.initialValue = 0;

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = &semaphore_type;

        vkCreateSemaphore(device, &semaphore_info, nullptr, &timeline);
    }

    void destroy() {
        vmaDestroyBuffer(allocator, ring_buffer, ring_buffer_allocation);
        vkDestroySemaphore(device, timeline, nullptr);
        vkDestroyCommandPool(device, pool, nullptr);
    }

    uint64_t begin() {
        transport_block = true;

        vkResetCommandBuffer(cmd_bufs[current_cmd_buf], 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd_bufs[current_cmd_buf], &begin_info));

        return ++current_signal;
    }

    uint64_t end() {
        transport_block = false;

        VK_CHECK(vkEndCommandBuffer(cmd_bufs[current_cmd_buf]));

        VkCommandBufferSubmitInfo cmd_info{};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmd_info.commandBuffer = cmd_bufs[current_cmd_buf];

        VkSemaphoreSubmitInfo signal_info{};
        signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_info.semaphore = timeline;
        signal_info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        signal_info.value = current_signal;

        VkSubmitInfo2 submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit_info.waitSemaphoreInfoCount = 0;
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &cmd_info;
        submit_info.signalSemaphoreInfoCount = 1;
        submit_info.pSignalSemaphoreInfos = &signal_info;

        VK_CHECK(vkQueueSubmit2(transport_queue, 1, &submit_info, nullptr));

        current_cmd_buf = (current_cmd_buf + 1) % cmd_bufs.size();
        return current_signal;
    }

    void upload(VkBufferMemoryBarrier2* barrier, void* src, uint32_t size, VkBuffer dst, uint32_t dst_start_offset) {
        assert(transport_block);

        ring_buffer_start_index = align(ring_buffer_start_index);

        std::memcpy(ring_buffer_data + ring_buffer_start_index, src, size);
        if (!coherent) {
            vmaFlushAllocation(allocator, ring_buffer_allocation, ring_buffer_start_index, size);
        }

        VkBufferCopy region{};
        region.srcOffset = ring_buffer_start_index;
        region.size = size;
        region.dstOffset = dst_start_offset;
        vkCmdCopyBuffer(cmd_bufs[current_cmd_buf], ring_buffer, dst, 1, &region);

        auto dstStage = barrier->dstStageMask;
        auto dstAccess = barrier->dstAccessMask;
        barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier->pNext = nullptr;
        barrier->srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier->srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier->dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier->dstAccessMask = 0;
        barrier->buffer = dst;
        barrier->offset = dst_start_offset;
        barrier->size = size;
        barrier->srcQueueFamilyIndex = transport_queue_family;

        VkDependencyInfo dep_info{};
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.bufferMemoryBarrierCount = 1;
        dep_info.pBufferMemoryBarriers = barrier;
        vkCmdPipelineBarrier2(cmd_bufs[current_cmd_buf], &dep_info);

        uint32_t dst_queue_family = barrier->dstQueueFamilyIndex;
        barrier->srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier->srcAccessMask = 0;
        barrier->dstStageMask = dstStage;
        barrier->dstAccessMask = dstAccess;
        barrier->dstQueueFamilyIndex = barrier->srcQueueFamilyIndex;
        barrier->srcQueueFamilyIndex = dst_queue_family;

        ring_buffer_start_index = (ring_buffer_start_index + size) % BUFFER_SIZE;
    }

    void upload(VkImageMemoryBarrier2* barrier, void* src, uint32_t size, uint32_t width, uint32_t height,
                VkFormat format, VkImage dst) {
        assert(transport_block);
        ring_buffer_start_index = align(ring_buffer_start_index);

        std::memcpy(ring_buffer_data + ring_buffer_start_index, src, size);
        if (!coherent) {
            vmaFlushAllocation(allocator, ring_buffer_allocation, ring_buffer_start_index, size);
        }

        FormatInfo fmt_info = get_format_info(format);

        VkBufferImageCopy region{};
        region.bufferOffset = ring_buffer_start_index;
        if (fmt_info.blockWidth == 1 && fmt_info.blockHeight == 1) {
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
        } else {
            region.bufferRowLength = (width + fmt_info.blockWidth - 1) / fmt_info.blockWidth;
            region.bufferImageHeight = (height + fmt_info.blockHeight - 1) / fmt_info.blockHeight;
        }
        region.imageExtent = VkExtent3D{
            .width = width,
            .height = height,
            .depth = 1,
        };
        region.imageOffset = VkOffset3D{
            .x = 0,
            .y = 0,
            .z = 0,
        };
        region.imageSubresource = VkImageSubresourceLayers{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        if (barrier->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
            VkImageMemoryBarrier2 pre_barrier{};
            pre_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            pre_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            pre_barrier.srcAccessMask = 0;
            pre_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            pre_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            pre_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            pre_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            pre_barrier.image = dst;
            pre_barrier.subresourceRange = VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            };

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &pre_barrier;
            vkCmdPipelineBarrier2(cmd_bufs[current_cmd_buf], &dep);

            barrier->oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        vkCmdCopyBufferToImage(cmd_bufs[current_cmd_buf], ring_buffer, dst, barrier->oldLayout, 1, &region);

        auto dstStage = barrier->dstStageMask;
        auto dstAccess = barrier->dstAccessMask;
        auto newLayout = barrier->newLayout;
        barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier->pNext = nullptr;
        barrier->srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier->srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier->dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier->dstAccessMask = 0;
        barrier->image = dst;
        barrier->subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        };
        barrier->newLayout = barrier->oldLayout;
        barrier->srcQueueFamilyIndex = transport_queue_family;

        VkImageMemoryBarrier2 barrier_cpy = *barrier;
        VkDependencyInfo dep_info{};
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.imageMemoryBarrierCount = 1;
        dep_info.pImageMemoryBarriers = &barrier_cpy;
        vkCmdPipelineBarrier2(cmd_bufs[current_cmd_buf], &dep_info);

        uint32_t dst_queue_family = barrier->dstQueueFamilyIndex;
        barrier->srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier->srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier->dstStageMask = dstStage;
        barrier->dstAccessMask = dstAccess;
        barrier->dstQueueFamilyIndex = barrier->srcQueueFamilyIndex;
        barrier->srcQueueFamilyIndex = dst_queue_family;
        barrier->newLayout = newLayout;

        ring_buffer_start_index = (ring_buffer_start_index + size) % BUFFER_SIZE;
    }

    uint64_t timeline_value(bool log) {
        if (!log) {
            uint64_t value;
            vkGetSemaphoreCounterValue(device, timeline, &value);
            return value;
        } else {
            auto t0 = std::chrono::high_resolution_clock::now();
            uint64_t value;
            vkGetSemaphoreCounterValue(device, timeline, &value);
            auto t1 = std::chrono::high_resolution_clock::now();
            std::cout << "vkGetSemaphoreCounterValue took "
                      << std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() << "us\n";
            return value;
        }
    }

    bool is_ready(uint64_t timeline_value) {
        if (cached_signal >= timeline_value) return true;

        VkSemaphoreWaitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        info.semaphoreCount = 1;
        info.pSemaphores = &timeline;
        info.pValues = &timeline_value;

        auto res = vkWaitSemaphores(device, &info, 0);
        if (res == VK_SUCCESS) {
            cached_signal = timeline_value;
            return true;
        } else {
            return false;
        }
    }

    void transition(VkImageMemoryBarrier2* barrier, VkImageAspectFlags aspect_mask) {
        barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier->pNext = nullptr;
        barrier->srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier->srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;

        barrier->subresourceRange = VkImageSubresourceRange{
            .aspectMask = aspect_mask,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        };
    }
}
