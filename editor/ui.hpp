#pragma once

#include "models.hpp"
#include "scene.hpp"

namespace ui {
    void models_pane();
    void model_entry(models::gid gid);

    void instances_pane(glm::mat4* transforms);
    size_t instance_entry(scene::Scene& current_scene, size_t ix, glm::mat4& transform);

    void transform_pane(glm::mat4* transforms);

    void scene_pane();
}
