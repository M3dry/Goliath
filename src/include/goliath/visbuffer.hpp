#pragma once

#include "goliath/buffer.hpp"
#include "goliath/engine.hpp"
#include "goliath/rendering.hpp"
#include "goliath/texture.hpp"
#include <volk.h>
#include <vulkan/vulkan_core.h>

namespace engine {
    struct VisBuffer {
        Buffer stages;

        uint32_t max_material_id;
        bool material_count_changed;

        uint32_t material_count_buffer_size;
        std::array<uint32_t, frames_in_flight> material_count_buffer_offsets;
        uint32_t offsets_buffer_size;
        std::array<uint32_t, frames_in_flight> offsets_buffer_offsets;
        uint32_t shading_dispatch_buffer_size;
        std::array<uint32_t, frames_in_flight> shading_dispatch_buffer_offsets;
        uint32_t fragment_id_buffer_size;
        std::array<uint32_t, frames_in_flight> fragment_id_buffer_offsets;

        glm::uvec2 dimensions;
        std::array<GPUImage, frames_in_flight> images;
        std::array<VkImageView, frames_in_flight> image_views;

        RenderingAttachement attach(uint32_t current_frame) {
            return RenderingAttachement{}
                .set_image(image_views[current_frame % frames_in_flight], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL)
                .set_clear_color(glm::vec4{0.0f})
                .set_load_op(LoadOp::Clear)
                .set_store_op(StoreOp::Store);
        }
    };
}

namespace engine::visbuffer {
    static constexpr VkFormat format = VK_FORMAT_R32_UINT;
    extern VkDescriptorSetLayout shading_layout;

    void init();
    void destroy();

    VisBuffer create(glm::uvec2 dimensions);
    void resize(VisBuffer& visbuffer, glm::uvec2 new_dimensions);
    void destroy(VisBuffer& visbuffer);

    void push_material(VisBuffer& visbuffer, uint16_t n = 1);
    void pop_material(VisBuffer& visbuffer, uint16_t n = 1);

    void clear_buffers(VisBuffer& visbuffer, uint32_t current_frame);

    void prepare_for_draw(VisBuffer& visbuffer, uint32_t current_frame);
    void count_materials(VisBuffer& visbuffer, uint64_t draw_id_addr, uint32_t current_frame);
    void get_offsets(VisBuffer& visbuffer, uint32_t current_frame);
    void write_fragment_ids(VisBuffer& visbuffer, uint64_t draw_id_addr, uint32_t current_frame);

    struct Shading {
        uint64_t indirect_buffer_offset;
        uint64_t fragment_id_buffer_offset;
        uint64_t vis_and_target_set;
        uint16_t material_id_count;
    };

    // `target` is expected to be already synced and it's layout to be GENERAL
    Shading shade(VisBuffer& visbuffer, VkImageView target, uint32_t current_frame);
}
