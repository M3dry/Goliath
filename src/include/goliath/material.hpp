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
    struct attribute;

    enum struct AttributeType {
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

    struct attribute {
        AttributeType type;

        constexpr attribute(AttributeType type) : type(type) {};

        constexpr std::optional<size_t> is_padding() const {
            return type == AttributeType::Padding32 || type == AttributeType::Padding64;
        }

        constexpr size_t size() const {
            switch (type) {
                using enum AttributeType;
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
    };
}

namespace engine {
    struct Material {
        uint32_t total_size = 0;
        std::vector<std::string> names;
        std::vector<material::attribute> attributes{};
        std::vector<uint32_t> texture_gid_offsets{}; // used for clean up

        template <typename Attrib> void emplace_back_attrib(std::string name, Attrib&& attr) {
            attributes.emplace_back(attr);
            names.emplace_back(name);

            total_size += attributes.back().size();
        }

        void pop_back_attrib() {
            total_size -= attributes.back().size();
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
                if (attr.type == material::AttributeType::Texture) {
                    texture_gid_offsets.emplace_back(current_offset);
                }

                current_offset += attr.size();
            }

            total_size = current_offset;
        }

        void destroy() {
            for (auto& attr : attributes) {
            }
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

namespace engine::material {
    Material make_pbr_material() {
        Material mat{};

        mat.emplace_back_attrib("albedo map", material::AttributeType::Texture);
        mat.emplace_back_attrib("metallic roughness map", material::AttributeType::Texture);
        mat.emplace_back_attrib("occlusion map", material::AttributeType::Texture);
        mat.emplace_back_attrib("emissive map", material::AttributeType::Texture);
        mat.emplace_back_attrib("albedo", material::AttributeType::Vec4);
        mat.emplace_back_attrib("mettalic factor", material::AttributeType::Float);
        mat.emplace_back_attrib("roughness factor", material::AttributeType::Float);
        mat.emplace_back_attrib("normal factor", material::AttributeType::Float);
        mat.emplace_back_attrib("occlusion factor", material::AttributeType::Float);
        mat.emplace_back_attrib("emissive factor", material::AttributeType::Vec3);

        return mat;
    }

    static Material pbr = make_pbr_material();
}
