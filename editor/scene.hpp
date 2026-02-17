#pragma once

#include "goliath/camera.hpp"
#include "goliath/models.hpp"
#include <cstddef>
#include <nlohmann/json.hpp>

namespace scene {
    struct CameraInfo {
        engine::Camera cam{};
        float fov = 90.0f;
        float sensitivity = 0.5f;
        float movement_speed = 0.5f;

        CameraInfo() {
            cam.position = {10,10,10};
            cam.set_projection(engine::camera::Perspective{
                .fov = glm::radians(fov),
                .aspect_ratio = 16.0f/9.0f
            });
            cam.look_at({0,0,0});
            cam.update_matrices();
        }
    };
    void to_json(nlohmann::json& j, const CameraInfo& v);
    void from_json(const nlohmann::json& j, CameraInfo& v);

    void load(nlohmann::json j);
    nlohmann::json save();
    nlohmann::json default_json();
    bool want_to_save();
    void modified();

    void destroy();


    size_t selected_scene();
    void select_scene(size_t scene_ix);

    size_t selected_instance();
    void select_instance(size_t instance_ix);

    void add(std::string name);
    void remove(size_t scene_ix);

    void add_instance(engine::models::gid model);
    void remove_instance(size_t instance_ix);

    CameraInfo camera();
    void update_camera(CameraInfo& cam);

    CameraInfo& get_camera_infos(size_t scene_ix);
}
