#pragma once

#include "goliath/models.hpp"
#include "goliath/transport2.hpp"
#include <nlohmann/json.hpp>

namespace engine::scenes {
    void init();
    void destroy();

    void load(const nlohmann::json& j);
    nlohmann::json save();

    void acquire(size_t scene_ix);
    void release(size_t scene_ix);

    void add_instance(size_t scene_ix, std::string name, glm::mat4 transform, models::gid model);
    void remove_instance(size_t scene_ix, size_t instance_ix);

    void add(std::string name);
    void remove(size_t scene_ix);
    void move_to(size_t scene_ix, size_t dest_ix);

    std::string& get_name(size_t scene_ix);
    std::span<std::string> get_instance_names(size_t scene_ix);
    std::span<const models::gid> get_instance_models(size_t scene_ix);
    std::span<glm::mat4> get_instance_transforms(size_t scene_ix);
    Buffer get_instance_transforms_buffer(size_t scene_ix, transport2::ticket& ticket);
    std::span<const models::gid> get_used_models(size_t scene_ix);

    std::span<std::string> get_names();

    bool want_to_save();
    void modified(size_t scene_ix);
    void update_transforms_buffer(size_t scene_ix);

    nlohmann::json default_json();

    template <typename F> inline transport2::ticket draw(size_t scene_ix, F&& f) {
        transport2::ticket ticket;

        auto transforms = get_instance_transforms_buffer(scene_ix, ticket);
        auto instance_models = get_instance_models(scene_ix);
        for (auto i = 0; i < instance_models.size(); i++) {
            auto mgid = instance_models[i];
            if (auto state = engine::models::is_loaded(mgid); !state || *state != engine::models::LoadState::OnGPU)
                continue;

            f(mgid, transforms.address(), i);
        }

        return ticket;
    }

    struct Draw {
        size_t transform_ix;
        models::gid gid;
    };

    class Iterator {
      private:
        size_t i = 0;
        std::span<const models::gid> models;

        friend Iterator draw(size_t scene_ix, transport2::ticket& t, uint64_t& transforms_addr);

        Iterator(size_t scene_ix) {
            models = get_instance_models(scene_ix);
        }
      public:
        Draw next() {
            if (models.size() <= i) return {(uint64_t)-1, models::gid{}};
            return Draw{i, models[i++]};
        }
    };

    Iterator draw(size_t scene_ix, transport2::ticket& t, uint64_t& transforms_addr);
}
