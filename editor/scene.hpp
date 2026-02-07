#pragma once

#include <cstddef>

namespace scene {
    extern size_t selected_instance;

    void load();
    void destroy();

    size_t selected_scene();
    void select_scene(size_t scene_ix);
}
