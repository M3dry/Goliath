#include "goliath/culling.hpp"
#include "goliath/buffer.hpp"
#include "goliath/compute.hpp"
#include "goliath/engine.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/rendering.hpp"
#include "goliath/synchronization.hpp"
#include <vulkan/vulkan_core.h>

namespace engine::culling {
    using FlattenDrawPC =
        engine::PushConstant<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t>;
    using CullingPC = engine::PushConstant<uint64_t, uint64_t, uint64_t, uint64_t, uint32_t>;

    ComputePipeline flatten_draw_pipeline;
    ComputePipeline culling_pipeline;

    uint32_t max_task_count = 0;
    uint64_t task_capacity;
    Buffer task_buffers[frames_in_flight]{};
    Buffer task_data_buffers[frames_in_flight]{};

    constexpr uint32_t task_size = 16;
    constexpr uint32_t task_data_size = 36;

    void init(uint32_t max_tasks) {
        resize(max_tasks);

        uint32_t flatten_draw_size;
        auto* flatten_draw_spv = util::read_file("./flatten_draw.spv", &flatten_draw_size);
        auto flatten_draw_module = engine::create_shader({flatten_draw_spv, flatten_draw_size});
        flatten_draw_pipeline =
            ComputePipeline{ComputePipelineBuilder{}.shader(flatten_draw_module).push_constant(FlattenDrawPC::size)};
        free(flatten_draw_spv);

        uint32_t culling_size;
        auto* culling_spv = util::read_file("./culling.spv", &culling_size);
        auto culling_module = engine::create_shader({culling_spv, culling_size});
        culling_pipeline =
            ComputePipeline{ComputePipelineBuilder{}.shader(culling_module).push_constant(CullingPC::size)};
        free(culling_spv);

        engine::destroy_shader(flatten_draw_module);
        engine::destroy_shader(culling_module);
    }

    void destroy() {
        for (size_t i = 0; i < frames_in_flight; i++) {
            task_buffers[i].destroy();
            task_data_buffers[i].destroy();
        }

        flatten_draw_pipeline.destroy();
        culling_pipeline.destroy();
    }

    void resize(uint32_t max_tasks) {
        max_task_count = max_tasks;
        for (size_t i = 0; i < frames_in_flight; i++) {
            task_buffers[i].destroy();
            task_data_buffers[i].destroy();
        }

        for (size_t i = 0; i < frames_in_flight; i++) {
            task_buffers[i] =
                Buffer::create("culling tasks buffer", max_tasks * task_size,
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);
            task_data_buffers[i] =
                Buffer::create("culling task data buffer", max_tasks * task_data_size,
                               VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);
        }
    }

    void bind_flatten() {
        flatten_draw_pipeline.bind();
    }

    void flatten(uint64_t group_addr, uint32_t draw_count, uint64_t draw_buffer_addr, uint64_t transforms_addr,
                 uint32_t default_transform_offset) {
        uint8_t pc[FlattenDrawPC::size]{};
        FlattenDrawPC::write(pc, group_addr, draw_buffer_addr, task_data_buffers[engine::get_current_frame()].address(),
                             task_buffers[engine::get_current_frame()].address(), transforms_addr,
                             default_transform_offset, draw_count, max_task_count);

        flatten_draw_pipeline.dispatch(ComputePipeline::DispatchParams{
            .push_constant = pc,
            .group_count_x = (uint32_t)std::ceil(draw_count / 64.0f),
            .group_count_y = 1,
            .group_count_z = 1,
        });
    }

    void cull(uint32_t max_draw_count, uint64_t draw_id_addr, uint64_t indirect_draw_addr) {
        auto& task_data_buffer = task_data_buffers[engine::get_current_frame()];
        auto& task_buffer = task_buffers[engine::get_current_frame()];

        VkBufferMemoryBarrier2 task_data_barrier{};
        task_data_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        task_data_barrier.pNext = nullptr;
        task_data_barrier.buffer = task_data_buffer;
        task_data_barrier.offset = 0;
        task_data_barrier.size = task_data_buffer.size();
        task_data_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        task_data_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        task_data_barrier.srcAccessMask = 0;
        task_data_barrier.srcStageMask = 0;
        task_data_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        task_data_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        task_data_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        task_data_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        VkBufferMemoryBarrier2 task_barrier{};
        task_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        task_barrier.pNext = nullptr;
        task_barrier.buffer = task_buffer;
        task_barrier.offset = 0;
        task_barrier.size = task_buffer.size();
        task_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        task_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        task_barrier.srcAccessMask = 0;
        task_barrier.srcStageMask = 0;
        task_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        task_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        task_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        task_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        engine::synchronization::begin_barriers();
        engine::synchronization::apply_barrier(task_data_barrier);
        engine::synchronization::apply_barrier(task_barrier);
        engine::synchronization::end_barriers();

        uint8_t pc[CullingPC::size]{};
        CullingPC::write(pc, task_data_buffers[engine::get_current_frame()].address(),
                         task_buffers[engine::get_current_frame()].address(), indirect_draw_addr, draw_id_addr,
                         max_draw_count);

        culling_pipeline.bind();
        culling_pipeline.dispatch(ComputePipeline::DispatchParams{
            .push_constant = pc,
            .group_count_x = (uint32_t)std::ceil(max_task_count / 32.0f),
            .group_count_y = 1,
            .group_count_z = 1,
        });
    }

