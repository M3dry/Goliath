#pragma once

#include "models.hpp"
#include <filesystem>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <vector>

namespace scene {
    struct Instance {
        models::gid model_gid;
        std::string name;

        glm::vec3 translate{0.0f};
        glm::vec3 rotate{0.0f};
        glm::vec3 scale{1.0f};

        void update_transform(glm::mat4& transform) const;
    };

    struct Scene {
        std::string name;

        std::vector<models::gid> used_models;
        std::vector<std::vector<size_t>> instances_of_used_models;

        std::vector<Instance> instances{};
        size_t selected_instance = -1;
        uint32_t ref_count = 0;

        void acquire();
        void release();

        void destroy();

        void add_model(models::gid gid);
        void add_instance(Instance instance);

        void remove_instance(size_t ix);
    };

    void load(std::filesystem::path scenes_json, bool* parser_error);
    void save(std::filesystem::path scenes_json);

    void destroy();

    void emplace_scene(std::string name);
    // false - couldn't remove scene since it's the only scene
    bool remove_scene(size_t ix);
    void move_to(size_t ix, size_t dest);

    Scene& selected_scene();
    uint32_t selected_scene_ix();
    void select_scene(uint32_t ix);

    std::span<Scene> get_scenes();
}
