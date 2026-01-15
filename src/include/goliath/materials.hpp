#pragma once

#include "goliath/buffer.hpp"
#include "goliath/material.hpp"

#include <nlohmann/json.hpp>

//   :GPU materials schema:
//   uint total mat id count
//   uint[total mat id count] material instance offsets
//   uint[] material data
//   ::
//
//   :Shader material instance data fetch:
//   in mat_id
//   auto mat_instances = materials[mat_id]
//
//   mat_instance_ix = mesh.offsets.material_offset
//   mat_data = parse_material(mat_instances[mat_instance_ix])
//   ::
//
//   :Updating material instance:
//   in mat_id, mat_instance_ix
//
//   mat_offset = cpu_material_offsets[mat_id] // in bytes
//
//   mat_data = cpu_materials[mat_id][mat_instance_ix]
//   update(mat_data)
//
//   upload_to_gpu(materials, offset: mat_offset, sizeof(mat_data), &mat_data);
//   ::
//
//   :New material instance:
//   in mat_id, mat_data
//
//   instance_ix = cpu_materials[mat_id].size();
//
//   cpu_materials[mat_id].emplace_back(mat_data);
//
//   for (i = mat_id + 1 < cpu_materials.size()) {
//      cpu_material_offset[i] += sizeof(mat_data);
//   }
//
//   update_material_buffer();
//
//   out instance_ix
//   ::
//
//   :New material archetype:
//   in schema
//
//   mat_id = cpu_materials.size()
//   cpu_material_offset.emplace_back(cpu_material_offset.size() == 0 ? 0 : cpu_material_offset.back() + cpu_material_schemas.size * cpu_materials.back().size)
//   cpu_material_schemas.emplace_back(schema);
//   cpu_materials.emplace_back()
//
//   out mat_id
//   ::
//
//   :Updating material buffer:
//   memcpy(gpu_staging_buffer, &cpu_offsets.size(), sizeof(uint32_t))
//   memcpy(gpu_staging_buffer + <off>, cpu_offsets, cpu_offsets.size() * sizeof(uint32_t))
//   for (i = 0 < cpu_materials.size()) {
//      memcpy(gpu_staging_buffer + <off>, cpu_materials[i], cpu_materials[i].size() * cpu_material_schemas[i].size)
//   }
//
//   transfer(gpu_staging_buffer)
//   ::

namespace engine::materials {
    void load(const nlohmann::json& j);
    nlohmann::json save();

    const Material& get_schema(uint32_t mat_id);
    uint32_t add_schema(Material schema);
    bool remove_schema(uint32_t mat_id);

    std::span<uint8_t> get_instance_data(uint32_t mat_id, uint32_t instance_ix);
    void update_instance_data(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data);
    uint32_t add_instance(uint32_t mat_id, std::string name, uint8_t* data);
    bool remove_instance(uint32_t mat_id, uint32_t instance_ix);

    Buffer get_buffer();
}
