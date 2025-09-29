#pragma once

#include <glm/ext/vector_float2.hpp>

namespace engine::event {
    enum PollEvent {
        Normal,
        Minimized,
    };

    PollEvent poll();

    bool is_held(uint32_t code);
    bool was_released(uint32_t code);

    glm::vec2 get_mouse_delta();
    glm::vec2 get_mouse_absolute();

    void update_tick();
}
