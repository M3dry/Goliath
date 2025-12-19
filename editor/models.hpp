#pragma once

#include "goliath/gpu_group.hpp"
#include "goliath/model.hpp"
#include <nlohmann/json.hpp>

namespace models {
    struct gid {
        uint8_t generation:8;
        uint32_t id:24;
    };

    void to_json(nlohmann::json& j, const gid& gid);
    void from_json(const nlohmann::json& j, gid& gid);

    void process_uploads();

    void init(std::filesystem::path json_file, bool* parse_error);
    void destroy();

    nlohmann::json save();

    gid add(std::filesystem::path path, std::string name);
    bool remove(gid gid);

    std::string& get_name(gid gid);
    const std::filesystem::path& get_path(gid gid);
    std::optional<engine::Model>& get_cpu_model(gid gid);

    // timeline == -1 implies the model hasn't been yet uploaded to the GPU
    uint64_t get_timeline(gid gid);
    engine::Buffer get_draw_buffer(gid gid);
    engine::GPUModel get_gpu_model(gid gid);
    engine::GPUGroup get_gpu_group(gid gid);

    void acquire(const gid* gids, uint32_t count);
    void release(const gid* gids, uint32_t count);
}
