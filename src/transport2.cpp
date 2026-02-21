#include "goliath/transport2.hpp"
#include "engine_.hpp"
#include "goliath/buffer.hpp"
#include "goliath/engine.hpp"
#include "goliath/synchronization.hpp"
#include "transport2_.hpp"

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
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: return {1, 1, 4};
        case VK_FORMAT_R16_UNORM: return {1, 1, 2};
        case VK_FORMAT_R16G16_UNORM: return {1, 1, 4};
        case VK_FORMAT_R16G16B16_UNORM: return {1, 1, 6};
        case VK_FORMAT_R16G16B16A16_UNORM: return {1, 1, 8};
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return {4, 4, 8};
        case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_R32G32B32_UINT: return {1, 1, 12};
        default: fprintf(stderr, "invalid format: %d", format); assert(false && "Unsupported format");
    }
}

namespace engine::transport2 {
    State* state;

    bool is_timeline_ready(uint64_t timeline);

    void release_ticket(uint32_t id) {
        state->ticket_timelines[id].first++;
        state->ticket_timelines[id].second = 0;

        state->free_tickets.emplace_back(state->ticket_timelines[id].first, id);
    }

    ticket get_free_ticket() {
        std::lock_guard lock{state->ticket_mutex};

        for (uint32_t i = 0; i < state->ticket_timelines.size(); i++) {
            if (state->ticket_timelines[i].second == 0 || !is_timeline_ready(state->ticket_timelines[i].second)) continue;

            release_ticket(i);
        }

        if (!state->free_tickets.empty()) {
            auto ticket = state->free_tickets.back();
            state->free_tickets.pop_back();
            return ticket;
        }

        state->ticket_timelines.emplace_back(0, 0);
        return ticket{0, (uint32_t)state->ticket_timelines.size() - 1};
    }

    size_t task::required_size() const {
        return std::visit(
            [&](auto&& dst) {
                using Dst = std::decay_t<decltype(dst)>;
                if constexpr (std::same_as<BufferDst, Dst>) {
                    return dst.src_size;
                } else {
                    auto info = get_format_info(dst.format);
                    assert(info.blockWidth == 1 && info.blockHeight == 1 && "Compressed formats not supported right now");

                    const uint32_t texel_size = info.bytesPerBlock;
                    return dst.extent.width * dst.extent.height * texel_size;
                }
            },
            dst);
    }

    void task::upload(uint8_t* out) {
        std::visit(
            [&](auto&& dst) {
                using Dst = std::decay_t<decltype(dst)>;
                if constexpr (std::same_as<BufferDst, Dst>) {
                    std::memcpy(out, (uint8_t*)src + src_offset, dst.src_size);
                } else {
                    uint8_t* real_src = (uint8_t*)src + src_offset;

                    auto info = get_format_info(dst.format);
                    assert(info.blockWidth == 1 && info.blockHeight == 1 && "Compressed formats not supported right now");

                    const uint32_t texel_size = info.bytesPerBlock;

                    const auto packed_row_length = dst.extent.width * texel_size;

                    for (size_t i = 0; i < dst.extent.height; i++) {
                        std::memcpy(out + i * packed_row_length, real_src + i * (dst.src_row_length * texel_size),
                                    packed_row_length);
                    }
                }
            },
            dst);
        if (owning) ((FreeFn*)(*owning))(src);
    }

