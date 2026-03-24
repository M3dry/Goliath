#include "goliath/scenes.hpp"
#include "goliath/models.hpp"
#include "goliath/transport2.hpp"
#include "goliath/util.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan_core.h>

namespace engine::scenes {
    struct Scene {
        std::vector<models::gid> used_models;
        std::vector<std::vector<size_t>> instances_of_used_models;

        std::vector<models::gid> instance_models{};
        std::vector<glm::mat4> instance_transforms{};

        transport2::ticket instance_transforms_buffer_ticket;
        Buffer instance_transforms_buffer;
    };

    void to_json(nlohmann::json& j, const Scene& scene) {
        j = nlohmann::json{
            {"used_models", scene.used_models},
            {"instances_of_used_models", scene.instances_of_used_models},
            {"instance_models", scene.instance_models},
            {"instance_transforms", scene.instance_transforms},
        };
    }

    void from_json(const nlohmann::json& j, Scene& scene) {
        j["used_models"].get_to(scene.used_models);
        j["instances_of_used_models"].get_to(scene.instances_of_used_models);
        j["instance_models"].get_to(scene.instance_models);
        j["instance_transforms"].get_to(scene.instance_transforms);
    }

    void to_json(nlohmann::json& j, const Light& light) {
        j = nlohmann::json{
            {"position", light.position},
            {"intensity", light.intensity},
        };
    }

    void from_json(const nlohmann::json& j, Light& scene) {
        j["position"].get_to(scene.position);
        j["intensity"].get_to(scene.intensity);
    }

    bool want_save = false;

    std::vector<std::string> scene_names{};
    std::vector<std::vector<std::string>> instance_namess{};
    std::vector<std::vector<std::string>> lights_names{};

    std::vector<uint32_t> scene_ref_counts{};
    std::vector<Scene> scenes{};

    std::vector<std::vector<Light>> lights{};
    std::vector<std::pair<Buffer, transport2::ticket>> light_buffers{};

    size_t add_model(size_t scene_ix, models::gid gid) {
        auto& scene = scenes[scene_ix];
        for (size_t i = 0; i < scene.used_models.size(); i++) {
            if (scene.used_models[i] == gid) return i;
        }

        scene.used_models.emplace_back(gid);
        scene.instances_of_used_models.emplace_back();

        auto rc = scene_ref_counts[scene_ix];
        while (rc-- != 0) {
            engine::models::acquire(&gid, 1);
        }

        return scene.used_models.size() - 1;
    }

    void destroy(size_t scene_ix) {
        const auto& used_models = scenes[scene_ix].used_models;
        auto rc = scene_ref_counts[scene_ix];

        transport2::unqueue(scenes[scene_ix].instance_transforms_buffer_ticket);
        scenes[scene_ix].instance_transforms_buffer.destroy();
        scenes[scene_ix].instance_transforms_buffer = Buffer{};
        light_buffers[scene_ix].first.destroy();
        light_buffers[scene_ix].first = Buffer{};

        while (rc-- != 0) {
            engine::models::release(used_models.data(), used_models.size());
        }
    }

    void init() {}

    void destroy() {
        for (size_t i = 0; i < scenes.size(); i++) {
            destroy(i);
        }
    }

    void load(const nlohmann::json& j) {
        j["names"].get_to(scene_names);
        j["instance_names"].get_to(instance_namess);
        j["scenes"].get_to(scenes);
        j["lights"].get_to(lights);

        light_buffers.resize(lights.size());
        scene_ref_counts.resize(scenes.size(), 0);
    }

    nlohmann::json save() {
        return nlohmann::json{
            {"names", scene_names},
            {"instance_names", instance_namess},
            {"scenes", scenes},
            {"lights", lights},
        };
    }

    void acquire(size_t scene_ix) {
        scene_ref_counts[scene_ix]++;

        update_buffers(scene_ix);

        auto& scene = scenes[scene_ix];
        engine::models::acquire(scene.used_models.data(), scene.used_models.size());
    }

