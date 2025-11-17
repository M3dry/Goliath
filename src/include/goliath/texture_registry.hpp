#pragma once

#include "goliath/texture.hpp"

#include <cstdint>
#include <filesystem>

namespace engine::texture_registry {
    void init();
    void destroy();

    void load(uint8_t* file_data, uint32_t file_size, bool load_names);

    uint32_t add(std::filesystem::path path, std::string name, const Sampler& sampler);
    // makes a copy of `data`, no ownership assumed
    uint32_t add(uint8_t* data, uint32_t data_size, std::string name, const Sampler& sampler);
    void remove(uint32_t gid);

    std::string& get_name(uint32_t gid);

    // null if texture @gid is embedded data
    const std::filesystem::path* get_path(uint32_t gid);
    // .data() of span is nullptr if texture @gid is via a filepath
    std::span<const uint8_t> get_data(uint32_t gid);

    // null if texture @gid isn't uploaded yet
    GPUImage* get_image(uint32_t gid);
    // null if texture @gid isn't uploaded yet
    GPUImageView* get_image_view(uint32_t gid);
    Sampler get_sampler(uint32_t gid);

    bool is_loaded(uint32_t gid);

    // will overwrite to path even if data is embedded
    void change_path(uint32_t gid, std::filesystem::path new_path);
    // will overwrite to embedded data even if path is stored
    // makes a copy of `data`, no ownership assumed
    bool change_data(uint32_t gid, uint8_t* data, uint32_t data_size);

    void acquire(uint32_t* gids, uint32_t count);
    void release(uint32_t* gids, uint32_t count);
}
