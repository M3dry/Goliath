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

        gid() : value(-1) {}
        gid(uint32_t generation, uint32_t id) : value((id & id_mask) | ((generation & 0xFFu) << gen_shift)) {}

        uint32_t id() const {
            return value & id_mask;
        }

        uint32_t gen() const {
            return (value & gen_mask) >> gen_shift;
        }

        uint32_t dim() const {
            return 0;
        }

        bool operator==(gid other) const {
            return value == other.value;
        }
    };

    struct LoadError {
        gid model;
    };

    void init(std::filesystem::path models_dir, Textures* textures, Materials* materials);
    void destroy();

    void to_json(nlohmann::json& j, const gid& gid);
    void from_json(const nlohmann::json& j, gid& gid);

    void load(const nlohmann::json& j);
    nlohmann::json save();

    using AddFn = std::function<bool(gid, const std::filesystem::path& path)>;
    gid add(AddFn&& add_fn, std::string name);
    gid add(Model model, std::string name);
    bool remove(gid gid);

    std::expected<std::string*, Err> get_name(gid gid);
    std::expected<engine::Model*, Err> get_cpu_model(gid gid);
    std::expected<transport2::ticket, Err> get_ticket(gid gid);
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

    void modified();

    bool is_deleted(gid gid);
}

namespace engine::culling {
    std::expected<void, models::Err> flatten(models::gid gid, uint64_t transforms_addr, uint32_t default_transform_offset);
}
