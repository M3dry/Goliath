#include "scene.hpp"
#include "goliath/scenes.hpp"
#include "goliath/util.hpp"
#include "project.hpp"

namespace scene {
    bool want_save = false;
    size_t selected_scene_ix = 0;
    std::vector<size_t> selected_instances{};
    std::vector<CameraInfo> camera_infos{};

    void to_json(nlohmann::json& j, const CameraInfo& info) {
        j = nlohmann::json{
            {"cam", info.cam},
            {"fov", info.fov},
            {"sensitivity", info.sensitivity},
            {"movement_speed", info.movement_speed},
        };
    }

    void from_json(const nlohmann::json& j, CameraInfo& info) {
        j["cam"].get_to(info.cam);
        j["fov"].get_to(info.fov);
        j["sensitivity"].get_to(info.sensitivity);
        j["movement_speed"].get_to(info.movement_speed);
    }

    void load(nlohmann::json j) {
        engine::scenes::init();

        auto scenes_json = engine::util::read_json(project::scenes_file);
        if (!scenes_json.has_value() && scenes_json.error() == engine::util::ReadJsonErr::FileErr &&
            !std::filesystem::exists(project::scenes_file)) {
            scenes_json = engine::scenes::default_json();
        } else if (!scenes_json.has_value()) {
            printf("Scenes file is corrupted\n");
            exit(0);
        }

        engine::scenes::load(*scenes_json);
        selected_scene_ix = j["selected_scene"];
        j["selected_instances"].get_to(selected_instances);
        camera_infos = j["camera_infos"];

        if (engine::scenes::get_names().size() == 0) {
            add("Default");
        }

        engine::scenes::acquire(selected_scene_ix);
    }

    nlohmann::json save() {
        return nlohmann::json{
            {"selected_scene", selected_scene()},
            {"selected_instances", selected_instances},
            {"camera_infos", camera_infos},
        };
    }

    nlohmann::json default_json() {
        return nlohmann::json{
            {"selected_scene", 0},
            {"selected_instances", nlohmann::json::array()},
            {"camera_infos", nlohmann::json::array()},
        };
    }

    bool want_to_save() {
        auto res = want_save;
        want_save = false;
        return res;
    }
    void modified() {
        want_save = true;
    }

    void destroy() {
        engine::scenes::destroy();
    }

    size_t selected_scene() {
        return selected_scene_ix;
    }

    void select_scene(size_t scene_ix) {
        if (scene_ix == selected_scene_ix) return;

        engine::scenes::release(selected_scene_ix);
        selected_scene_ix = scene_ix;
        engine::scenes::acquire(selected_scene_ix);

        want_save = true;
    }

    size_t selected_instance() {
        return selected_instances[selected_scene()];
    }

    void select_instance(size_t instance_ix) {
        selected_instances[selected_scene()] = instance_ix;
        want_save = true;
    }

    void add(std::string name) {
        engine::scenes::add(name);
        select_scene(engine::scenes::get_names().size() - 1);
        selected_instances.emplace_back(-1);
        camera_infos.emplace_back();

        want_save = true;
    }

    void remove(size_t scene_ix) {
        if (engine::scenes::get_names().size() <= 1) return;

        engine::scenes::remove(scene_ix);
        selected_instances.erase(selected_instances.begin() + scene_ix);
        if (scene::selected_scene() == scene_ix) {
            scene::select_scene(scene_ix == 0 ? 0 : scene_ix - 1);
        }

        want_save = true;
    }

    void add_instance(engine::models::gid model) {
        engine::scenes::add_instance(scene::selected_scene(), **engine::models::get_name(model),
                                     glm::identity<glm::mat4>(), model);
        scene::select_instance(engine::scenes::get_instance_names(scene::selected_scene()).size() - 1);

        want_save = true;
    }

    void remove_instance(size_t instance_ix) {
        engine::scenes::remove_instance(selected_scene(), instance_ix);
        if (scene::selected_instance() == -1) {

        } else if (scene::selected_instance() == instance_ix) {
            scene::select_instance(-1);
        } else if (scene::selected_instance() > instance_ix) {
            scene::select_instance(scene::selected_instance() - 1);
        }

        want_save = true;
    }

    CameraInfo camera() {
        return camera_infos[selected_scene()];
    }

    void update_camera(CameraInfo& cam) {
        want_save = true;
        camera_infos[selected_scene()] = cam;
    }

    CameraInfo& get_camera_infos(size_t scene_ix) {
        return camera_infos[scene_ix];
    }
}
