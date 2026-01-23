#pragma once

#include "goliath/textures.hpp"
#include <cstdint>
#include <cstring>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <nlohmann/json.hpp>

namespace engine::material {
    enum struct attribute {
        Texture,
        Float,
        Uint,
        Int,
        Vec2,
        Vec3,
        Vec4,
        UVec2,
        UVec3,
        UVec4,
        IVec2,
        IVec3,
        IVec4,
        Mat2x2,
        Mat3x3,
        Mat4x4,
    };

    constexpr size_t size(const attribute& attr) {
        switch (attr) {
            using enum attribute;
            case Texture: return sizeof(textures::gid);
            case Float: return sizeof(float);
            case Uint: return sizeof(uint32_t);
            case Int: return sizeof(int32_t);
            case Vec2: return sizeof(glm::vec2);
            case Vec3: return sizeof(glm::vec3);
            case Vec4: return sizeof(glm::vec4);
            case UVec2: return sizeof(glm::vec<2, uint32_t>);
            case UVec3: return sizeof(glm::vec<3, uint32_t>);
            case UVec4: return sizeof(glm::vec<4, uint32_t>);
            case IVec2: return sizeof(glm::vec<2, int32_t>);
            case IVec3: return sizeof(glm::vec<3, int32_t>);
            case IVec4: return sizeof(glm::vec<4, int32_t>);
            case Mat2x2: return sizeof(glm::mat2);
            case Mat3x3: return sizeof(glm::mat3);
            case Mat4x4: return sizeof(glm::mat4);
        }

        assert(false);
    }
}

namespace engine {
    struct Material {
        uint32_t total_size = 0;
        std::vector<std::string> names;
        std::vector<material::attribute> attributes{};
        std::vector<uint32_t> texture_gid_offsets{};

        template <typename Attrib> void emplace_back_attrib(std::string name, Attrib&& attr) {
            attributes.emplace_back(attr);
            names.emplace_back(name);

            if (attr == material::attribute::Texture) {
                texture_gid_offsets.emplace_back(total_size);
            }

            total_size += material::size(attributes.back());
        }

        void pop_back_attrib() {
            total_size -= material::size(attributes.back());
            attributes.pop_back();
            names.pop_back();

            rebuild_offsets();
        }

        void remove_attrib(uint32_t ix) {
            attributes.erase(attributes.begin() + ix);
            names.erase(names.begin() + ix);
            rebuild_offsets();
        }

        void swap_attributes(uint32_t ix1, uint32_t ix2) {
            std::swap(attributes[ix1], attributes[ix2]);
            std::swap(names[ix1], names[ix2]);
        }

        void rebuild_offsets() {
            texture_gid_offsets.clear();

            uint32_t current_offset = 0;
            for (const auto& attr : attributes) {
                if (attr == material::attribute::Texture) {
                    texture_gid_offsets.emplace_back(current_offset);
                }

                current_offset += material::size(attr);
            }

            total_size = current_offset;
        }

        void acquire_textures(uint8_t* material_data) {
            for (const auto& offset : texture_gid_offsets) {
                textures::gid gid;
                std::memcpy(&gid, material_data + offset, sizeof(uint32_t));

                textures::acquire(&gid, 1);
            }
        }

        void release_textures(uint8_t* material_data) {
            for (const auto& offset : texture_gid_offsets) {
                textures::gid gid;
                std::memcpy(&gid, material_data + offset, sizeof(uint32_t));

                textures::release(&gid, 1);
            }
        }

        constexpr bool operator==(const Material& other) const {
            return total_size == other.total_size && names == other.names && attributes == other.attributes && texture_gid_offsets == other.texture_gid_offsets;
        }
    };

    void to_json(nlohmann::json& j, const Material& mat);
    void from_json(const nlohmann::json& j, Material& mat);
}

namespace engine::material::pbr {
    extern Material schema;

    struct Data {
        textures::gid albedo_map{0, 0};
        textures::gid metallic_roughness_map{0, 0};
        textures::gid normal_map{0, 0};
        textures::gid occlusion_map{0, 0};
        textures::gid emissive_map{0, 0};

        uint32_t albedo_texcoord;
        uint32_t metallic_roughness_texcoord;
        uint32_t normal_texcoord;
        uint32_t occlusion_texcoord;
        uint32_t emissive_texcoord;

        glm::vec4 albedo{1.0f};
        float metallic_factor{0.0f};
        float roughness_factor{0.0f};
        float normal_factor{0.0f};
        float occlusion_factor{0.0f};
        glm::vec3 emissive_factor{0.0f};
    };

    void write_data_blob(const Data& data, void* out);
}