    bool task::split(uint32_t budget, std::vector<task>& rest) {
        assert(budget < required_size());

        return std::visit(
            [&](auto& dst) {
                using Dst = std::decay_t<decltype(dst)>;
                if constexpr (std::same_as<BufferDst, Dst>) {
                    rest.emplace_back();
                    rest[0].src = src;
                    rest[0].ticket_id = ticket_id;
                    rest[0].owning = owning;
                    rest[0].last = last;
                    rest[0].src_offset = budget;
                    rest[0].dst_stage = dst_stage;
                    rest[0].dst_access = dst_access;

                    rest[0].dst = BufferDst{
                        .src_size = dst.src_size - budget,
                        .buffer = dst.buffer,
                        .offset = dst.offset + budget,
                        .initial_offset = dst.initial_offset,
                    };

                    ticket_id = get_free_ticket().id();
                    owning = std::nullopt;
                    last = false;
                    dst.src_size = budget;

                    return false;
                } else {
                    auto info = get_format_info(dst.format);
                    assert(info.blockWidth == 1 && info.blockHeight == 1 && "Compressed formats not supported right now");

                    const uint32_t texel_size = info.bytesPerBlock;
                    if (budget < texel_size) return true;

                    rest.emplace_back();
                    rest.emplace_back();

                    rest[0].dst = dst;
                    rest[0].src = src;
                    rest[0].ticket_id = get_free_ticket().id();
                    rest[0].owning = std::nullopt;
                    rest[0].last = false;
                    rest[0].dst_stage = dst_stage;
                    rest[0].dst_access = dst_access;

                    rest[1].dst = dst;
                    rest[1].src = src;
                    rest[1].ticket_id = ticket_id;
                    rest[1].owning = owning;
                    rest[1].last = last;
                    rest[1].dst_stage = dst_stage;
                    rest[1].dst_access = dst_access;

                    ticket_id = get_free_ticket().id();
                    owning = std::nullopt;
                    last = false;

                    uint32_t max_texels = budget / texel_size;
                    uint32_t w = std::min<uint32_t>(dst.extent.width, std::sqrt(max_texels));
                    uint32_t h = std::min<uint32_t>(dst.extent.height, max_texels / w);

                    uint32_t w1 = dst.extent.width - w;
                    uint32_t h1 = h;

                    uint32_t w2 = w + w1;
                    uint32_t h2 = dst.extent.height - h;

                    rest[0].src_offset = (h1 * dst.extent.width + w1) * texel_size;
                    std::get<ImageDst>(rest[0].dst).offset = VkOffset3D{
                        .x = dst.offset.x + (int32_t)w,
                        .y = dst.offset.y,
                        .z = dst.offset.z,
                    };
                    std::get<ImageDst>(rest[0].dst).extent = VkExtent3D{
                        .width = w1,
                        .height = h1,
                        .depth = 1,
                    };

                    std::get<ImageDst>(rest[1].dst).offset = VkOffset3D{
                        .x = dst.offset.x,
                        .y = dst.offset.y + (int32_t)h,
                        .z = dst.offset.z,
                    };
                    rest[1].src_offset = (h1 * dst.extent.width + w1) * texel_size;
                    std::get<ImageDst>(rest[1].dst).extent = VkExtent3D{
                        .width = w2,
                        .height = h2,
                        .depth = 1,
                    };

                    dst.extent.width = w;
                    dst.extent.height = h;

                    assert(!((h1 == 0 || w1 == 0) && (h2 == 0 || w2 == 0)));
                    if (h1 == 0 || w1 == 0) {
                        std::lock_guard lock{state->ticket_mutex};

                        release_ticket(rest[0].ticket_id);
                        rest.erase(rest.begin());
                    } else if (h2 == 0 || w2 == 0) {
                        std::lock_guard lock{state->ticket_mutex};

                        release_ticket(rest[0].ticket_id);
                        rest[0].ticket_id = rest[1].ticket_id;
                        rest[0].owning = rest[1].owning;
                        rest[0].last = rest[1].last;

                        rest.pop_back();
                    }

                    return false;
                }
            },
            dst);
    }

