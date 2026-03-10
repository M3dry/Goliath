#include "layout.hpp"
#include "imgui.h"

namespace layouts {
    const char default_layout[] = {
#embed "default_layout.ini"
    };

    void set_to_default() {
        ImGui::LoadIniSettingsFromMemory(default_layout, sizeof(default_layout));
    }
}
