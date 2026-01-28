#pragma once

#include <vector>
#include <array>

#include "goliath/collisions.hpp"
#include <glm/ext/vector_float3.hpp>

namespace engine::lights {
    enum struct Attenuation {
        Constant = 0,
        CartesianDistance = 1 << 0,
    };

    struct Light {
        struct HalfPlane {

        };

        glm::vec3 initial_intensity;
        Attenuation attenuation_function;

        std::array<HalfPlane, 12> half_planes;
    };

    // std::vector<collisions::Sphere> bb_spheres{};
    // std::vector<Light> lights{};
}
