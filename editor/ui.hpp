#pragma once

#include "goliath/camera.hpp"
#include "goliath/material.hpp"
#include "goliath/texture.hpp"
#include "imgui.h"
#include "goliath/models.hpp"
#include <vulkan/vulkan_core.h>

namespace ui {
    void init();
    void destroy();
    void tick(float dt);
    void begin();

    bool game_window();
    bool skipped_game_window();
    std::optional<VkImageMemoryBarrier2> blit_game_window(VkBlitImageInfo2 src_blit_info);
    ImVec2 game_window_size();
    engine::GPUImage get_window_image();

    void assets_pane();
    void assets_entry_pre(engine::models::gid gid, uint32_t ix);
    void assets_entry_pre(engine::textures::gid gid, uint32_t ix);
    void assets_entry_post(engine::models::gid gid, uint32_t ix);
    void assets_entry_post(engine::textures::gid gid, uint32_t ix);
    void assets_entry_drag_preview(engine::models::gid gid);
    void assets_entry_drag_preview(engine::textures::gid gid);

    void instances_pane();
    size_t instance_entry(size_t scene_ix, size_t instance_ix);

    void transform_pane();

    // void scene_pane();

    void selected_model_materials_pane();
    bool material_inputs(const engine::Material& schema, std::span<uint8_t> data);

    void rename_popup();
}
