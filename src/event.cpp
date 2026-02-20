#include "goliath/event.hpp"
#include "engine_.hpp"
#include "imgui_impl_glfw.h"

#include <GLFW/glfw3.h>

namespace engine::event {
    glm::vec2 mouse_delta{0.0f};
    glm::vec2 mouse_absolute{0.0f};
}

#define WRAP_IMGUI_CALLBACK(x)                                                                                         \
    do {                                                                                                               \
        x;                                                                                                             \
        ImGuiIO& io = ImGui::GetIO();                                                                                  \
        if (io.WantCaptureKeyboard || io.WantCaptureMouse) return;                                                     \
    } while (0)

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    WRAP_IMGUI_CALLBACK(ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods));
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    WRAP_IMGUI_CALLBACK(ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods));
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

    engine::event::mouse_delta = glm::vec2{
        xpos - engine::event::mouse_absolute.x,
        ypos - engine::event::mouse_absolute.y,
    };
    engine::event::mouse_absolute = glm::vec2{xpos, ypos};
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    WRAP_IMGUI_CALLBACK(ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset));
}

void char_callback(GLFWwindow* window, uint32_t codepoint) {
    WRAP_IMGUI_CALLBACK(ImGui_ImplGlfw_CharCallback(window, codepoint));
}

void window_close_callback(GLFWwindow* window) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void window_size_callback(GLFWwindow* window, int width, int height) {
    engine::new_window_size(width, height);
}

namespace engine::event {
    void register_glfw_callbacks() {
        ImGui_ImplGlfw_SetCallbacksChainForAllWindows(true);

        glfwSetKeyCallback(state->window, key_callback);
        glfwSetMouseButtonCallback(state->window, mouse_button_callback);
        glfwSetCursorPosCallback(state->window, cursor_position_callback);
        glfwSetScrollCallback(state->window, scroll_callback);
        glfwSetCharCallback(state->window, char_callback);
        glfwSetWindowCloseCallback(state->window, window_close_callback);
        glfwSetWindowSizeCallback(state->window, window_size_callback);
    }

    PollEvent poll() {
        glfwPollEvents();
        if (glfwGetWindowAttrib(state->window, GLFW_ICONIFIED) == GLFW_TRUE) return Minimized;
        int width, height;
        glfwGetFramebufferSize(state->window, &width, &height);
        if (width == 0 || height == 0) return Minimized;

        return Normal;
    }

    bool is_held(ImGuiKey code) {
        return ImGui::IsKeyDown(code);
    }

    bool was_released(ImGuiKey code) {
        return ImGui::IsKeyReleased(code);
    }

    glm::vec2 get_mouse_delta() {
        return mouse_delta;
    }

    glm::vec2 get_mouse_absolute() {
        return mouse_absolute;
    }

    void update_tick() {
        mouse_delta = glm::vec2{0.0f};
    }
}
