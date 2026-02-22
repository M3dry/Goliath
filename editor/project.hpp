#pragma once

#include <filesystem>

namespace project {
    extern std::filesystem::path project_root;
    extern std::filesystem::path materials;
    extern std::filesystem::path models_directory;
    extern std::filesystem::path models_registry;
    extern std::filesystem::path textures_directory;
    extern std::filesystem::path textures_registry;
    extern std::filesystem::path scenes_file;
    extern std::filesystem::path editor_state;
    extern std::filesystem::path asset_inputs;

    bool find_project();

    void init();
}
