#pragma once

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <numbers>

namespace engine::camera {
    struct Perspective {
        float fov = std::numbers::pi_v<float>/2.0f;
        float aspect_ratio = 16.0f / 9.0f;
    };

    struct Orthographic {
        float frustum_width = 1.0f;
        float frustum_height = 1.0f;
    };
}

namespace engine {
    class Camera {
      public:
        glm::vec3 position{0.0f};

        Camera();

        glm::vec3 forward();
        glm::vec3 right();
        glm::vec3 up();

        glm::mat4 view();
        glm::mat4 projection();
        glm::mat4 view_projection();

        void set_projection(camera::Perspective persp, float near = 0.1f, float far = 100.0f);
        void set_projection(camera::Orthographic ortho, float near = 0.1f, float far = 100.0f);

        void look_at(glm::vec3 target, glm::vec3 up = {0.0f, 1.0f, 0.0f});
        void rotate(float yaw_delta, float pitch_delta);

        void update_matrices();
      private:
        glm::quat _orientation;
        glm::mat4 _projection;
        glm::mat4 _view;
        glm::mat4 _view_projection;

    };
}
