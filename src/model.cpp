#include "goliath/model.hpp"
#include "goliath/buffer.hpp"
#include "goliath/collisions.hpp"
#include "goliath/gpu_group.hpp"
#include "goliath/material.hpp"
#include "goliath/rendering.hpp"

#include <utility>
#include <volk.h>

namespace engine {
    uint32_t Mesh::get_optimized_size() const {
        uint32_t total_size = sizeof(material_id) + sizeof(uint32_t) + material_data_size + sizeof(engine::Topology) +
                              sizeof(uint32_t) + sizeof(uint32_t) + sizeof(bool) + sizeof(collisions::AABB) +
                              (2 + texcoords.size()) * sizeof(bool);

        if (indices != nullptr) total_size += index_count * sizeof(uint32_t);
        if (positions != nullptr) total_size += vertex_count * sizeof(glm::vec3);
        if (normals != nullptr) total_size += vertex_count * sizeof(glm::vec3);
        if (tangents != nullptr) total_size += (indexed_tangents ? index_count : vertex_count) * sizeof(glm::vec4);
        for (std::size_t i = 0; i < texcoords.size(); i++) {
            if (texcoords[i] != nullptr) total_size += vertex_count * sizeof(glm::vec2);
        }

        return total_size;
    }

    void Mesh::save_optimized(std::span<uint8_t> data) const {
        uint32_t off = 0;

        std::memcpy(data.data() + off, &material_id, sizeof(material_id));
        off += sizeof(material_id);
        assert(off < data.size());

        std::memcpy(data.data() + off, &material_data_size, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        std::memcpy(data.data() + off, material_data, material_data_size);
        off += material_data_size;
        assert(off < data.size());

        std::memcpy(data.data() + off, &vertex_topology, sizeof(Topology));
        off += sizeof(Topology);
        assert(off < data.size());

        std::memcpy(data.data() + off, &index_count, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        std::memcpy(data.data() + off, &vertex_count, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        std::memcpy(data.data() + off, &indexed_tangents, sizeof(bool));
        off += sizeof(bool);
        assert(off < data.size());

        std::memcpy(data.data() + off, &bounding_box, sizeof(collisions::AABB));
        off += sizeof(collisions::AABB);
        assert(off < data.size());

        std::memcpy(data.data() + off, indices, index_count * sizeof(uint32_t));
        off += index_count * sizeof(uint32_t);
        assert(off < data.size());

        std::memcpy(data.data() + off, positions, vertex_count * sizeof(glm::vec3));
        off += vertex_count * sizeof(glm::vec3);
        assert(off < data.size());

        std::memset(data.data() + off, normals != nullptr, sizeof(bool));
        off += sizeof(bool);
        assert(off < data.size());

        std::memset(data.data() + off, tangents != nullptr, sizeof(bool));
        off += sizeof(bool);
        assert(off < data.size());

        for (const auto& texcoord : texcoords) {
            std::memset(data.data() + off, texcoord != nullptr, sizeof(bool));
            off += sizeof(bool);
            assert(off < data.size());
        }

        if (normals != nullptr) {
            std::memcpy(data.data() + off, normals, vertex_count * sizeof(glm::vec3));
            off += vertex_count * sizeof(glm::vec3);
            assert(off < data.size());
        }

        if (tangents != nullptr) {
            auto size = (indexed_tangents ? index_count : vertex_count) * sizeof(glm::vec4);
            std::memcpy(data.data() + off, tangents, size);
            off += size;
            assert(off < data.size());
        }

        for (const auto& texcoord : texcoords) {
            if (texcoord != nullptr) {
                std::memcpy(data.data() + off, texcoord, vertex_count * sizeof(glm::vec2));
                off += vertex_count * sizeof(glm::vec2);
                assert(off <= data.size());
            }
        }
    }

    void Mesh::load_optimized(Mesh& out, std::span<uint8_t> data) {
        out = Mesh{};

        uint32_t off = 0;

        std::memcpy(&out.material_id, data.data() + off, sizeof(material_id));
        off += sizeof(material_id);
        assert(off < data.size());

        std::memcpy(&out.material_data_size, data.data() + off, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        out.material_data = malloc(out.material_data_size);
        std::memcpy(out.material_data, data.data() + off, out.material_data_size);
        off += out.material_data_size;
        assert(off < data.size());

        // FIXME: figure out where to update the stored texture count/if to store it in the binary
        out.material_texture_count = material::pbr::schema.texture_gid_offsets.size();

        std::memcpy(&out.vertex_topology, data.data() + off, sizeof(Topology));
        off += sizeof(Topology);
        assert(off < data.size());

        std::memcpy(&out.index_count, data.data() + off, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        std::memcpy(&out.vertex_count, data.data() + off, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        std::memcpy(&out.indexed_tangents, data.data() + off, sizeof(bool));
        off += sizeof(bool);
        assert(off < data.size());

        std::memcpy(&out.bounding_box, data.data() + off, sizeof(collisions::AABB));
        off += sizeof(collisions::AABB);
        assert(off < data.size());

        out.indices = (uint32_t*)malloc(out.index_count * sizeof(uint32_t));
        std::memcpy(out.indices, data.data() + off, out.index_count * sizeof(uint32_t));
        off += out.index_count * sizeof(uint32_t);
        assert(off < data.size());

        out.positions = (glm::vec3*)malloc(out.vertex_count * sizeof(glm::vec3));
        std::memcpy(out.positions, data.data() + off, out.vertex_count * sizeof(glm::vec3));
        off += out.vertex_count * sizeof(glm::vec3);
        assert(off < data.size());

        bool has_normals;
        std::memcpy(&has_normals, data.data() + off, sizeof(bool));
        off += sizeof(bool);
        assert(off < data.size());

        bool has_tangents;
        std::memcpy(&has_tangents, data.data() + off, sizeof(bool));
        off += sizeof(bool);
        assert(off < data.size());

        std::array<bool, 4> has_texcoord{};
        for (auto& has_texcoord : has_texcoord) {
            std::memcpy(&has_texcoord, data.data() + off, sizeof(bool));
            off += sizeof(bool);
            assert(off < data.size());
        }

        if (has_normals) {
            out.normals = (glm::vec3*)malloc(out.vertex_count * sizeof(glm::vec3));
            std::memcpy(out.normals, data.data() + off, out.vertex_count * sizeof(glm::vec3));
            off += out.vertex_count * sizeof(glm::vec3);
            assert(off <= data.size());
        }

        if (has_tangents) {
            auto size = (out.indexed_tangents ? out.index_count : out.vertex_count) * sizeof(glm::vec4);
            out.tangents = (glm::vec4*)malloc(size);
            std::memcpy(out.tangents, data.data() + off, size);
            off += size;
            assert(off <= data.size());
        }

        for (size_t i = 0; i < has_texcoord.size(); i++) {
            if (!has_texcoord[i]) continue;

            out.texcoords[i] = (glm::vec2*)malloc(out.vertex_count * sizeof(glm::vec2));
            std::memcpy(out.texcoords[i], data.data() + off, out.vertex_count * sizeof(glm::vec2));
            off += out.vertex_count * sizeof(glm::vec2);
            assert(off <= data.size());
        }
    }

    model::GPUOffset Mesh::calc_offset(uint32_t start_offset, uint32_t* total_size) const {
        uint32_t size = 0;
        size += material_data_size;

        model::GPUOffset offset{
            .start = start_offset,
            .material_offset = 0,
        };
        offset.set_indexed_tangetns(indexed_tangents);

        if (indices != nullptr) {
            offset.indices_offset = size;
            size += sizeof(uint32_t) * index_count;
        }

        if (!indexed_tangents && tangents != nullptr) {
            offset.tangent_offset = size;
            size += sizeof(glm::vec4) * index_count;
        }

        uint32_t stride = 0;
        uint32_t stride_start = size;
        if (positions != nullptr) {
            offset.position_offset = stride_start;
            stride_start += sizeof(glm::vec3);
            stride += sizeof(glm::vec3);
        }

        if (normals != nullptr) {
            offset.normal_offset = stride_start;
            stride_start += sizeof(glm::vec3);
            stride += sizeof(glm::vec3);
        }

        if (indexed_tangents && tangents != nullptr) {
            offset.tangent_offset = stride_start;
            stride_start += sizeof(glm::vec4);
            stride += sizeof(glm::vec4);
        }

        for (std::size_t i = 0; i < texcoords.size(); i++) {
            if (texcoords[i] != nullptr) {
                offset.texcoords_offset[i] = stride_start;
                stride_start += sizeof(glm::vec2);
                stride += sizeof(glm::vec2);
            }
        }

        offset.set_stride(stride);

        *total_size = size + stride * vertex_count;
        return offset;
    }

    uint32_t Mesh::upload_data(uint8_t* buf) const {
        uint32_t total_size;
        auto offset = calc_offset(0, &total_size);

        std::memcpy(buf + offset.material_offset, material_data, material_data_size);

        if (offset.indices_offset != (uint32_t)-1) {
            std::memcpy(buf + offset.indices_offset, indices, index_count * sizeof(uint32_t));
        }

        if (!indexed_tangents && offset.tangent_offset != (uint32_t)-1) {
            printf("indexed tangents: %d\n", indexed_tangents);
            printf("tangent offset: %d\n", offset.tangent_offset);
            std::memcpy(buf + offset.tangent_offset, tangents, index_count * sizeof(glm::vec4));
        }

        if (offset.position_offset != (uint32_t)-1) {
            auto buf_ = buf + offset.position_offset;
            for (std::size_t i = 0; i < vertex_count; i++) {
                std::memcpy(buf_, &positions[i], sizeof(glm::vec3));
                buf_ += offset.get_stride();
            }
        }

        if (offset.normal_offset != (uint32_t)-1) {
            auto buf_ = buf + offset.normal_offset;
            for (std::size_t i = 0; i < vertex_count; i++) {
                std::memcpy(buf_, &normals[i], sizeof(glm::vec3));
                buf_ += offset.get_stride();
            }
        }

        if (indexed_tangents && offset.tangent_offset != (uint32_t)-1) {
            auto buf_ = buf + offset.tangent_offset;
            for (std::size_t i = 0; i < vertex_count; i++) {
                std::memcpy(buf_, &tangents[i], sizeof(glm::vec4));
                buf_ += offset.get_stride();
            }
        }

        for (std::size_t t = 0; t < texcoords.size(); t++) {
            if (offset.texcoords_offset[t] == (uint32_t)-1) continue;

            auto buf_ = buf + offset.texcoords_offset[t];
            for (std::size_t i = 0; i < vertex_count; i++) {
                std::memcpy(buf_, &texcoords[t][i], sizeof(glm::vec2));
                buf_ += offset.get_stride();
            }
        }

        return total_size;
    }

    uint32_t Model::get_optimized_size() const {
        uint32_t total_size = sizeof(collisions::AABB) + sizeof(uint32_t) +
                              mesh_indices_count * (sizeof(uint32_t) + sizeof(glm::mat4)) + sizeof(uint32_t) +
                              mesh_count * sizeof(uint32_t);

        for (size_t mesh_ix = 0; mesh_ix < mesh_count; mesh_ix++) {
            total_size += meshes[mesh_ix].get_optimized_size();
        }

        return total_size;
    }

    void Model::save_optimized(std::span<uint8_t> data) const {
        uint32_t header_size = sizeof(collisions::AABB) + sizeof(uint32_t) +
                               mesh_indices_count * (sizeof(uint32_t) + sizeof(glm::mat4)) + sizeof(uint32_t) +
                               mesh_count * sizeof(uint32_t);
        uint32_t off = 0;

        std::memcpy(data.data() + off, &bounding_box, sizeof(collisions::AABB));
        off += sizeof(collisions::AABB);
        assert(off < data.size());

        std::memcpy(data.data() + off, &mesh_indices_count, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        std::memcpy(data.data() + off, mesh_indexes, sizeof(uint32_t) * mesh_indices_count);
        off += sizeof(uint32_t) * mesh_indices_count;
        assert(off < data.size());

        std::memcpy(data.data() + off, mesh_transforms, sizeof(glm::mat4) * mesh_indices_count);
        off += sizeof(glm::mat4) * mesh_indices_count;
        assert(off < data.size());

        std::memcpy(data.data() + off, &mesh_count, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        uint32_t mesh_offset = header_size;
        for (size_t mesh_ix = 0; mesh_ix < mesh_count; mesh_ix++) {
            const auto& mesh = meshes[mesh_ix];
            mesh.save_optimized({data.data() + mesh_offset, data.size() - mesh_offset});

            std::memcpy(data.data() + off, &mesh_offset, sizeof(uint32_t));
            off += sizeof(uint32_t);
            assert(off < data.size());

            mesh_offset += mesh.get_optimized_size();
            assert(mesh_offset <= data.size());
        }
    }

    void Model::load_optimized(Model& out, std::span<uint8_t> data) {
        uint32_t off = 0;

        std::memcpy(&out.bounding_box, data.data() + off, sizeof(collisions::AABB));
        off += sizeof(collisions::AABB);
        assert(off < data.size());

        std::memcpy(&out.mesh_indices_count, data.data() + off, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        out.mesh_indexes = (uint32_t*)malloc(sizeof(uint32_t) * out.mesh_indices_count);
        std::memcpy(out.mesh_indexes, data.data() + off, sizeof(uint32_t) * out.mesh_indices_count);
        off += sizeof(uint32_t) * out.mesh_indices_count;
        assert(off < data.size());

        out.mesh_transforms = (glm::mat4*)malloc(sizeof(glm::mat4) * out.mesh_indices_count);
        std::memcpy(out.mesh_transforms, data.data() + off, sizeof(glm::mat4) * out.mesh_indices_count);
        off += sizeof(glm::mat4) * out.mesh_indices_count;
        assert(off < data.size());

        std::memcpy(&out.mesh_count, data.data() + off, sizeof(uint32_t));
        off += sizeof(uint32_t);
        assert(off < data.size());

        out.meshes = (Mesh*)malloc(out.mesh_count * sizeof(Mesh));
        for (size_t mesh_ix = 0; mesh_ix < out.mesh_count; mesh_ix++) {
            uint32_t mesh_offset;
            std::memcpy(&mesh_offset, data.data() + off, sizeof(uint32_t));
            off += sizeof(uint32_t);
            assert(off < data.size());

            assert(mesh_offset < data.size());
            Mesh::load_optimized(out.meshes[mesh_ix], {data.data() + mesh_offset, data.size() - mesh_offset});
        }
    }
}

namespace engine::model {
    void upload_func(uint8_t* data, uint32_t start, uint32_t size, textures::gid* texture_gids,
                     uint32_t texture_gid_count, void* ctx) {
        const uint8_t* final_data = data + size;
        const Model* model;
        std::memcpy(&model, ctx, sizeof(void*));
        GPUOffset* offsets = (GPUOffset*)((uint8_t*)ctx + sizeof(void*));

        uint32_t texture_count = 0;
        for (std::size_t i = 0; i < model->mesh_count; i++) {
            const auto& mesh = model->meshes[i];

            if (mesh.material_id == 0 && mesh.material_data_size == material::pbr::schema.total_size) {
                for (const auto& off : material::pbr::schema.texture_gid_offsets) {
                    std::memcpy(&texture_gids[texture_count], (uint8_t*)(mesh.material_data) + off, sizeof(uint32_t));
                    texture_count++;
                }
            }
        }

        for (std::size_t i = 0; i < model->mesh_indices_count; i++) {
            auto mesh_ix = model->mesh_indexes[i];
            const auto& mesh = model->meshes[mesh_ix];

            GPUMeshData m{};

            m.mat_id = mesh.material_id;
            m.offset = offsets[mesh_ix];
            m.transform = model->mesh_transforms[mesh_ix];
            m.vertex_count = mesh.indices != nullptr ? mesh.index_count : mesh.vertex_count;
            m.bounding_box = mesh.bounding_box;

            std::memcpy(data, &m, sizeof(GPUMeshData));
            data += sizeof(GPUMeshData);
        }

        for (std::size_t i = 0; i < model->mesh_count; i++) {
            data += model->meshes[i].upload_data(data);
        }

        free(ctx);
    };

    std::pair<GPUModel, Buffer> upload(const Model* model) {
        uint32_t needed_textures = 0;
        for (std::size_t i = 0; i < model->mesh_count; i++) {
            needed_textures += model->meshes[i].material_texture_count;
        }

        auto ctx = malloc(sizeof(void*) + sizeof(GPUOffset) * model->mesh_count);
        std::memcpy(ctx, &model, sizeof(void*));

        GPUOffset* offsets = (GPUOffset*)((uint8_t*)ctx + sizeof(void*));

        uint32_t texture_count = 0;
        uint32_t mesh_sizes = sizeof(GPUMeshData) * model->mesh_indices_count;
        for (std::size_t i = 0; i < model->mesh_count; i++) {
            uint32_t size;
            offsets[i] = model->meshes[i].calc_offset(mesh_sizes, &size);
            offsets[i].relative_start = mesh_sizes - i * sizeof(GPUMeshData);
            mesh_sizes += size;
            texture_count += model->meshes[i].material_texture_count;
        }

        auto data_offset = engine::gpu_group::upload(texture_count, mesh_sizes, upload_func, (void*)ctx);
        for (std::size_t i = 0; i < model->mesh_count; i++) {
            offsets[i].start += data_offset;
        }

        void* draw_buf_host;
        bool draw_buf_coherent;
        auto draw_buf = Buffer::create("model draw buffer", model->mesh_indices_count * sizeof(DrawCommand),
                                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                       {{&draw_buf_host, &draw_buf_coherent}});

        for (uint32_t i = 0; i < model->mesh_indices_count; i++) {
            auto mesh_ix = model->mesh_indexes[i];
            auto& mesh = model->meshes[mesh_ix];

            auto draw_cmd = DrawCommand{
                .cmd =
                    VkDrawIndirectCommand{
                        .vertexCount = mesh.indices != nullptr ? mesh.index_count : mesh.vertex_count,
                        .instanceCount = 1,
                        .firstVertex = 0,
                        .firstInstance = 0,
                    },
                .start_offset = data_offset + i * (uint32_t)sizeof(GPUMeshData),
            };
            std::memcpy((uint8_t*)draw_buf_host + i * sizeof(DrawCommand), &draw_cmd, sizeof(DrawCommand));
        }
        if (!draw_buf_coherent) draw_buf.flush_mapped(0, (uint32_t)draw_buf.size());

        return {GPUModel{data_offset, model->mesh_indices_count}, draw_buf};
    }

    GPUModel upload_raw(const Model* model) {
        uint32_t needed_textures = 0;
        for (std::size_t i = 0; i < model->mesh_count; i++) {
            needed_textures += model->meshes[i].material_texture_count;
        }

        auto ctx = malloc(sizeof(void*) + sizeof(GPUOffset) * model->mesh_count);
        std::memcpy(ctx, &model, sizeof(void*));

        GPUOffset* offsets = (GPUOffset*)((uint8_t*)ctx + sizeof(void*));

        uint32_t texture_count = 0;
        uint32_t mesh_sizes = sizeof(GPUMeshData) * model->mesh_indices_count;
        for (std::size_t i = 0; i < model->mesh_count; i++) {
            uint32_t size;
            offsets[i] = model->meshes[i].calc_offset(mesh_sizes, &size);
            offsets[i].relative_start = mesh_sizes - i * sizeof(GPUMeshData);
            mesh_sizes += size;
            texture_count += model->meshes[i].material_texture_count;
        }
        auto data_offset = engine::gpu_group::upload(texture_count, mesh_sizes, upload_func, (void*)ctx);
        for (std::size_t i = 0; i < model->mesh_count; i++) {
            offsets[i].start += data_offset;
        }

        return GPUModel{data_offset, model->mesh_indices_count};
    }
}
