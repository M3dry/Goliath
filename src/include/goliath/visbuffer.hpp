#pragma once

#include "goliath/buffer.hpp"
#include "goliath/rendering.hpp"
#include <volk.h>

namespace engine::visbuffer {
    static constexpr VkFormat format = VK_FORMAT_R32_UINT;
    extern VkDescriptorSetLayout shading_set_layout;

    extern Buffer stages;
    extern uint32_t material_count_buffer_size;
    extern std::array<uint32_t, frames_in_flight> material_count_buffer_offsets;
    extern uint32_t offsets_buffer_size;
    extern std::array<uint32_t, frames_in_flight> offsets_buffer_offsets;
    extern uint32_t shading_dispatch_buffer_size;
    extern std::array<uint32_t, frames_in_flight> shading_dispatch_buffer_offsets;
    extern uint32_t fragment_id_buffer_size;
    extern std::array<uint32_t, frames_in_flight> fragment_id_buffer_offsets;

    void init(glm::uvec2 dims);
    void resize(glm::uvec2 new_dims, bool material_count_changed);
    void destroy();

    void push_material(uint16_t n = 1);
    void pop_material(uint16_t n = 1);

    VkImageView get_view();
    VkImage get_image();

    VkImageMemoryBarrier2 transition_to_attachement();
    VkImageMemoryBarrier2 transition_to_general();

    RenderingAttachement attach();

    void prepare_for_draw();
    void count_materials(uint64_t draw_id_addr);
    void get_offsets();
    void write_fragment_ids(uint64_t draw_id_addr);

    struct Shading {
        uint64_t indirect_buffer_offset;
        uint64_t fragment_id_buffer_offset;
        uint64_t vis_and_target_set;
        uint16_t material_id_count;
    };

    // `target` is expected to be already synced and it's layout to be GENERAL
    Shading shade(VkImageView target);

    void clear_buffers();
}
