#pragma once

#include "goliath/models.hpp"
#include "goliath/transport2.hpp"
#include <nlohmann/json.hpp>

namespace engine::scenes {
    void init();
    void destroy();

    void load(const nlohmann::json& j);
    nlohmann::json save();

    void acquire(size_t i);
    void release(size_t i);

    void add_instance(size_t scene_ix, std::string name, glm::mat4 transform, models::gid model);
    void remove_instance(size_t scene_ix, size_t instance_ix);

    void add(std::string name);
    void remove(size_t scene_ix);
    void move_to(size_t scene_ix, size_t dest_ix);

    std::string& get_name(size_t scene_ix);
    std::span<std::string> get_instance_names(size_t scene_ix);
    std::span<models::gid> get_instance_models(size_t scene_ix);
    std::span<glm::mat4> get_instance_transforms(size_t scene_ix);
    Buffer get_instance_transforms_buffer(size_t scene_ix, transport2::ticket& ticket);
    std::span<models::gid> get_used_models(size_t scene_ix);

    std::span<std::string> get_names();

    bool want_to_save();
    void modified(size_t scene_ix);
    void update_transforms_buffer(size_t scene_ix);

    nlohmann::json default_json();

}
