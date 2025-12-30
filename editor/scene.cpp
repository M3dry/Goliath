#include "scene.hpp"
#include "nlohmann/json_fwd.hpp"
#include "project.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace glm {
    void to_json(nlohmann::json& j, const vec3& v) {
        j = nlohmann::json{
            v.x,
            v.y,
            v.z,
        };
    }

    void from_json(const nlohmann::json& j, vec3& v) {
        j.at(0).get_to(v.x);
        j.at(1).get_to(v.y);
        j.at(2).get_to(v.z);
    }
}

namespace scene {
    void to_json(nlohmann::json& j, const Instance& inst) {
        j = nlohmann::json{
            {"model_gid", inst.model_gid}, {"name", inst.name},   {"translate", inst.translate},
            {"rotate", inst.rotate},       {"scale", inst.scale},
        };
    }

    void from_json(const nlohmann::json& j, Instance& inst) {
        j.at("model_gid").get_to(inst.model_gid);
        j.at("name").get_to(inst.name);
        j.at("translate").get_to(inst.translate);
        j.at("rotate").get_to(inst.rotate);
        j.at("scale").get_to(inst.scale);
    }

    void to_json(nlohmann::json& j, const Scene& scene) {
        j = nlohmann::json{
            {"name", scene.name},
            {"used_models", scene.used_models},
            {"instances", scene.instances},
            {"selected_instance", scene.selected_instance},
        };
    }

    void from_json(const nlohmann::json& j, Scene& scene) {
        j.at("name").get_to(scene.name);
        j.at("used_models").get_to(scene.used_models);
        j.at("instances").get_to(scene.instances);
        j.at("selected_instance").get_to(scene.selected_instance);
    }

    void Instance::update_transform(glm::mat4& transform) const {
        transform =
            glm::translate(glm::identity<glm::mat4>(), translate) *
            glm::rotate(glm::rotate(glm::rotate(glm::identity<glm::mat4>(), glm::radians(rotate.x), glm::vec3{0, 1, 0}),
                                    glm::radians(rotate.y), glm::vec3{1, 0, 0}),
                        glm::radians(rotate.z), glm::vec3{0, 0, 1}) *
            glm::scale(glm::identity<glm::mat4>(), scale);
    }

    void Scene::acquire() {
        models::acquire(used_models.data(), used_models.size());
        ref_count++;

        if (instances_of_used_models.size() != used_models.size()) {
            instances_of_used_models.resize(used_models.size());

            for (size_t inst_ix = 0; inst_ix < instances.size(); inst_ix++) {
                auto it = std::find(used_models.begin(), used_models.end(), instances[inst_ix].model_gid);
                assert(it != used_models.end() && "Model must be in the `used_models` field of the scene");

                auto ix = std::distance(used_models.begin(), it);
                instances_of_used_models[ix].emplace_back(inst_ix);
            }
        }
    }

    void Scene::release() {
        models::release(used_models.data(), used_models.size());
        ref_count--;
    }

    void Scene::destroy() {
        while (ref_count-- != 0) {
            release();
        }
    }

    void Scene::add_model(models::gid gid) {
        for (const auto used_gid : used_models) {
            if (gid == used_gid) {
                return;
            }
        }
        used_models.emplace_back(gid);
        instances_of_used_models.emplace_back();

        auto rc = ref_count;
        while (rc-- != 0) {
            models::acquire(&gid, 1);
        }
    }

    void Scene::add_instance(Instance instance) {
        bool model_exists = false;
        for (size_t i = 0; i < used_models.size(); i++) {
            if (instance.model_gid == used_models[i]) {
                instances_of_used_models[i].emplace_back(instances.size());
                model_exists = true;
                break;
            }
        }

        if (!model_exists) {
            add_model(instance.model_gid);
            instances_of_used_models.back().emplace_back(instances.size());
        }

        instances.emplace_back(instance);
        selected_instance = instances.size() - 1;

        save(project::scenes_file);
    }

    void Scene::remove_instance(size_t ix) {
        if (selected_instance == ix) selected_instance = -1;
        instances.erase(instances.begin() + ix);

        for (auto& insts : instances_of_used_models) {
            std::erase_if(insts, [&](auto& inst_ix) {
                if (inst_ix > ix) {
                    inst_ix--;
                    return false;
                } else {
                    return inst_ix == ix;
                }
            });
        }

        save(project::scenes_file);
    }

    uint32_t selected_scene_ = 0;
    std::vector<Scene> scenes{};

    void load(std::filesystem::path scenes_json, bool* parse_error) {
        *parse_error = false;

        std::ifstream i{scenes_json};
        if (!i) {
            selected_scene_ = 0;
            scenes = {Scene{"Default"}};

            return;
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(i);
            if (parse_error) *parse_error = false;
        } catch (const nlohmann::json::parse_error& e) {
            if (parse_error) *parse_error = true;

            selected_scene_ = 0;
            scenes = {Scene{"Default"}};

            return;
        }

        selected_scene_ = j.value("selected_scene", 0);
        scenes = j.value<std::vector<Scene>>("scenes", {Scene{"Default"}});
    }

    void save(std::filesystem::path scenes_json) {
        printf("saving scene\n");
        std::ofstream o{scenes_json};

        o << nlohmann::json{
            {"selected_instance", selected_scene_},
            {"scenes", scenes},
        };
    }

    void destroy() {
        for (auto& scene : scenes) {
            scene.destroy();
        }
    }

    void emplace_scene(std::string name) {
        scenes.emplace_back(name);

        save(project::scenes_file);
    }

    // false - couldn't remove scene since it's the only scene
    bool remove_scene(size_t ix) {
        if (scenes.size() <= 1) return false;
        scenes[ix].destroy();
        scenes.erase(scenes.begin() + ix);

        save(project::scenes_file);
        return true;
    }

    void move_to(size_t ix, size_t dest) {
        auto scene = scenes[ix];
        scenes.erase(scenes.begin() + ix);
        scenes.insert(scenes.begin() + dest, scene);

        save(project::scenes_file);
    }

    Scene& selected_scene() {
        return scenes[selected_scene_];
    }

    uint32_t selected_scene_ix() {
        return selected_scene_;
    }

    void select_scene(uint32_t ix) {
        selected_scene().release();
        selected_scene_ = ix;
        selected_scene().acquire();

        save(project::scenes_file);
    }

    std::span<Scene> get_scenes() {
        return scenes;
    }
}
