#pragma once

#include "goliath/texture_registry.hpp"
#include <cstdint>
#include <cstring>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <optional>

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
        Padding32,
        Padding64,
    };

    constexpr std::optional<size_t> is_padding(const attribute& attr) {
        return attr == attribute::Padding32 || attr == attribute::Padding64;
    }

    constexpr size_t size(const attribute& attr) {
        switch (attr) {
            using enum attribute;
            case Texture: return sizeof(uint32_t);
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
            case Padding32: return sizeof(uint32_t);
            case Padding64: return sizeof(uint64_t);
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
                uint32_t gid;
                std::memcpy(&gid, material_data + offset, sizeof(uint32_t));

                texture_registry::acquire(&gid, 1);
            }
        }

        void release_textures(uint8_t* material_data) {
            for (const auto& offset : texture_gid_offsets) {
                uint32_t gid;
                std::memcpy(&gid, material_data + offset, sizeof(uint32_t));

                texture_registry::release(&gid, 1);
            }
        }
    };
}

namespace engine::material::pbr {
    extern Material schema;

    struct Data {
        uint32_t albedo_map{0};
        uint32_t metallic_roughness_map{0};
        uint32_t normal_map{0};
        uint32_t occlusion_map{0};
        uint32_t emissive_map{0};

        uint8_t albedo_texcoord:2;
        uint8_t metallic_roughness_texcoord:2;
        uint8_t normal_texcoord:2;
        uint8_t occlusion_texcoord:2;
        uint8_t emissive_texcoord:2;

        glm::vec4 albedo{1.0f};
        float metallic_factor{0.0f};
        float roughness_factor{0.0f};
        float normal_factor{0.0f};
        float occlusion_factor{0.0f};
        glm::vec3 emissive_factor{0.0f};
    };

    void write_data_blob(const Data& data, void* out);
}