    void release(size_t scene_ix) {
        if (scene_ref_counts[scene_ix] == 0) return;
        scene_ref_counts[scene_ix]--;

        auto& scene = scenes[scene_ix];
        scene.instance_transforms_buffer.destroy();
        scene.instance_transforms_buffer = Buffer{};
        scene.instance_transforms_buffer_ticket = {};
        light_buffers[scene_ix].first.destroy();
        light_buffers[scene_ix].first = Buffer{};
        light_buffers[scene_ix].second = {};
        engine::models::release(scene.used_models.data(), scene.used_models.size());
    }

    void add_instance(size_t scene_ix, std::string name, glm::mat4 transform, models::gid model) {
        auto model_ix = add_model(scene_ix, model);

        auto& scene = scenes[scene_ix];

        scene.instances_of_used_models[model_ix].emplace_back(scene.instance_models.size());
        scene.instance_models.emplace_back(model);
        scene.instance_transforms.emplace_back(transform);
        instance_namess[scene_ix].emplace_back(name);

        update_buffers(scene_ix);
        want_save = true;
    }

    void remove_instance(size_t scene_ix, size_t instance_ix) {
        auto& scene = scenes[scene_ix];

        auto inst_model_gid = scene.instance_models[instance_ix];

        scene.instance_transforms.erase(scene.instance_transforms.begin() + instance_ix);
        scene.instance_models.erase(scene.instance_models.begin() + instance_ix);
        instance_namess[scene_ix].erase(instance_namess[scene_ix].begin() + instance_ix);

        for (auto& insts : scene.instances_of_used_models) {
            std::erase_if(insts, [&](auto& inst_ix) {
                if (inst_ix > instance_ix) {
                    inst_ix--;
                    return false;
                } else {
                    return inst_ix == instance_ix;
                }
            });
        }

        if (auto model_it = std::find(scene.used_models.begin(), scene.used_models.end(), inst_model_gid);
            model_it != scene.used_models.end()) {
            auto model_ix = std::distance(scene.used_models.begin(), model_it);

            if (scene.instances_of_used_models[model_ix].size() == 0) {
                scene.used_models.erase(scene.used_models.begin() + model_ix);
                scene.instances_of_used_models.erase(scene.instances_of_used_models.begin() + model_ix);
            }
        }

        update_buffers(scene_ix);
        want_save = true;
    }

    void remove_all_instances_of_model(size_t scene_ix, models::gid model, size_t& selected_instance) {
        auto& scene = scenes[scene_ix];

        auto it = std::find(scene.used_models.begin(), scene.used_models.end(), model);
        if (it == scene.used_models.end()) return;
        auto model_ix = std::distance(scene.used_models.begin(), it);

        while (scene.instances_of_used_models[model_ix].size() != 0) {
            printf("hello\n");
            auto instance_ix = scene.instances_of_used_models[model_ix][0];
            remove_instance(scene_ix, instance_ix);

            if (instance_ix < selected_instance) {
                selected_instance--;
            } else if (instance_ix == selected_instance) {
                selected_instance = instance_ix == 0 ? selected_instance + 1 : selected_instance - 1;
                if (selected_instance >= scene.instance_transforms.size()) {
                    selected_instance = -1;
                }
            }
        }
    }

    void add(std::string name) {
        scene_names.emplace_back(name);
        instance_namess.emplace_back();

        scene_ref_counts.emplace_back(0);
        scenes.emplace_back();

        lights.emplace_back();
        light_buffers.emplace_back();

        want_save = true;
    }

    void remove(size_t scene_ix) {
        if (scenes.size() <= scene_ix) return;
        destroy(scene_ix);

        scene_names.erase(scene_names.begin() + scene_ix);
        instance_namess.erase(instance_namess.begin() + scene_ix);

        scene_ref_counts.erase(scene_ref_counts.begin() + scene_ix);
        scenes.erase(scenes.begin() + scene_ix);

        lights.erase(lights.begin() + scene_ix);
        light_buffers.erase(light_buffers.begin() + scene_ix);

        want_save = true;
    }

