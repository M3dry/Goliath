#include "goliath/camera.hpp"
#include <glm/gtc/quaternion.hpp>

namespace engine {
    Camera::Camera() {
        look_at(glm::vec3{0.0f});
        set_projection(camera::Perspective{}, 0.1f, 100.0f);
    }

    glm::vec3 Camera::forward() {
        return _orientation * glm::vec3{0.0f, 0.0f, -1.0f};
    }

    glm::vec3 Camera::right() {
        return _orientation * glm::vec3{1.0f, 0.0f, 0.0f};
    }

    glm::vec3 Camera::up() {
        return _orientation * glm::vec3{0.0f, 1.0f, 0.0f};
    }

    glm::mat4 Camera::view() {
        return _view;
    }

    glm::mat4 Camera::projection() {
        return _projection;
    }

    glm::mat4 Camera::view_projection() {
        return _view_projection;
    }

    void Camera::set_projection(camera::Perspective persp, float near, float far) {
        _projection = glm::perspective(persp.fov, persp.aspect_ratio, near, far);
    }

    void Camera::set_projection(camera::Orthographic ortho, float near, float far) {
        _projection = glm::ortho(-ortho.frustum_width / 2.0f, ortho.frustum_width / 2.0f, -ortho.frustum_height / 2.0f,
                                 ortho.frustum_height / 2.0f, near, far);
    }

    void Camera::look_at(glm::vec3 target, glm::vec3 up) {
        _orientation = glm::quatLookAt(glm::normalize(target - position), up);
    }

    void Camera::rotate(float yaw_delta, float pitch_delta) {
        glm::quat yaw_rot = glm::angleAxis(yaw_delta, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat pitch_rot = glm::angleAxis(pitch_delta, right());

        _orientation = glm::normalize(yaw_rot * pitch_rot * _orientation);
    }

    void Camera::update_matrices() {
        _view = glm::lookAt(position, position + forward(), up());
        _view_projection = _projection * _view;
    }
}
