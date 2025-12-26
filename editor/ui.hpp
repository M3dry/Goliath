#pragma once

#include "goliath/texture.hpp"
#include "imgui.h"
#include "models.hpp"
#include "scene.hpp"
#include <vulkan/vulkan_core.h>

namespace ui {
    void init();
    void destroy();

    std::optional<VkImageMemoryBarrier2> game_window();
    bool skipped_game_window();
    std::optional<VkImageMemoryBarrier2> blit_game_window(VkBlitImageInfo2 src_blit_info);
    ImVec2 game_window_size();
    engine::GPUImage get_window_image();

    void models_pane();
    void model_entry(models::gid gid);

    void instances_pane(glm::mat4* transforms);
    size_t instance_entry(scene::Scene& current_scene, size_t ix, glm::mat4& transform);

    void transform_pane(glm::mat4* transforms);

    void scene_pane();
}