    void task::upload(VkCommandBuffer cmd_buf, VkBuffer src_buf, uint32_t src_buf_offset) {
        std::visit(
            [&](auto&& dst) {
                using Dst = std::decay_t<decltype(dst)>;
                if constexpr (std::same_as<BufferDst, Dst>) {
                    VkBufferCopy region{
                        .srcOffset = src_buf_offset,
                        .dstOffset = dst.offset,
                        .size = dst.src_size,
                    };

                    vkCmdCopyBuffer(cmd_buf, src_buf, dst.buffer, 1, &region);
                    if (!last) return;

                    state->transport_queue_buffer_barriers.emplace_back(VkBufferMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .pNext = nullptr,
                        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                        .dstAccessMask = VK_ACCESS_2_NONE,
                        .srcQueueFamilyIndex = engine::state->transport_queue_family,
                        .dstQueueFamilyIndex = engine::state->graphics_queue_family,
                        .buffer = dst.buffer,
                        .offset = dst.initial_offset,
                        .size = src_offset + dst.src_size,
                    });

                    state->graphics_queue_buffer_barriers.emplace_back(VkBufferMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .pNext = nullptr,
                        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                        .srcAccessMask = VK_ACCESS_2_NONE,
                        .dstStageMask = dst_stage,
                        .dstAccessMask = dst_access,
                        .srcQueueFamilyIndex = engine::state->transport_queue_family,
                        .dstQueueFamilyIndex = engine::state->graphics_queue_family,
                        .buffer = dst.buffer,
                        .offset = dst.initial_offset,
                        .size = src_offset + dst.src_size,
                    });
                } else {
                    VkBufferImageCopy region{
                        .bufferOffset = src_buf_offset,
                        .bufferRowLength = 0,
                        .bufferImageHeight = 0,
                        .imageSubresource = dst.subresource,
                        .imageOffset = dst.offset,
                        .imageExtent = dst.extent,
                    };

                    vkCmdCopyBufferToImage(cmd_buf, src_buf, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                           &region);
                    if (!last) return;

                    state->transport_queue_image_barriers.emplace_back(VkImageMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .pNext = nullptr,
                        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                        .dstAccessMask = VK_ACCESS_2_NONE,
                        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .newLayout = dst.new_layout,
                        .srcQueueFamilyIndex = engine::state->transport_queue_family,
                        .dstQueueFamilyIndex = engine::state->graphics_queue_family,
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

                    state->graphics_queue_image_barriers.emplace_back(VkImageMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .pNext = nullptr,
                        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                        .srcAccessMask = VK_ACCESS_2_NONE,
                        .dstStageMask = dst_stage,
                        .dstAccessMask = dst_access,
                        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .newLayout = dst.new_layout,
                        .srcQueueFamilyIndex = engine::state->transport_queue_family,
                        .dstQueueFamilyIndex = engine::state->graphics_queue_family,
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

    bool is_timeline_ready(uint64_t timeline) {
        if (state->finished_timeline >= timeline) return true;

        VkSemaphoreWaitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        info.semaphoreCount = 1;
        info.pSemaphores = &state->timeline_semaphore;
        info.pValues = &timeline;

        auto res = vkWaitSemaphores(device(), &info, 0);
        if (res == VK_SUCCESS) {
            state->finished_timeline = std::max(timeline, state->finished_timeline);
            return true;
        } else {
            return false;
        }
    }

    void thread() {
        while (!state->stop_worker) {
            std::lock_guard lock{state->full_upload_lock};

            state->task_queue_lock.lock();
            auto& task_queue = state->task_queues[state->current_task_queue];
            state->current_task_queue = (state->current_task_queue + 1) % state->task_queues.size();
            state->task_queue_lock.unlock();

            if (task_queue.empty()) {
                _mm_pause();
                continue;
            }

            auto& cmd_buf = state->cmd_bufs[state->current_cmd_buf];
            auto& cmd_buf_fence = state->cmd_buf_fences[state->current_cmd_buf];
            auto& transport_graphics_semaphore = state->transport_graphics_semaphores[state->current_cmd_buf];
            auto& staging_buffer = state->staging_buffers[state->current_cmd_buf];
            auto& staging_buffer_ptr = state->staging_buffer_ptrs[state->current_cmd_buf];
            state->current_cmd_buf = (state->current_cmd_buf + 1) % state->cmd_bufs.size();

            VK_CHECK(vkWaitForFences(device(), 1, &cmd_buf_fence, true, UINT64_MAX));
            VK_CHECK(vkResetFences(device(), 1, &cmd_buf_fence));
            VK_CHECK(vkResetCommandBuffer(cmd_buf, 0));

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

            if (!state->transport_queue_buffer_barriers.empty() || !state->transport_queue_image_barriers.empty()) {
                VkDependencyInfo dep_info{};
                dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep_info.pNext = nullptr;
                dep_info.bufferMemoryBarrierCount = state->transport_queue_buffer_barriers.size();
                dep_info.pBufferMemoryBarriers = state->transport_queue_buffer_barriers.data();
                dep_info.imageMemoryBarrierCount = state->transport_queue_image_barriers.size();
                dep_info.pImageMemoryBarriers = state->transport_queue_image_barriers.data();
                vkCmdPipelineBarrier2(cmd_buf, &dep_info);

                state->transport_queue_buffer_barriers.clear();
                state->transport_queue_image_barriers.clear();
            }

            std::vector<task> tasks;
            uint32_t size = 0;
            while (!task_queue.empty()) {
                auto& t = task_queue.front();

                auto budget = state->staging_buffer_size - size;
                if (budget == 0) break;
                if (budget < t.required_size()) {
                    std::vector<task> rest_tasks{};
                    if (task_queue[0].split(budget, rest_tasks)) continue;

                    task_queue.insert(task_queue.begin() + 1, rest_tasks.begin(), rest_tasks.end());
                    t = task_queue[0];
                }

                t.upload((uint8_t*)staging_buffer_ptr + size);

                size += t.required_size();
                if (std::holds_alternative<task::BufferDst>(t.dst)) {
                    auto dst = std::get<task::BufferDst>(t.dst);
                }
                tasks.emplace_back(t);
                task_queue.pop_front();
            }

            if (state->flush_staging_buffer) staging_buffer.flush_mapped(0, state->staging_buffer_size);

            std::vector<std::variant<VkBufferMemoryBarrier2, VkImageMemoryBarrier2>> graphics_barriers;
            uint32_t upload_offset = 0;
            for (auto& task : tasks) {
                task.upload(cmd_buf, staging_buffer, upload_offset);
                upload_offset += task.required_size();
            }

            if (!state->transport_queue_buffer_barriers.empty() || !state->transport_queue_image_barriers.empty()) {
                VkDependencyInfo dep_info{};
                dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep_info.bufferMemoryBarrierCount = state->transport_queue_buffer_barriers.size();
                dep_info.pBufferMemoryBarriers = state->transport_queue_buffer_barriers.data();
                dep_info.imageMemoryBarrierCount = state->transport_queue_image_barriers.size();
                dep_info.pImageMemoryBarriers = state->transport_queue_image_barriers.data();
                vkCmdPipelineBarrier2(cmd_buf, &dep_info);

                state->transport_queue_buffer_barriers.clear();
                state->transport_queue_image_barriers.clear();
            }

            VK_CHECK(vkEndCommandBuffer(cmd_buf));

            VkCommandBufferSubmitInfo cmd_info{};
            cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmd_info.commandBuffer = cmd_buf;

            // VkSemaphoreSubmitInfo signal_info{};
            // signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            // signal_info.semaphore = timeline_semaphore;
            // signal_info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            // signal_info.value = ++timeline_counter;
            VkSemaphoreSubmitInfo signal_info{};
            signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal_info.semaphore = transport_graphics_semaphore;
            signal_info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            VkSubmitInfo2 submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount = 0;
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &cmd_info;
            submit_info.signalSemaphoreInfoCount = 1;
            submit_info.pSignalSemaphoreInfos = &signal_info;

            VK_CHECK(vkQueueSubmit2(engine::state->transport_queue, 1, &submit_info, cmd_buf_fence));

            synchronization::submit_from_another_thread(state->graphics_queue_buffer_barriers, state->graphics_queue_image_barriers,
                                                        {},
                                                        VkSemaphoreSubmitInfo{
                                                            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                                            .semaphore = transport_graphics_semaphore,
                                                            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                        },
                                                        VkSemaphoreSubmitInfo{
                                                            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                                            .semaphore = state->timeline_semaphore,
                                                            .value = ++state->timeline_counter,
                                                            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                        });

            state->graphics_queue_buffer_barriers.clear();
            state->graphics_queue_image_barriers.clear();

            {
                std::lock_guard lock{state->ticket_mutex};
                for (auto& task : tasks) {
                    state->ticket_timelines[task.ticket_id].second = state->timeline_counter;
                }
            }
        }
    }

    void init() {
        state = new State{};

        for (size_t i = 0; i < state->staging_buffers.size(); i++) {
            state->staging_buffers[i] =
                Buffer::create(std::format("Transport buffer #{}", i).c_str(), state->staging_buffer_size,
                               VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT, {{&state->staging_buffer_ptrs[i], &state->flush_staging_buffer}});
        }

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = engine::state->transport_queue_family;
        VK_CHECK(vkCreateCommandPool(device(), &pool_info, nullptr, &state->cmd_pool));

        VkCommandBufferAllocateInfo cmd_buf_alloc_info{};
        cmd_buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buf_alloc_info.commandBufferCount = 1;
        cmd_buf_alloc_info.commandPool = state->cmd_pool;
        cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        for (auto& cmd_buf : state->cmd_bufs) {
            VK_CHECK(vkAllocateCommandBuffers(device(), &cmd_buf_alloc_info, &cmd_buf));
        }

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.pNext = nullptr;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (auto& fence : state->cmd_buf_fences) {
            VK_CHECK(vkCreateFence(device(), &fence_info, nullptr, &fence));
        }

        VkSemaphoreTypeCreateInfo semaphore_type{};
        semaphore_type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        semaphore_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        semaphore_type.initialValue = 0;

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = &semaphore_type;

        vkCreateSemaphore(device(), &semaphore_info, nullptr, &state->timeline_semaphore);

        semaphore_info.pNext = nullptr;
        for (auto& semaphore : state->transport_graphics_semaphores) {
            vkCreateSemaphore(device(), &semaphore_info, nullptr, &semaphore);
        }

        state->worker = std::thread{thread};
    }

    void destroy() {
        state->stop_worker = true;
        state->worker.join();

        for (auto buf : state->staging_buffers) {
            buf.destroy();
        }
        vkDestroyCommandPool(device(), state->cmd_pool, nullptr);

        for (auto& fence : state->cmd_buf_fences) {
            vkDestroyFence(device(), fence, nullptr);
        }

        vkDestroySemaphore(device(), state->timeline_semaphore, nullptr);
        for (auto semaphore : state->transport_graphics_semaphores) {
            vkDestroySemaphore(device(), semaphore, nullptr);
        }

        delete state;
    }

    bool is_ready(ticket t) {
        if (t == ticket{}) return false;
        std::lock_guard lock{state->ticket_mutex};
        auto [gen, timeline] = state->ticket_timelines[t.id()];

        if (t.gen() < gen) return true;
        if (timeline == 0) return false;

        return is_timeline_ready(timeline);
    }

    VkSemaphoreSubmitInfo wait_on(std::span<ticket> tickets) {
        uint64_t largest_timeline_value = 0;
        for (auto t : tickets) {
            if (t == ticket{}) continue;

            {
                std::lock_guard lock{state->ticket_mutex};
                if (state->ticket_timelines[t.id()].first > t.gen()) continue;
            }

            bool spin = false;
            do {
                std::lock_guard lock{state->ticket_mutex};
                spin = state->ticket_timelines[t.id()].second == 0;
            } while (spin);

            {
                std::lock_guard lock{state->ticket_mutex};
                if (state->ticket_timelines[t.id()].first > t.gen()) continue;

                largest_timeline_value = std::max(largest_timeline_value, state->ticket_timelines[t.id()].second);
            }
        }

        return VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                     .pNext = nullptr,
                                     .semaphore = state->timeline_semaphore,
                                     .value = largest_timeline_value,
                                     .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    }

    ticket upload(bool priority, void* src, std::optional<FreeFn*> own, uint32_t size, VkBuffer dst,
                  uint32_t dst_offset, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        auto ticket = get_free_ticket();

        task task = {
            .dst =
                task::BufferDst{
                    .src_size = size,
                    .buffer = dst,
                    .offset = dst_offset,
                    .initial_offset = dst_offset,
                },
            .src = src,
            .src_offset = 0,
            .ticket_id = ticket.id(),
            .owning = own,
            .last = true,
            .dst_stage = dst_stage,
            .dst_access = dst_access,
        };

        {
            std::lock_guard lock{state->task_queue_lock};
            auto& task_queue = state->task_queues[state->current_task_queue];

            if (priority) task_queue.emplace_front(task);
            else task_queue.emplace_back(task);
        }

        return ticket;
    }

    ticket upload(bool priority, VkFormat format, VkExtent3D dimension, void* src, std::optional<FreeFn*> own,
                  VkImage dst, VkImageSubresourceLayers dst_layers, VkOffset3D dst_offset, VkImageLayout current_layout,
                  VkImageLayout dst_layout, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        auto ticket = get_free_ticket();

        {
            std::lock_guard lock{state->full_upload_lock};
            state->transport_queue_image_barriers.emplace_back(VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = current_layout,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image = dst,
                .subresourceRange =
                    VkImageSubresourceRange{
                        .aspectMask = dst_layers.aspectMask,
                        .baseMipLevel = dst_layers.mipLevel,
                        .levelCount = 1,
                        .baseArrayLayer = dst_layers.baseArrayLayer,
                        .layerCount = dst_layers.layerCount,
                    },
            });
        }

        std::vector<task> tasks{};
        tasks.reserve(dst_layers.layerCount);

        for (uint32_t i = 0; i < dst_layers.layerCount; i++) {
            tasks.emplace_back(task{
                .dst =
                    task::ImageDst{
                        .image = dst,
                        .subresource =
                            VkImageSubresourceLayers{
                                .aspectMask = dst_layers.aspectMask,
                                .mipLevel = dst_layers.mipLevel,
                                .baseArrayLayer = dst_layers.baseArrayLayer + i,
                                .layerCount = 1,
                            },
                        .initial_base_array_layer = dst_layers.baseArrayLayer,

                        .offset = dst_offset,
                        .extent = dimension,

                        .src_row_length = dimension.width,

                        .format = format,
                        .new_layout = dst_layout,
                    },
                .src = src,
                .src_offset = 0,
                .ticket_id = ticket.id(),
                .owning = own,
                .last = i == (dst_layers.layerCount - 1),
                .dst_stage = dst_stage,
                .dst_access = dst_access,
            });
        }

        {
            std::lock_guard lock{state->task_queue_lock};
            auto& task_queue = state->task_queues[state->current_task_queue];

            if (priority) {
                task_queue.insert(task_queue.begin(), tasks.rbegin(), tasks.rend());
            } else {
                task_queue.insert(task_queue.end(), tasks.begin(), tasks.end());
            }
        }

        return ticket;
    }

    void unqueue(ticket t, bool free) {
        {
            std::lock_guard lock{state->task_queue_lock};
            auto& task_queue = state->task_queues[state->current_task_queue];

            for (size_t i = 0; i < task_queue.size(); i++) {
                if (task_queue[i].ticket_id != t.id()) continue;
                if (state->ticket_timelines[t.id()].first != t.gen()) return;

                auto task = task_queue[i];
                task_queue.erase(task_queue.begin() + i);

                if (free && task.owning) {
                    (*task.owning)(task.src);
                }
                return;
            }
        }

        {
            std::lock_guard lock1{state->full_upload_lock};
            std::lock_guard lock2{state->task_queue_lock};

            auto& task_queue = state->task_queues[(state->current_task_queue + 1) % state->task_queues.size()];

            for (size_t i = 0; i < task_queue.size(); i++) {
                if (task_queue[i].ticket_id != t.id()) continue;
                if (state->ticket_timelines[t.id()].first != t.gen()) return;

                auto task = task_queue[i];
                task_queue.erase(task_queue.begin() + i);

                if (free && task.owning) {
                    (*task.owning)(task.src);
                }
                return;
            }
        }
    }

    uint64_t get_timeline() {
        uint64_t v;
        vkGetSemaphoreCounterValue(device(), state->timeline_semaphore, &v);
        return v;
    }

    uint64_t get_timeline(ticket t) {
        return state->ticket_timelines[t.id()].second;
    }

    void* get_internal_state() {
        return state;
    }

    void set_internal_state(void* s) {
        state = (State*)s;
    }
}
