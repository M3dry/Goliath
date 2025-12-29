#pragma once

#include <filesystem>

namespace engine::textures {
    void init(uint32_t init_texture_capacity, std::filesystem::path texture_directory);
    void destroy();

    void process_uploads();

    void rebuild_pool();
}
