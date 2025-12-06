#pragma once

#include "goliath/buffer.hpp"
#include <cstdint>

#include <volk.h>

namespace engine::culling {
    struct DrawCommand {
        VkDrawIndirectCommand vk_cmd;
        uint32_t transform_offset;
    };

    struct CulledDrawCommand {
        VkDrawIndirectCommand vk_cmd;
        uint32_t draw_id;
    };

    void init(uint32_t max_tasks);
    void destroy();
    void resize(uint32_t max_draw_count);

    void bind_flatten();
    void flatten(uint64_t group_addr, uint32_t draw_count, uint64_t draw_buffer_addr, uint64_t transforms_addr,
                 uint32_t default_transform_offset);

    void cull(uint32_t max_draw_count, uint64_t draw_id_addr, uint64_t indirect_draw_addr);

    void sync_for_draw(Buffer& draw_id_buffer, Buffer& indirect_draw_addr);

    void clear_buffers(Buffer& draw_ids, Buffer& indirect_draws,
                       VkAccessFlags2 draw_ids_src_access = VK_ACCESS_2_SHADER_READ_BIT,
                       VkPipelineStageFlags2 draw_ids_src_stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                       VkAccessFlags2 indirect_draws_access = VK_ACCESS_2_SHADER_READ_BIT |
                                                              VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                       VkPipelineStageFlags2 indirect_draws_stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
}
