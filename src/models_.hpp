#pragma once

#include <filesystem>

namespace engine::models {
    void init(std::filesystem::path models_directory);
    void destroy();

    bool process_uploads();
}
