#pragma once

#include "goliath/texture_pool.hpp"
#include <cstdint>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

namespace engine::model {
    struct GPUOffsets {
        uint32_t start = (uint32_t)-1;
        uint32_t stride = 0;
        uint32_t indices_offset = (uint32_t)-1;
        uint32_t position_offset = (uint32_t)-1;
        uint32_t normal_offset = (uint32_t)-1;
        uint32_t tangent_offset = (uint32_t)-1;
    };

    struct GPUMaterialOffsets_PBR {
        uint32_t albedo_map_uv_offset = (uint32_t)-1;
        uint32_t normal_map_uv_offset = (uint32_t)-1;
        uint32_t occlusion_map_uv_offset = (uint32_t)-1;
        uint32_t emissive_map_uv_offset = (uint32_t)-1;
    };

    struct GPUMaterial_PBR {
        uint32_t albedo_map = texture_pool::null_ix;
        uint32_t metallic_roughness_map = texture_pool::null_ix;
        uint32_t normal_map = texture_pool::null_ix;
        uint32_t occlusion_map = texture_pool::null_ix;
        uint32_t emissive_map = texture_pool::null_ix;
        glm::vec4 albedo{1.0f};
        glm::vec3 metallic_roughness_normal_oclussion_factors;
        glm::vec3 emissive_factor;
    };
}
