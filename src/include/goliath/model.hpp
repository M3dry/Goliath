#pragma once

#include "goliath/buffer.hpp"
#include "goliath/collisions.hpp"
#include "goliath/rendering.hpp"
#include <cstdint>
#include <cstring>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <volk.h>

namespace engine::model {
    struct GPUOffset {
        uint32_t start = (uint32_t)-1;
        uint32_t relative_start = (uint32_t)-1;
        uint32_t stride = 0;
        uint32_t material_offset = (uint32_t)-1;
        uint32_t indices_offset = (uint32_t)-1;
        uint32_t position_offset = (uint32_t)-1;
        uint32_t normal_offset = (uint32_t)-1;
        uint32_t tangent_offset = (uint32_t)-1;
        std::array<uint32_t, 4> texcoords_offset = {(uint32_t)-1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};

        static constexpr uint32_t STRIDE_MASK = 0x7FFFFFFFu;
        static constexpr uint32_t INDEXED_TANGENTS_MASK = 0x80000000u;

        void set_stride(uint32_t value) {
            stride = (stride & INDEXED_TANGENTS_MASK) | (value & STRIDE_MASK);
        }

        uint32_t get_stride() const {
            return stride & STRIDE_MASK;
        }

        void set_indexed_tangetns(bool value) {
            if (value) stride |= INDEXED_TANGENTS_MASK;
            else stride &= STRIDE_MASK;
        }

        bool get_indexed_tangetns() const {
            return (stride & INDEXED_TANGENTS_MASK) != 0;
        }
    };
}

namespace engine {
    using material_id = uint16_t;

    struct Mesh {
        material_id material_id = (uint16_t)-1;
        uint32_t material_instance;

        engine::Topology vertex_topology;

        uint32_t index_count = 0;
        uint32_t vertex_count = 0;
        bool indexed_tangents = false;

        uint32_t* indices = nullptr;
        glm::vec3* positions = nullptr;
        glm::vec3* normals = nullptr;
        glm::vec4* tangents = nullptr;
        std::array<glm::vec2*, 4> texcoords = {nullptr, nullptr, nullptr, nullptr};

        collisions::AABB bounding_box;

        uint32_t get_optimized_size() const;
        void save_optimized(std::span<uint8_t> data) const;
        static void load_optimized(Mesh& out, std::span<uint8_t> data);

        model::GPUOffset calc_offset(uint32_t start_offset, uint32_t* total_size) const;
        // returns data size it wrote to `buf`
        uint32_t upload_data(uint8_t* buf) const;

        void clone(Mesh* out) {
            out->material_id = material_id;
            out->material_instance = material_instance;

            out->vertex_topology = vertex_topology;
            out->vertex_count = vertex_count;

            out->positions = (glm::vec3*)malloc(vertex_count * sizeof(glm::vec3));
            std::memcpy(out->positions, positions, vertex_count * sizeof(glm::vec3));

            out->normals = (glm::vec3*)malloc(vertex_count * sizeof(glm::vec3));
            std::memcpy(out->normals, normals, vertex_count * sizeof(glm::vec3));

            out->tangents = (glm::vec4*)malloc(vertex_count * sizeof(glm::vec4));
            std::memcpy(out->tangents, tangents, vertex_count * sizeof(glm::vec4));

            for (std::size_t i = 0; i < texcoords.size(); i++) {
                out->texcoords[i] = (glm::vec2*)malloc(vertex_count * sizeof(glm::vec2));
                std::memcpy(out->texcoords[i], texcoords[i], vertex_count * sizeof(glm::vec2));
            }

            out->bounding_box = bounding_box;
        }

        void destroy() {
            free(indices);
            free(positions);
            free(normals);
            free(tangents);
            for (auto texcoord : texcoords) {
                free(texcoord);
            }
        }
    };

    struct Model {
        collisions::AABB bounding_box{};

        uint32_t mesh_count = 0;
        Mesh* meshes = nullptr;

        uint32_t mesh_indices_count = 0;
        uint32_t* mesh_indexes = nullptr;
        glm::mat4* mesh_transforms = nullptr;

        uint32_t get_save_size() const;
        // [`data`, `data` + `get_optimized_size()`) must be a valid range
        void save(std::span<uint8_t> data) const;

        static void load(Model& out, std::span<uint8_t> data);

        void destroy() {
            for (std::size_t i = 0; i < mesh_count; i++) {
                meshes[i].destroy();
            }

            free(mesh_transforms);
            free(meshes);
        }
    };

    struct GPUModel {
        uint32_t data_start;
        uint32_t mesh_count;
    };
}

namespace engine::model {
    struct DrawCommand {
        VkDrawIndirectCommand cmd;
        uint32_t start_offset;
        uint32_t _transform_offset{(uint32_t)-1};
    };

    struct GPUMeshData {
        GPUOffset offset;
        uint16_t _padding{};
        material_id mat_id;
        uint32_t vertex_count;
        glm::mat4 transform;
        collisions::AABB bounding_box;
    };

    std::pair<GPUModel, Buffer>
    upload(const Model* model);
    GPUModel upload_raw(const Model* model);
}
