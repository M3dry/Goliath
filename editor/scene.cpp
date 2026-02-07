#include "scene.hpp"
#include "goliath/scenes.hpp"
#include "goliath/util.hpp"
#include "project.hpp"

namespace scene {
    size_t selected_scene_ix = 0;
    size_t selected_instance = -1;

    void load() {
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

        if (engine::scenes::get_names().size() == 0) {
            engine::scenes::add("Default");
        }

        engine::scenes::acquire(selected_scene_ix);
    }

    void destroy() {
        engine::scenes::destroy();
    }

    size_t selected_scene() {
        return selected_scene_ix;
    }

    void select_scene(size_t scene_ix) {
        engine::scenes::release(selected_scene_ix);
        selected_scene_ix = scene_ix;
        engine::scenes::acquire(selected_scene_ix);
    }
}
