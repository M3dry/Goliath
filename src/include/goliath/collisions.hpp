#pragma once

#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>

namespace engine::collisions {
    struct AABB {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};

        void extend(AABB other) {
            if (min == glm::vec3{0.0f} && max == glm::vec3{0.0f}) {
                min = other.min;
                max = other.max;
            } else {
                min = glm::min(min, other.min);
                max = glm::max(max, other.max);
            }
        }
    };
}
