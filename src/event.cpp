#include "goliath/event.hpp"
#include "goliath/engine.hpp"
#include "goliath/imgui.hpp"
#include "imgui_impl_glfw.h"

#include <GLFW/glfw3.h>
#include <bitset>

namespace engine::event {
    std::bitset<GLFW_KEY_LAST> down;
    std::bitset<GLFW_KEY_LAST> up;
    glm::vec2 mouse_delta{0.0f};
    glm::vec2 mouse_absolute{0.0f};
}

#define WRAP_IMGUI_CALLBACK(x)                                                                                         \
    do {                                                                                                               \
        bool imgui_state = engine::imgui::enabled();                                                                   \
        if (!imgui_state) break;                                                                                       \
        x;                                                                                                             \
        ImGuiIO& io = ImGui::GetIO();                                                                                  \
        if (io.WantCaptureKeyboard || io.WantCaptureMouse) return;                                                     \
    } while (0)

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && key == GLFW_KEY_L) {
    } else {
        WRAP_IMGUI_CALLBACK(ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods));
    }

    engine::event::down[(uint32_t)key] = action == GLFW_PRESS || action == GLFW_REPEAT;
    engine::event::up[(uint32_t)key] = action == GLFW_RELEASE;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    WRAP_IMGUI_CALLBACK(ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods));
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    WRAP_IMGUI_CALLBACK(ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos));

    static double last_x = 0;
    static double last_y = 0;

    engine::event::mouse_delta = glm::vec2{xpos - last_x, ypos - last_y};
    engine::event::mouse_absolute = glm::vec2{xpos, ypos};

    last_x = xpos;
    last_y = ypos;
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

namespace engine::event {
    void register_glfw_callbacks() {
        ImGui_ImplGlfw_SetCallbacksChainForAllWindows(true);

        glfwSetKeyCallback(window, key_callback);
        glfwSetMouseButtonCallback(window, mouse_button_callback);
        glfwSetCursorPosCallback(window, cursor_position_callback);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetCharCallback(window, char_callback);
        glfwSetWindowCloseCallback(window, window_close_callback);
    }

    PollEvent poll() {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE) return Minimized;
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) return Minimized;

        return Normal;
    }

    bool is_held(uint32_t code) {
        return down[code];
    }

    bool was_released(uint32_t code) {
        return up[static_cast<std::size_t>(code)];
    }

    glm::vec2 get_mouse_delta() {
        return mouse_delta;
    }

    glm::vec2 get_mouse_aboslute() {
        return mouse_absolute;
    }

    void update_tick() {
        up.reset();
        mouse_delta = glm::vec2{0.0f};
    }
}
