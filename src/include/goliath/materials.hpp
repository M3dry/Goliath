#pragma once

#include "goliath/buffer.hpp"
#include "goliath/material.hpp"

#include <nlohmann/json.hpp>

namespace engine::materials {
    void load(const nlohmann::json& j);
    nlohmann::json save();

    nlohmann::json default_json();

    const Material& get_schema(uint32_t mat_id);
    uint32_t add_schema(Material schema, std::string name);
    bool remove_schema(uint32_t mat_id);

    std::span<uint8_t> get_instance_data(uint32_t mat_id, uint32_t instance_ix);
    void update_instance_data(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data);
    uint32_t add_instance(uint32_t mat_id, std::string name, uint8_t* data);
    bool remove_instance(uint32_t mat_id, uint32_t instance_ix);
    void acquire_instance(uint32_t mat_id, uint32_t instance_ix);
    void release_instance(uint32_t mat_id, uint32_t instance_ix);

    Buffer get_buffer();
}
