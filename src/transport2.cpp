#include "goliath/transport2.hpp"
#include "goliath/buffer.hpp"
#include "goliath/engine.hpp"
#include "goliath/synchronization.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <emmintrin.h>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace engine::transport2 {
    std::vector<VkBufferMemoryBarrier2> transport_queue_buffer_barriers{};
    std::vector<VkImageMemoryBarrier2> transport_queue_image_barriers{};
    std::vector<std::variant<VkBufferMemoryBarrier2, VkImageMemoryBarrier2>> graphics_queue_barriers{};

    struct task {
        struct BufferDst {
            VkBuffer buffer;
            uint32_t offset;
            uint32_t initial_offset;
        };

        struct ImageDst {
            VkImage image;
            VkImageSubresourceLayers subresource;
            uint32_t initial_base_array_layer;

            VkOffset3D offset;
            VkExtent3D extent;

            uint32_t row_length;
            uint32_t image_height;

            VkImageLayout new_layout;
        };

        std::variant<BufferDst, ImageDst> dst;
        void* src;
        uint32_t src_offset;
        uint32_t src_size;
        uint32_t ticket_id;
        bool owning;
        bool last;

        uint32_t required_size() const {
            return src_size - src_offset;
        }

        void split(uint32_t budget, task& rest) {

        }

        void upload(VkCommandBuffer cmd_buf, VkBuffer src_buf, uint32_t src_buf_offset) {
            std::visit(
                [&](auto&& dst) {
                    using Dst = std::decay_t<decltype(dst)>;
                    if constexpr (std::same_as<BufferDst, Dst>) {
                        VkBufferCopy region{
                            .srcOffset = src_buf_offset,
                            .dstOffset = dst.offset,
                            .size = src_size,
                        };

                        vkCmdCopyBuffer(cmd_buf, src_buf, dst.buffer, 1, &region);
                        if (!last) return;

                        transport_queue_buffer_barriers.emplace_back(VkBufferMemoryBarrier2{
                            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                            .pNext = nullptr,
                            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                            .dstAccessMask = VK_ACCESS_2_NONE,
                            .srcQueueFamilyIndex = transport_queue_family,
                            .dstQueueFamilyIndex = graphics_queue_family,
                            .buffer = dst.buffer,
                            .offset = dst.initial_offset,
                            .size = src_offset + src_size,
                        });

                        graphics_queue_barriers.emplace_back(VkBufferMemoryBarrier2{
                            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                            .pNext = nullptr,
                            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                            .dstAccessMask = VK_ACCESS_2_NONE,
                            .srcQueueFamilyIndex = transport_queue_family,
                            .dstQueueFamilyIndex = graphics_queue_family,
                            .buffer = dst.buffer,
                            .offset = dst.initial_offset,
                            .size = src_offset + src_size,
                        });
                    } else {
                        VkBufferImageCopy region{
                            .bufferOffset = src_buf_offset,
                            .bufferRowLength = dst.row_length,
                            .bufferImageHeight = dst.image_height,
                            .imageSubresource = dst.subresource,
                            .imageOffset = dst.offset,
                            .imageExtent = dst.extent,
                        };

                        vkCmdCopyBufferToImage(cmd_buf, src_buf, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                               &region);
                        if (!last) return;

                        transport_queue_image_barriers.emplace_back(VkImageMemoryBarrier2{
                            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                            .pNext = nullptr,
                            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                            .dstAccessMask = VK_ACCESS_2_NONE,
                            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            .newLayout = dst.new_layout,
                            .srcQueueFamilyIndex = transport_queue_family,
                            .dstQueueFamilyIndex = graphics_queue_family,
                            .image = dst.image,
                            .subresourceRange =
                                VkImageSubresourceRange{
                                    .aspectMask = dst.subresource.aspectMask,
                                    .baseMipLevel = dst.subresource.mipLevel,
                                    .levelCount = dst.subresource.baseArrayLayer - dst.initial_base_array_layer + 1,
                                    .baseArrayLayer = dst.initial_base_array_layer,
                                    .layerCount = dst.subresource.layerCount,
                                },
                        });

                        graphics_queue_barriers.emplace_back(VkImageMemoryBarrier2{
                            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                            .pNext = nullptr,
                            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                            .dstAccessMask = VK_ACCESS_2_NONE,
                            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            .newLayout = dst.new_layout,
                            .srcQueueFamilyIndex = transport_queue_family,
                            .dstQueueFamilyIndex = graphics_queue_family,
                            .image = dst.image,
                            .subresourceRange =
                                VkImageSubresourceRange{
                                    .aspectMask = dst.subresource.aspectMask,
                                    .baseMipLevel = dst.subresource.mipLevel,
                                    .levelCount = dst.subresource.baseArrayLayer - dst.initial_base_array_layer + 1,
                                    .baseArrayLayer = dst.initial_base_array_layer,
                                    .layerCount = dst.subresource.layerCount,
                                },
                        });
                    }
                },
                dst);
        }
    };

    bool stop_worker = false;
    std::thread worker;

    std::vector<std::pair<uint64_t, uint64_t>> ticket_timelines{};
    std::vector<ticket> free_tickets{};

    uint64_t timeline_counter = 0;
    uint64_t finished_timeline = 0;
    VkSemaphore timeline_semaphore;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buf;

    static constexpr uint32_t staging_buffer_size = 8000000;
    Buffer staging_buffer{};
    void* staging_buffer_ptr{};
    uint64_t staging_buffer_finish_timeline = 0;
    bool flush_staging_buffer = false;

    uint32_t current_task_queue = 0;
    std::array<std::deque<task>, 2> task_queues{};
    std::mutex task_queue_lock{};

    bool is_timeline_ready(uint64_t timeline) {
        VkSemaphoreWaitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        info.semaphoreCount = 1;
        info.pSemaphores = &timeline_semaphore;
        info.pValues = &timeline;

        auto res = vkWaitSemaphores(device, &info, 0);
        if (res == VK_SUCCESS) {
            finished_timeline = timeline;
            return true;
        } else {
            return false;
        }
    }

    void thread() {
        while (!stop_worker) {
            if (!is_timeline_ready(staging_buffer_finish_timeline)) {
                _mm_pause();
                continue;
            }

            task_queue_lock.lock();
            auto& task_queue = task_queues[current_task_queue];
            current_task_queue = (current_task_queue + 1) % task_queues.size();
            task_queue_lock.unlock();

            if (task_queue.empty() && transport_queue_buffer_barriers.empty() &&
                transport_queue_image_barriers.empty()) {
                _mm_pause();
                continue;
            }

            VK_CHECK(vkResetCommandBuffer(cmd_buf, 0));

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

            VkDependencyInfo dep_info{};
            dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_info.bufferMemoryBarrierCount = transport_queue_buffer_barriers.size();
            dep_info.pBufferMemoryBarriers = transport_queue_buffer_barriers.data();
            dep_info.imageMemoryBarrierCount = transport_queue_image_barriers.size();
            dep_info.pImageMemoryBarriers = transport_queue_image_barriers.data();
            vkCmdPipelineBarrier2(cmd_buf, &dep_info);
            transport_queue_buffer_barriers.clear();
            transport_queue_image_barriers.clear();

            std::vector<task> tasks;
            uint32_t size = 0;
            // TODO: splitting
            while (!task_queue.empty()) {
                auto& task = task_queue.front();

                // && (size + task_queue.front().required_size()) <= staging_buffer_size
                auto budget = staging_buffer_size - size;
                if (budget == 0) break;
                if (budget < task.required_size()) {
                    task_queue.emplace_front();
                    std::swap(task_queue[0], task_queue[1]);

                    task_queue[0].split(budget, task_queue[1]);
                    task = task_queue[0];
                }

                std::memcpy((uint8_t*)staging_buffer_ptr + size, (uint8_t*)task.src + task.src_offset, task.src_size);
                if (task.owning) free(task.src);

                size += task.required_size();
                tasks.emplace_back(task);
                task_queue.pop_front();
            }

            if (flush_staging_buffer) staging_buffer.flush_mapped(0, staging_buffer_size);

            uint32_t upload_offset = 0;
            for (auto& task : tasks) {
                task.upload(cmd_buf, staging_buffer, upload_offset);
                upload_offset += task.required_size();
            }

            VK_CHECK(vkEndCommandBuffer(cmd_buf));

            VkCommandBufferSubmitInfo cmd_info{};
            cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmd_info.commandBuffer = cmd_buf;

            VkSemaphoreSubmitInfo signal_info{};
            signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal_info.semaphore = timeline_semaphore;
            signal_info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            signal_info.value = ++timeline_counter;

            VkSubmitInfo2 submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount = 0;
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &cmd_info;
            submit_info.signalSemaphoreInfoCount = 1;
            submit_info.pSignalSemaphoreInfos = &signal_info;

            VK_CHECK(vkQueueSubmit2(transport_queue, 1, &submit_info, nullptr));

            for (auto& task : tasks) {
                ticket_timelines[task.ticket_id].second = timeline_counter;
            }
        }
    }

    void init() {
        staging_buffer = Buffer::create("Transport buffer", staging_buffer_size, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
                                        {{&staging_buffer_ptr, &flush_staging_buffer}});

        VkCommandPoolCreateInfo transport_pool_info{};
        transport_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        transport_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        transport_pool_info.queueFamilyIndex = transport_queue_family;
        vkCreateCommandPool(device, &transport_pool_info, nullptr, &cmd_pool);

        VkCommandBufferAllocateInfo cmd_buf_alloc_info{};
        cmd_buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buf_alloc_info.commandBufferCount = 1;
        cmd_buf_alloc_info.commandPool = cmd_pool;
        cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vkAllocateCommandBuffers(device, &cmd_buf_alloc_info, &cmd_buf);

        VkSemaphoreTypeCreateInfo semaphore_type{};
        semaphore_type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        semaphore_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        semaphore_type.initialValue = 0;

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = &semaphore_type;

        vkCreateSemaphore(device, &semaphore_info, nullptr, &timeline_semaphore);

        worker = std::thread{thread};
    }

    void destroy() {
        worker.join();

        staging_buffer.destroy();
        vkDestroyCommandPool(device, cmd_pool, nullptr);
        vkDestroySemaphore(device, timeline_semaphore, nullptr);
    }

    void tick() {
        synchronization::begin_barriers();
        for (auto barrier : graphics_queue_barriers) {
            std::visit([](auto barrier) { synchronization::apply_barrier(barrier); }, barrier);
        }
        synchronization::end_barriers();
    }

    bool is_ready(ticket ticket) {
        auto [gen, timeline] = ticket_timelines[ticket.id()];
        if (ticket.gen() < gen) return true;
        if (timeline == 0) return false;
        if (finished_timeline >= timeline) return true;

        VkSemaphoreWaitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        info.semaphoreCount = 1;
        info.pSemaphores = &timeline_semaphore;
        info.pValues = &timeline;

        return is_timeline_ready(timeline);
    }

    ticket upload(void* src, bool own, uint32_t size, VkBuffer dst, uint32_t dst_offset) {}

    // std::vector<VkBufferMemoryBarrier2> transport_queue_buffer_barriers{};
    // std::vector<VkImageMemoryBarrier2> transport_queue_image_barriers{};
    //
    // std::vector<std::variant<VkBufferMemoryBarrier2, VkImageMemoryBarrier2>> graphics_queue_barriers{};
    //
    // struct UploadTask {
    //     struct BufferDst {
    //         VkBuffer buffer;
    //         uint32_t offset;
    //     };
    //
    //     struct ImageDst {
    //         VkImage image;
    //         VkImageSubresourceLayers subresource;
    //         uint32_t initial_base_array_layer;
    //
    //         VkOffset3D offset;
    //         VkExtent3D extent;
    //
    //         uint32_t row_length;
    //         uint32_t image_height;
    //
    //         VkImageLayout new_layout;
    //     };
    //
    //     void* src;
    //     uint32_t src_offset;
    //     uint32_t src_size;
    //     bool owning;
    //     bool last;
    //
    //     std::variant<BufferDst, ImageDst> dst;
    //
    //     uint32_t required_size() const {
    //         return src_size - src_offset;
    //     }
    //
    //     void upload(VkCommandBuffer cmd_buf, VkBuffer src_buf, uint32_t src_buf_offset) {
    //         std::visit(
    //             [&](auto&& dst) {
    //                 using Dst = std::decay_t<decltype(dst)>;
    //                 if constexpr (std::same_as<BufferDst, Dst>) {
    //                     VkBufferCopy region{
    //                         .srcOffset = src_buf_offset,
    //                         .dstOffset = dst.offset,
    //                         .size = src_size,
    //                     };
    //
    //                     vkCmdCopyBuffer(cmd_buf, src_buf, dst.buffer, 1, &region);
    //                     if (!last) return;
    //
    //                     transport_queue_buffer_barriers.emplace_back(VkBufferMemoryBarrier2{
    //                         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    //                         .pNext = nullptr,
    //                         .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //                         .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //                         .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
    //                         .dstAccessMask = VK_ACCESS_2_NONE,
    //                         .srcQueueFamilyIndex = transport_queue_family,
    //                         .dstQueueFamilyIndex = graphics_queue_family,
    //                         .buffer = dst.buffer,
    //                         .offset = dst.offset,
    //                         .size = src_offset + src_size,
    //                     });
    //
    //                     graphics_queue_barriers.emplace_back(VkBufferMemoryBarrier2{
    //                         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    //                         .pNext = nullptr,
    //                         .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //                         .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //                         .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
    //                         .dstAccessMask = VK_ACCESS_2_NONE,
    //                         .srcQueueFamilyIndex = transport_queue_family,
    //                         .dstQueueFamilyIndex = graphics_queue_family,
    //                         .buffer = dst.buffer,
    //                         .offset = dst.offset,
    //                         .size = src_offset + src_size,
    //                     });
    //                 } else {
    //                     VkBufferImageCopy region{
    //                         .bufferOffset = src_buf_offset,
    //                         .bufferRowLength = dst.row_length,
    //                         .bufferImageHeight = dst.image_height,
    //                         .imageSubresource = dst.subresource,
    //                         .imageOffset = dst.offset,
    //                         .imageExtent = dst.extent,
    //                     };
    //
    //                     vkCmdCopyBufferToImage(cmd_buf, src_buf, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
    //                                            &region);
    //                     if (!last) return;
    //
    //                     transport_queue_image_barriers.emplace_back(VkImageMemoryBarrier2{
    //                         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    //                         .pNext = nullptr,
    //                         .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //                         .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //                         .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
    //                         .dstAccessMask = VK_ACCESS_2_NONE,
    //                         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //                         .newLayout = dst.new_layout,
    //                         .srcQueueFamilyIndex = transport_queue_family,
    //                         .dstQueueFamilyIndex = graphics_queue_family,
    //                         .image = dst.image,
    //                         .subresourceRange =
    //                             VkImageSubresourceRange{
    //                                 .aspectMask = dst.subresource.aspectMask,
    //                                 .baseMipLevel = dst.subresource.mipLevel,
    //                                 .levelCount = dst.subresource.baseArrayLayer - dst.initial_base_array_layer + 1,
    //                                 .baseArrayLayer = dst.initial_base_array_layer,
    //                                 .layerCount = dst.subresource.layerCount,
    //                             },
    //                     });
    //
    //                     graphics_queue_barriers.emplace_back(VkImageMemoryBarrier2{
    //                         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    //                         .pNext = nullptr,
    //                         .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //                         .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //                         .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
    //                         .dstAccessMask = VK_ACCESS_2_NONE,
    //                         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //                         .newLayout = dst.new_layout,
    //                         .srcQueueFamilyIndex = transport_queue_family,
    //                         .dstQueueFamilyIndex = graphics_queue_family,
    //                         .image = dst.image,
    //                         .subresourceRange =
    //                             VkImageSubresourceRange{
    //                                 .aspectMask = dst.subresource.aspectMask,
    //                                 .baseMipLevel = dst.subresource.mipLevel,
    //                                 .levelCount = dst.subresource.baseArrayLayer - dst.initial_base_array_layer + 1,
    //                                 .baseArrayLayer = dst.initial_base_array_layer,
    //                                 .layerCount = dst.subresource.layerCount,
    //                             },
    //                     });
    //                 }
    //             },
    //             dst);
    //     }
    // };
    //
    // VkCommandPool cmd_pool;
    // std::array<VkCommandBuffer, frames_in_flight> cmd_bufs;
    //
    // static constexpr uint32_t frame_budget = 8000000;
    // std::array<Buffer, frames_in_flight> staging_buffers{};
    // std::array<void*, frames_in_flight> staging_buffer_ptrs{};
    // std::array<uint64_t, frames_in_flight> staging_buffer_finish_timeline{};
    // bool flush_staging_buffers = false;
    //
    // uint32_t current_write_offset;
    //
    // uint64_t enqueue_timeline_counter = 0;
    // uint64_t present_timeline_counter = 0;
    // uint64_t finished_timeline = 0;
    // VkSemaphore timeline_semaphore;
    // std::queue<UploadTask> upload_queue;
    //
    // void init() {
    //     for (auto& finish_timeline : staging_buffer_finish_timeline) {
    //         finish_timeline = 0;
    //     }
    //
    //     for (size_t i = 0; i < frames_in_flight; i++) {
    //         staging_buffers[i] = Buffer::create("Transport buffer", frame_budget, VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
    //                                             {{&staging_buffer_ptrs[i], &flush_staging_buffers}});
    //     }
    //
    //     VkCommandPoolCreateInfo transport_pool_info{};
    //     transport_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    //     transport_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    //     transport_pool_info.queueFamilyIndex = transport_queue_family;
    //     vkCreateCommandPool(device, &transport_pool_info, nullptr, &cmd_pool);
    //
    //     VkCommandBufferAllocateInfo cmd_buf_alloc_info{};
    //     cmd_buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    //     cmd_buf_alloc_info.commandBufferCount = 1;
    //     cmd_buf_alloc_info.commandPool = cmd_pool;
    //     cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    //     for (std::size_t i = 0; i < cmd_bufs.size(); i++) {
    //         vkAllocateCommandBuffers(device, &cmd_buf_alloc_info, &cmd_bufs[i]);
    //     }
    //
    //     VkSemaphoreTypeCreateInfo semaphore_type{};
    //     semaphore_type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    //     semaphore_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    //     semaphore_type.initialValue = 0;
    //
    //     VkSemaphoreCreateInfo semaphore_info{};
    //     semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    //     semaphore_info.pNext = &semaphore_type;
    //
    //     vkCreateSemaphore(device, &semaphore_info, nullptr, &timeline_semaphore);
    // }
    //
    // void destroy() {
    //     for (size_t i = 0; i < frames_in_flight; i++) {
    //         staging_buffers[i].destroy();
    //     }
    //
    //     vkDestroyCommandPool(device, cmd_pool, nullptr);
    //     vkDestroySemaphore(device, timeline_semaphore, nullptr);
    // }
    //
    // void tick() {
    //     auto curr_frame = get_current_frame();
    //
    //     synchronization::begin_barriers();
    //     for (const auto& barrier : graphics_queue_barriers) {
    //         std::visit([](auto barrier) { synchronization::apply_barrier(barrier); }, barrier);
    //     }
    //     synchronization::end_barriers();
    //     graphics_queue_barriers.clear();
    //
    //     if (!is_ready(staging_buffer_finish_timeline[curr_frame])) {
    //         printf("NOTE: Transport didn't finish in time");
    //         return;
    //     }
    //
    //     auto* staging_dst = (uint8_t*)staging_buffer_ptrs[curr_frame];
    //     auto& cmd_buf = cmd_bufs[curr_frame];
    //     VK_CHECK(vkResetCommandBuffer(cmd_buf, 0));
    //
    //     VkCommandBufferBeginInfo begin_info{};
    //     begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    //     begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    //     VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));
    //
    //     VkDependencyInfo dep_info{};
    //     dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    //     dep_info.bufferMemoryBarrierCount = transport_queue_buffer_barriers.size();
    //     dep_info.pBufferMemoryBarriers = transport_queue_buffer_barriers.data();
    //     dep_info.imageMemoryBarrierCount = transport_queue_image_barriers.size();
    //     dep_info.pImageMemoryBarriers = transport_queue_image_barriers.data();
    //     vkCmdPipelineBarrier2(cmd_buf, &dep_info);
    //     transport_queue_buffer_barriers.clear();
    //     transport_queue_image_barriers.clear();
    //
    //     std::vector<UploadTask> tasks{};
    //     uint32_t staging_upload_offset = 0;
    //     while (!upload_queue.empty() &&
    //            (staging_upload_offset + upload_queue.front().required_size()) <= frame_budget) {
    //         auto& task = upload_queue.front();
    //
    //         std::memcpy(staging_dst + staging_upload_offset, (uint8_t*)task.src + task.src_offset, task.src_size);
    //         if (task.owning) free(task.src);
    //
    //         staging_upload_offset += task.required_size();
    //         tasks.emplace_back(task);
    //         upload_queue.pop();
    //     }
    //
    //     if (flush_staging_buffers) staging_buffers[curr_frame].flush_mapped(0, staging_upload_offset);
    //
    //     uint32_t upload_offset = 0;
    //     for (auto& task : tasks) {
    //         task.upload(cmd_buf, staging_buffers[curr_frame], upload_offset);
    //         upload_offset += task.required_size();
    //     }
    //
    //     VK_CHECK(vkEndCommandBuffer(cmd_buf));
    //
    //     VkCommandBufferSubmitInfo cmd_info{};
    //     cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    //     cmd_info.commandBuffer = cmd_buf;
    //
    //     VkSemaphoreSubmitInfo signal_info{};
    //     signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    //     signal_info.semaphore = timeline_semaphore;
    //     signal_info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    //     signal_info.value = ++present_timeline_counter;
    //
    //     VkSubmitInfo2 submit_info{};
    //     submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    //     submit_info.waitSemaphoreInfoCount = 0;
    //     submit_info.commandBufferInfoCount = 1;
    //     submit_info.pCommandBufferInfos = &cmd_info;
    //     submit_info.signalSemaphoreInfoCount = 1;
    //     submit_info.pSignalSemaphoreInfos = &signal_info;
    //
    //     VK_CHECK(vkQueueSubmit2(transport_queue, 1, &submit_info, nullptr));
    // }
    //
    // bool is_ready(uint64_t timeline) {
    //     if (finished_timeline >= timeline) return true;
    //
    //     VkSemaphoreWaitInfo info{};
    //     info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    //     info.semaphoreCount = 1;
    //     info.pSemaphores = &timeline_semaphore;
    //     info.pValues = &timeline;
    //
    //     auto res = vkWaitSemaphores(device, &info, 0);
    //     if (res == VK_SUCCESS) {
    //         finished_timeline = timeline;
    //         return true;
    //     } else {
    //         return false;
    //     }
    // }
    //
    // uint64_t upload(void* src, bool own, uint32_t size, VkBuffer dst, uint32_t dst_offset) {
    //     if (dst == nullptr) return 0;
    //
    //     uint32_t src_offset = 0;
    //
    //     if (current_write_offset + size > frame_budget) {
    //         auto size_ = std::min(frame_budget - current_write_offset, size);
    //         upload_queue.emplace(src, 0, size_, own, size == size_, UploadTask::BufferDst{dst, dst_offset});
    //         enqueue_timeline_counter++;
    //
    //         src_offset += size_;
    //         size -= size_;
    //         dst_offset += size_;
    //         current_write_offset = 0;
    //     }
    //
    //     while (size != 0) {
    //         auto size_ = std::min(frame_budget, size);
    //         upload_queue.emplace(src, src_offset, size_, own, size <= frame_budget,
    //                              UploadTask::BufferDst{dst, dst_offset});
    //         enqueue_timeline_counter++;
    //
    //         src_offset += size_;
    //         size -= size_;
    //         dst_offset += size_;
    //         current_write_offset = size_ % frame_budget;
    //     }
    //
    //     return enqueue_timeline_counter;
    // }
    //
    // uint64_t upload(void* src, bool own, VkImage dst, VkImageLayout old_layout, VkImageLayout new_layout,
    //                 VkFormat dst_format, VkExtent3D dst_dimension, VkOffset3D dst_offset,
    //                 VkImageSubresourceLayers dst_layers) {
    //     if (dst == nullptr) return 0;
    //
    //     transport_queue_image_barriers.emplace_back(VkImageMemoryBarrier2{
    //         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    //         .pNext = nullptr,
    //         .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
    //         .srcAccessMask = VK_ACCESS_2_NONE,
    //         .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //         .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //         .image = dst,
    //         .subresourceRange =
    //             VkImageSubresourceRange{
    //                 .aspectMask = dst_layers.aspectMask,
    //                 .baseMipLevel = dst_layers.mipLevel,
    //                 .levelCount = 1,
    //                 .baseArrayLayer = dst_layers.baseArrayLayer,
    //                 .layerCount = dst_layers.layerCount,
    //             },
    //     });
    //
    //     // Compute bytes per texel for common formats
    //     uint32_t bytes_per_pixel = 4; // default to RGBA8; replace with proper lookup for other formats
    //
    //     // Helper to compute a chunk size in texels that fits within frame_budget
    //     auto compute_tile_extent = [&](uint32_t width, uint32_t height) {
    //         uint32_t max_texels = (frame_budget - current_write_offset) / bytes_per_pixel;
    //         // pick square-ish tile
    //         uint32_t tile_width = std::min(width, uint32_t(std::sqrt(max_texels)));
    //         uint32_t tile_height = std::min(height, max_texels / tile_width);
    //         return VkExtent3D{tile_width, tile_height, 1};
    //     };
    //
    //     for (uint32_t layer = 0; layer < dst_layers.layerCount; ++layer) {
    //         uint32_t y = 0;
    //         while (y < dst_dimension.height) {
    //             uint32_t remaining_height = dst_dimension.height - y;
    //
    //             for (uint32_t x = 0; x < dst_dimension.width;) {
    //                 uint32_t remaining_width = dst_dimension.width - x;
    //
    //                 VkExtent3D tile_extent = compute_tile_extent(remaining_width, remaining_height);
    //                 uint32_t tile_bytes = tile_extent.width * tile_extent.height * bytes_per_pixel;
    //
    //                 UploadTask::ImageDst image_dst{};
    //                 image_dst.image = dst;
    //                 image_dst.subresource = {
    //                     dst_layers.aspectMask,
    //                     dst_layers.mipLevel,
    //                     dst_layers.baseArrayLayer + layer,
    //                     1,
    //                 };
    //                 image_dst.initial_base_array_layer = dst_layers.baseArrayLayer;
    //                 image_dst.offset = {
    //                     .x = dst_offset.x + int32_t(x),
    //                     .y = dst_offset.y + int32_t(y),
    //                     .z = dst_offset.z,
    //                 };
    //                 image_dst.extent = {
    //                     .width = tile_extent.width,
    //                     .height = tile_extent.height,
    //                     .depth = dst_dimension.depth,
    //                 };
    //                 image_dst.row_length = tile_extent.width;
    //                 image_dst.image_height = tile_extent.height;
    //                 image_dst.new_layout = new_layout;
    //
    //                 // enqueue upload task
    //                 bool last = y + tile_extent.height == dst_dimension.height &&
    //                             x + tile_extent.width == dst_dimension.width && layer + 1 == dst_layers.layerCount;
    //                 upload_queue.emplace(src, (y * dst_dimension.width + x) * bytes_per_pixel, tile_bytes, own, last,
    //                                      image_dst);
    //                 enqueue_timeline_counter++;
    //
    //                 x += tile_extent.width;
    //                 current_write_offset = (current_write_offset + tile_bytes) % frame_budget;
    //             }
    //
    //             y += compute_tile_extent(dst_dimension.width, remaining_height).height;
    //         }
    //     }
    //
    //     return enqueue_timeline_counter;
    // }
}