    void sync_for_draw(Buffer& draw_id_buffer, Buffer& indirect_draw_buffer) {
        VkBufferMemoryBarrier2 draw_id_barrier{};
        draw_id_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        draw_id_barrier.pNext = nullptr;
        draw_id_barrier.buffer = draw_id_buffer;
        draw_id_barrier.offset = 0;
        draw_id_barrier.size = draw_id_buffer.size();
        draw_id_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        draw_id_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        draw_id_barrier.srcAccessMask = 0;
        draw_id_barrier.srcStageMask = 0;
        draw_id_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        draw_id_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        draw_id_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        draw_id_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;

        VkBufferMemoryBarrier2 indirect_draw_barrier{};
        indirect_draw_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        indirect_draw_barrier.pNext = nullptr;
        indirect_draw_barrier.buffer = indirect_draw_buffer;
        indirect_draw_barrier.offset = 0;
        indirect_draw_barrier.size = indirect_draw_buffer.size();
        indirect_draw_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        indirect_draw_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        indirect_draw_barrier.srcAccessMask = 0;
        indirect_draw_barrier.srcStageMask = 0;
        indirect_draw_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        indirect_draw_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        indirect_draw_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        indirect_draw_barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

        engine::synchronization::apply_barrier(draw_id_barrier);
        engine::synchronization::apply_barrier(indirect_draw_barrier);
    }

    void clear_buffers(Buffer& draw_ids, Buffer& indirect_draws, VkAccessFlags2 draw_ids_src_access,
                       VkPipelineStageFlags2 draw_ids_src_stage, VkAccessFlags2 indirect_draws_access,
                       VkPipelineStageFlags2 indirect_draws_stage) {
        auto& task_data_buffer = task_data_buffers[engine::get_current_frame()];
        auto& task_buffer = task_buffers[engine::get_current_frame()];

        VkBufferMemoryBarrier2 task_data_barrier{};
        task_data_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        task_data_barrier.pNext = nullptr;
        task_data_barrier.buffer = task_data_buffer;
        task_data_barrier.offset = 0;
        task_data_barrier.size = task_data_buffer.size();
        task_data_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        task_data_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        task_data_barrier.srcAccessMask = 0;
        task_data_barrier.srcStageMask = 0;
        task_data_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        task_data_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        task_data_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        task_data_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        VkBufferMemoryBarrier2 task_barrier{};
        task_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        task_barrier.pNext = nullptr;
        task_barrier.buffer = task_buffer;
        task_barrier.offset = 0;
        task_barrier.size = task_buffer.size();
        task_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        task_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        task_barrier.srcAccessMask = 0;
        task_barrier.srcStageMask = 0;
        task_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        task_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        task_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        task_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        VkBufferMemoryBarrier2 draw_ids_barrier{};
        draw_ids_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        draw_ids_barrier.pNext = nullptr;
        draw_ids_barrier.buffer = draw_ids;
        draw_ids_barrier.offset = 0;
        draw_ids_barrier.size = draw_ids.size();
        draw_ids_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        draw_ids_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        draw_ids_barrier.srcAccessMask = 0;
        draw_ids_barrier.srcStageMask = 0;
        draw_ids_barrier.srcAccessMask = draw_ids_src_access;
        draw_ids_barrier.srcStageMask = draw_ids_src_stage;
        draw_ids_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        draw_ids_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        VkBufferMemoryBarrier2 indirect_draws_barrier{};
        indirect_draws_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        indirect_draws_barrier.pNext = nullptr;
        indirect_draws_barrier.buffer = indirect_draws;
        indirect_draws_barrier.offset = 0;
        indirect_draws_barrier.size = indirect_draws.size();
        indirect_draws_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        indirect_draws_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        indirect_draws_barrier.srcAccessMask = 0;
        indirect_draws_barrier.srcStageMask = 0;
        indirect_draws_barrier.srcAccessMask = indirect_draws_access;
        indirect_draws_barrier.srcStageMask = indirect_draws_stage;
        indirect_draws_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        indirect_draws_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        engine::synchronization::begin_barriers();
        engine::synchronization::apply_barrier(task_barrier);
        engine::synchronization::apply_barrier(task_data_barrier);
        engine::synchronization::apply_barrier(draw_ids_barrier);
        engine::synchronization::apply_barrier(indirect_draws_barrier);
        engine::synchronization::end_barriers();

        auto cmd_buf = get_cmd_buf();

        vkCmdFillBuffer(cmd_buf, draw_ids.data(), 0, draw_ids.size(), 0);
        vkCmdFillBuffer(cmd_buf, indirect_draws.data(), 0, indirect_draws.size(), 0);

        vkCmdFillBuffer(cmd_buf, task_buffer.data(), 0, task_buffer.size(), 0);
        vkCmdFillBuffer(cmd_buf, task_data_buffer.data(), 0, task_data_buffer.size(), 0);
    }
}
