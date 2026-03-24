#pragma once

#include "game.hpp"
#include "goliath/assets.hpp"
#include "goliath/material.hpp"
#include "goliath/models.hpp"
#include <vulkan/vulkan_core.h>

namespace ui {
    void init();
    void destroy();
    void tick(float dt);
    void begin();

    void viewport_window(GameView& scene_viewport, bool& scene_focused, GameView* game_viewport, bool* game_focused);

    void assets_pane();
    void assets_entry_pre(engine::models::gid gid, uint32_t ix);
    void assets_entry_pre(engine::Materials::gid gid, uint32_t ix);
    void assets_entry_pre(engine::Textures::gid gid, uint32_t ix);
    void assets_entry_post(engine::models::gid gid, uint32_t ix);
    void assets_entry_post(engine::Materials::gid gid, uint32_t ix);
    void assets_entry_post(engine::Textures::gid gid, uint32_t ix);
    void assets_entry_drag_preview(engine::models::gid gid);
    void assets_entry_drag_preview(engine::Materials::gid gid);
    void assets_entry_drag_preview(engine::Textures::gid gid);

    void instances_pane();
    size_t instance_entry(size_t scene_ix, size_t instance_ix);

    void transform_pane();

    void selected_model_materials_pane();

    void material_windows();
    bool material_inputs(const engine::Material& schema, std::span<uint8_t> data);

    void rename_popup();

    void scenes_settings_pane();
    void scene_settings_pane(size_t scene_ix);

    void assets_inputs_pane(engine::Assets& assets);

    void new_material_instance_creation();
    void material_instance_creation();

    void material_schema_creation();

    void remove_asset_popup();

    void lights_pane();
}
