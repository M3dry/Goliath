#pragma once

#include "goliath/buffer.hpp"
#include "goliath/collisions.hpp"
#include "goliath/rendering.hpp"
#include "goliath/texture_pool.hpp"
#include <cstdint>
#include <cstring>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <string>

namespace engine::model {
    struct GPUOffsets {
        uint32_t start = (uint32_t)-1;
        uint32_t stride = 0;
        uint32_t material_offset = (uint32_t)-1;
        uint32_t indices_offset = (uint32_t)-1;
        uint32_t position_offset = (uint32_t)-1;
        uint32_t normal_offset = (uint32_t)-1;
        uint32_t tangent_offset = (uint32_t)-1;
        uint32_t texcoord1_offset = (uint32_t)-1;
        uint32_t texcoord2_offset = (uint32_t)-1;
        uint32_t texcoord3_offset = (uint32_t)-1;
        uint32_t texcoord4_offset = (uint32_t)-1;
    };

    struct Material_PBR {
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

namespace engine {
    using material_id = uint32_t;

    struct Mesh {
        material_id material_id;
        uint32_t material_data_size;
        void* material_data = nullptr;

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

        static std::size_t load_optimized(Mesh* out, uint8_t* data);

        void clone(Mesh* out) {
            out->material_id = material_id;
            out->material_data_size = material_data_size;
            out->material_data = malloc(out->material_data_size);
            std::memcpy(out->material_data, material_data, material_data_size);

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
            free(material_data);
            delete[] positions;
            delete[] normals;
            delete[] tangents;
            for (auto texcoord : texcoords) {
                delete[] texcoord;
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

        enum Err {
            Ok,
            TinyGLTFErr,
            NoRootScene,
            PositionAttributeMissing,
            UnsupportedMeshTopology,
            UnsupportedIndexSize,
            InvalidPositionElementSize,
            InvalidNormalElementSize,
            InvalidTangentElementSize,
            InvalidTexcoordElementSize,
            VertexCountDiffersBetweenAttributes,
        };

        static Err load_gltf(Model* out, std::span<uint8_t> data, std::string* tinygltf_error = nullptr, std::string* tinygltf_warning = nullptr);
        static Err load_glb(Model* out, std::span<uint8_t> data, std::string* tinygltf_err = nullptr, std::string* tinygltf_warning = nullptr);
        static bool load_optimized(Model* out, uint8_t* data);

        void destroy() {
            for (std::size_t i = 0; i < mesh_count; i++) {
                meshes[i].destroy();
            }

            delete[] mesh_transforms;
            delete[] meshes;
        }
    };

    using GPUModel = std::pair<uint32_t, uint32_t>;

    struct GPUGroup {
        uint32_t mesh_count;
        material_id* material_ids;
        model::GPUOffsets* offsets;
        glm::mat4* model_transforms;
        uint32_t* mesh_vertex_counts;

        Buffer vertex_data;
        std::pair<uint32_t, uint32_t> texture_block;

        void destroy();
    };
}

namespace engine::model {
    void begin_gpu_upload();
    GPUModel upload();
    GPUGroup end_gpu_upload();
}
