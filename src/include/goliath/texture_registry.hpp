#pragma once

#include "goliath/texture.hpp"
#include "goliath/samplers.hpp"
#include "goliath/texture_pool.hpp"

#include <cstdint>
#include <filesystem>

#include <nlohmann/json.hpp>

namespace engine::texture_registry {
    void init();
    void destroy();

    void load(uint8_t* file_data, uint32_t file_size);
    uint8_t* save(uint32_t& size);

    uint32_t add(std::filesystem::path path, std::string name, const Sampler& sampler);
    uint32_t add(uint8_t* data, uint32_t data_size, uint32_t width, uint32_t height, VkFormat format, std::string name, const Sampler& sampler);
    bool remove(uint32_t gid);

    std::string& get_name(uint32_t gid);

    // null if texture @gid is embedded data
    const std::filesystem::path& get_path(uint32_t gid);
    // .data() of span is nullptr if texture @gid is via a filepath
    std::span<const uint8_t> get_blob(uint32_t gid);

    // null if texture @gid isn't uploaded yet
    GPUImage get_image(uint32_t gid);
    // null if texture @gid isn't uploaded yet
    VkImageView get_image_view(uint32_t gid);
    Sampler get_sampler(uint32_t gid);

    // will overwrite to path even if data is embedded
    void change_path(uint32_t gid, std::filesystem::path new_path);
    // will overwrite to embedded data even if path is stored
    // makes a copy of `data`, no ownership assumed
    void change_data(uint32_t gid, uint8_t* data, uint32_t data_size);

    void acquire(const uint32_t* gids, uint32_t count);
    void release(const uint32_t* gids, uint32_t count);

    void rebuild_pool();
    const TexturePool& get_texture_pool();
}
