#pragma once

#include "goliath/gpu_group.hpp"
#include "goliath/model.hpp"
#include <nlohmann/json.hpp>

namespace engine::models {
    enum struct Err {
        BadGeneration,
    };

    struct gid {
        uint32_t value;

        static constexpr uint32_t id_mask = 0x00FF'FFFFu;
        static constexpr uint32_t gen_mask = 0xFF00'0000u;
        static constexpr uint32_t gen_shift = 24;

        gid() : value(0) {}
        gid(uint32_t generation, uint32_t id) : value((id & id_mask) | ((generation & 0xFFu) << gen_shift)) {}

        uint32_t id() const {
            return value & id_mask;
        }

        uint32_t gen() const {
            return (value & gen_mask) >> gen_shift;
        }

        bool operator==(gid other) const {
            return value == other.value;
        }
    };

    void to_json(nlohmann::json& j, const gid& gid);
    void from_json(const nlohmann::json& j, gid& gid);

    void load(const nlohmann::json& j);
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
