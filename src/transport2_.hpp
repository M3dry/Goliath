#pragma once

#include "goliath/buffer.hpp"
#include "goliath/transport2.hpp"
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace engine::transport2 {
    struct task {
        struct BufferDst {
            uint32_t src_size;

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

            uint32_t src_row_length;

            VkFormat format;
            VkImageLayout new_layout;
        };

        std::variant<BufferDst, ImageDst> dst;
        void* src;
        uint32_t src_offset;
        uint32_t ticket_id;
        std::optional<FreeFn*> owning;
        bool last;

        VkPipelineStageFlagBits2 dst_stage;
        VkAccessFlagBits2 dst_access;

        size_t required_size() const;
        void upload(uint8_t* out);
        bool split(uint32_t budget, std::vector<task>& rest);
        void upload(VkCommandBuffer cmd_buf, VkBuffer src_buf, uint32_t src_buf_offset);
    };

    struct State {
        std::mutex full_upload_lock{};

        std::vector<VkBufferMemoryBarrier2> transport_queue_buffer_barriers{};
        std::vector<VkImageMemoryBarrier2> transport_queue_image_barriers{};

        std::vector<VkBufferMemoryBarrier2> graphics_queue_buffer_barriers{};
        std::vector<VkImageMemoryBarrier2> graphics_queue_image_barriers{};

        bool stop_worker = false;
        std::thread worker;

        std::mutex ticket_mutex{};
        std::vector<std::pair<uint64_t, uint64_t>> ticket_timelines{};
        std::vector<ticket> free_tickets{};

        uint64_t timeline_counter = 0;
        uint64_t finished_timeline = 0;
        VkSemaphore timeline_semaphore;

        VkCommandPool cmd_pool;
        uint32_t current_cmd_buf = 0;
        std::array<VkCommandBuffer, 2> cmd_bufs;
        std::array<VkFence, 2> cmd_buf_fences;
        std::array<VkSemaphore, 2> transport_graphics_semaphores;

        static constexpr uint32_t staging_buffer_size = 8000000;
        std::array<Buffer, 2> staging_buffers{};
        std::array<void*, 2> staging_buffer_ptrs{};
        bool flush_staging_buffer = false;

        uint32_t current_task_queue = 0;
        std::array<std::deque<task>, 2> task_queues{};
        std::mutex task_queue_lock{};

        std::mutex graphics_barriers_lock{};
    };

    void init();
    void destroy();
}
