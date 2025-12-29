#pragma once

#include "goliath/samplers.hpp"
#include "goliath/texture.hpp"
#include "goliath/texture_pool.hpp"
#include <cstdint>
#include <expected>

#include <nlohmann/json.hpp>

namespace engine::textures {
    enum struct Err {
        BadGeneration,
    };

    struct gid {
        uint8_t generation:8;
        uint32_t id:24;

        bool operator==(const gid& other) const {
            return id == other.id && generation == other.generation;
        }
    };

    void to_json(nlohmann::json& j, const gid& gid);
    void from_json(const nlohmann::json& j, gid& gid);

    void load(nlohmann::json j);
    nlohmann::json save();

    gid add(std::filesystem::path path, std::string name, Sampler sampler);
    gid add(std::span<uint8_t> image, uint32_t width, uint32_t height, VkFormat format, std::string name, Sampler sampler);
    bool remove(gid gid);

    std::expected<std::string*, Err> get_name(gid gid);
    std::expected<GPUImage, Err> get_image(gid gid);
    std::expected<VkImageView, Err> get_image_view(gid gid);
    std::expected<uint32_t, Err> get_sampler(gid gid);

    uint8_t get_generation(uint32_t ix);

    void acquire(const gid* gids, uint32_t count);
    void release(const gid* gids, uint32_t count);

    const TexturePool& get_texture_pool();
}
