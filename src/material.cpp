#include "goliath/material.hpp"

namespace engine::material::pbr {
    Material make_schema() {
        Material mat{};

        mat.emplace_back_attrib("albedo map", material::attribute::Texture);
        mat.emplace_back_attrib("metallic roughness map", material::attribute::Texture);
        mat.emplace_back_attrib("normal map", material::attribute::Texture);
        mat.emplace_back_attrib("occlusion map", material::attribute::Texture);
        mat.emplace_back_attrib("emissive map", material::attribute::Texture);

        mat.emplace_back_attrib("albedo texcoord", material::attribute::Uint);
        mat.emplace_back_attrib("metallic roughness texcoord", material::attribute::Uint);
        mat.emplace_back_attrib("normal texcoord", material::attribute::Uint);
        mat.emplace_back_attrib("occlusion texcoord", material::attribute::Uint);
        mat.emplace_back_attrib("emissive texcoord", material::attribute::Uint);

        mat.emplace_back_attrib("albedo", material::attribute::Vec4);
        mat.emplace_back_attrib("mettalic factor", material::attribute::Float);
        mat.emplace_back_attrib("roughness factor", material::attribute::Float);
        mat.emplace_back_attrib("normal factor", material::attribute::Float);
        mat.emplace_back_attrib("occlusion factor", material::attribute::Float);
        mat.emplace_back_attrib("emissive factor", material::attribute::Vec3);

        return mat;
    }

    Material schema = make_schema();

    void write_data_blob(const Data& data, void* out) {
        uint8_t* blob = (uint8_t*)out;

        std::memcpy(blob, &data.albedo_map, sizeof(textures::gid));
        blob += sizeof(textures::gid);

        std::memcpy(blob, &data.metallic_roughness_map, sizeof(textures::gid));
        blob += sizeof(textures::gid);

        std::memcpy(blob, &data.normal_map, sizeof(textures::gid));
        blob += sizeof(textures::gid);

        std::memcpy(blob, &data.occlusion_map, sizeof(textures::gid));
        blob += sizeof(textures::gid);

        std::memcpy(blob, &data.emissive_map, sizeof(textures::gid));
        blob += sizeof(textures::gid);

        std::memcpy(blob, &data.albedo_texcoord, sizeof(uint32_t));
        blob += sizeof(uint32_t);

        std::memcpy(blob, &data.metallic_roughness_texcoord, sizeof(uint32_t));
        blob += sizeof(uint32_t);

        std::memcpy(blob, &data.normal_texcoord, sizeof(uint32_t));
        blob += sizeof(uint32_t);

        std::memcpy(blob, &data.occlusion_texcoord, sizeof(uint32_t));
        blob += sizeof(uint32_t);

        std::memcpy(blob, &data.emissive_texcoord, sizeof(uint32_t));
        blob += sizeof(uint32_t);

        std::memcpy(blob, &data.albedo, sizeof(glm::vec4));
        blob += sizeof(glm::vec4);

        std::memcpy(blob, &data.metallic_factor, sizeof(float));
        blob += sizeof(float);

        std::memcpy(blob, &data.roughness_factor, sizeof(float));
        blob += sizeof(float);

        std::memcpy(blob, &data.normal_factor, sizeof(float));
        blob += sizeof(float);

        std::memcpy(blob, &data.occlusion_factor, sizeof(float));
        blob += sizeof(float);

        std::memcpy(blob, &data.emissive_factor, sizeof(glm::vec4));
        blob += sizeof(glm::vec4);
    }
}
