#pragma once

#include "goliath/gpu_group.hpp"
#include "goliath/model.hpp"
#include <nlohmann/json.hpp>

namespace models {
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

    void process_uploads();

    void init(const nlohmann::json& j);
    void destroy();

    nlohmann::json save();

    gid add(std::filesystem::path path, std::string name);
    bool remove(gid gid);

    std::expected<std::string*, Err> get_name(gid gid);
    std::expected<engine::Model*, Err> get_cpu_model(gid gid);

    // timeline == -1 implies the model hasn't been yet uploaded to the GPU
    std::expected<uint64_t, Err> get_timeline(gid gid);
    std::expected<engine::Buffer, Err> get_draw_buffer(gid gid);
    std::expected<engine::GPUModel, Err> get_gpu_model(gid gid);
    std::expected<engine::GPUGroup, Err> get_gpu_group(gid gid);

    uint8_t get_generation(uint32_t ix);

    enum struct LoadState {
        OnDisk,
        OnCPU,
        OnGPU,
    };

    std::expected<LoadState, Err> is_loaded(gid gid);

    void acquire(const gid* gids, uint32_t count);
    void release(const gid* gids, uint32_t count);

    std::span<std::string> get_names();
}

namespace engine::culling {
    std::expected<void, models::Err> flatten(models::gid gid, uint64_t transforms_addr, uint32_t default_transform_offset);
}
