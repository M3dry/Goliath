#include "error_stack.hpp"
#include "imgui.h"

#include <deque>
#include <mutex>

namespace error_stack {
    std::mutex mutex{};
    std::deque<Fn> fns;

    void push(Fn&& f) {
        std::lock_guard lock{mutex};

        fns.emplace_back(f);
    }

    void draw_top() {
        std::lock_guard lock{mutex};

        if (fns.empty()) return;

        ImGui::OpenPopup("Error");
        if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (fns.front()()) {
                fns.pop_front();
            }
            ImGui::EndPopup();
        }
    }
}