    uint32_t add_light(uint32_t scene_ix, Light light) {
        lights[scene_ix].emplace_back(light);
        update_buffers(scene_ix);

        return lights[scene_ix].size() - 1;
    }

    void remove_light(uint32_t scene_ix, uint32_t light_ix) {
        auto& ls = lights[scene_ix];
        ls.erase(ls.begin() + light_ix);

        update_buffers(scene_ix);
    }

    Light& get_light(uint32_t scene_ix, uint32_t light_ix) {
        return lights[scene_ix][light_ix];
    }

    std::string& get_name(size_t scene_ix) {
        return scene_names[scene_ix];
    }

    std::span<std::string> get_instance_names(size_t scene_ix) {
        return instance_namess[scene_ix];
    }

    std::span<const models::gid> get_instance_models(size_t scene_ix) {
        return scenes[scene_ix].instance_models;
    }

    std::span<glm::mat4> get_instance_transforms(size_t scene_ix) {
        return scenes[scene_ix].instance_transforms;
    }

    Buffer get_instance_transforms_buffer(size_t scene_ix, transport2::ticket& ticket) {
        ticket = scenes[scene_ix].instance_transforms_buffer_ticket;
        return scenes[scene_ix].instance_transforms_buffer;
    }

    std::span<const models::gid> get_used_models(size_t scene_ix) {
        return scenes[scene_ix].used_models;
    }

    std::span<std::string> get_names() {
        return scene_names;
    }

    bool want_to_save() {
        auto res = want_save;
        want_save = false;
        return res;
    }

    void modified(size_t scene_ix) {
        want_save = true;
    }

    void update_buffers(size_t scene_ix) {
        auto& scene = scenes[scene_ix];
        if (scene.instance_transforms.empty()) {
            if (scene.instance_transforms_buffer != Buffer{}) scene.instance_transforms_buffer.destroy();

            scene.instance_transforms_buffer = {};
            scene.instance_transforms_buffer_ticket = {};
            return;
        }

        scene.instance_transforms_buffer.destroy();
        scene.instance_transforms_buffer =
            Buffer::create(std::format("Scene `{}`'s transforms buffer", scene_names[scene_ix]).c_str(),
                           scene.instance_transforms.size() * sizeof(glm::mat4),
                           VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);

        scene.instance_transforms_buffer_ticket =
            transport2::upload(true, scene.instance_transforms.data(), std::nullopt,
                               scene.instance_transforms_buffer.size(), scene.instance_transforms_buffer, 0,
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

        auto& light_buf = light_buffers[scene_ix];

        if (lights[scene_ix].empty()) {
            if (light_buf.first != Buffer{}) light_buf.first.destroy();

            light_buf.first= {};
            light_buf.second = {};
            return;
        }

        light_buf.first.destroy();
        light_buf.first =
            Buffer::create(std::format("Scene `{}`'s lights buffer", scene_names[scene_ix]).c_str(),
                           lights[scene_ix].size() * sizeof(Light),
                           VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);
        light_buf.second =
            transport2::upload(true, lights[scene_ix].data(), std::nullopt, light_buf.first.size(), light_buf.first, 0,
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }

    nlohmann::json default_json() {
        return nlohmann::json{
            {"names", nlohmann::json::array()},
            {"instance_names", nlohmann::json::array()},
            {"scenes", nlohmann::json::array()},
            {"lights", nlohmann::json::array()},
        };
    }

    Iterator draw(size_t scene_ix, transport2::ticket& t, uint64_t& transforms_addr) {
        transforms_addr = get_instance_transforms_buffer(scene_ix, t).address();
        return Iterator{scene_ix};
    }
}
