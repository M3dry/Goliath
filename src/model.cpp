#include "goliath/model.hpp"
#include "goliath/buffer.hpp"
#include "goliath/engine.hpp"
#include "goliath/rendering.hpp"
#include "goliath/transport.hpp"
#include <format>
#include <utility>
#include <volk.h>

#define TINYGLTF_IMPLEMENTATION
#define NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tinygltf/tiny_gltf.h>

struct Handled {
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> meshes;
};

uint32_t get_type_size(int type) {
    switch (type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        case TINYGLTF_TYPE_MAT2: return 2 * 2;
        case TINYGLTF_TYPE_MAT3: return 3 * 3;
        case TINYGLTF_TYPE_MAT4: return 4 * 4;
        case TINYGLTF_TYPE_MATRIX:
        case TINYGLTF_TYPE_VECTOR:
            assert(false && "No idea what to do with this one, dump the model data and have fun");
    }

    std::unreachable();
}

uint32_t get_component_type_size(int component_type) {
    switch (component_type) {
        case TINYGLTF_COMPONENT_TYPE_BYTE: return sizeof(int8_t);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return sizeof(uint8_t);
        case TINYGLTF_COMPONENT_TYPE_SHORT: return sizeof(short);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return sizeof(unsigned short);
        case TINYGLTF_COMPONENT_TYPE_INT: return sizeof(int);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return sizeof(unsigned int);
        case TINYGLTF_COMPONENT_TYPE_FLOAT: return sizeof(float);
        case TINYGLTF_COMPONENT_TYPE_DOUBLE: return sizeof(double);
    }

    std::unreachable();
}

float normalize_value(uint8_t* value, int component_type) {
    switch (component_type) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return *((uint8_t*)value) / (float)std::numeric_limits<uint8_t>::max();
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return *((uint16_t*)value) / (float)std::numeric_limits<uint16_t>::max();
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return *((uint32_t*)value) / (float)std::numeric_limits<uint32_t>::max();
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            return std::max(*((int8_t*)value) / (float)std::numeric_limits<int8_t>::max(), -1.0f);
        case TINYGLTF_COMPONENT_TYPE_SHORT:
            return std::max(*((int16_t*)value) / (float)std::numeric_limits<int16_t>::max(), -1.0f);
        case TINYGLTF_COMPONENT_TYPE_INT:
            return std::max(*((int32_t*)value) / (float)std::numeric_limits<int32_t>::max(), -1.0f);
        case TINYGLTF_COMPONENT_TYPE_FLOAT: return *(float*)value;
        case TINYGLTF_COMPONENT_TYPE_DOUBLE: return (float)*(double*)value;
    }

    std::unreachable();
}

void* copy_buffer(uint32_t* size, uint32_t* element_size, const tinygltf::Model& model,
                  const tinygltf::Accessor& accessor, bool normalize = false) {
    auto& buffer_view = model.bufferViews[(uint64_t)accessor.bufferView];
    auto* buffer = (uint8_t*)model.buffers[(uint64_t)buffer_view.buffer].data.data();

    uint32_t type_size = get_type_size(accessor.type);
    uint32_t component_type_size = get_component_type_size(accessor.componentType);
    *element_size = type_size * component_type_size;
    uint32_t buffer_stride = (uint32_t)buffer_view.byteStride;
    if (buffer_stride == 0) buffer_stride = *element_size;

    *element_size = normalize ? sizeof(float) * type_size : *element_size;
    uint8_t* arr = (uint8_t*)malloc(*element_size * accessor.count);
    auto* buffer_cursor = buffer + buffer_view.byteOffset + accessor.byteOffset;

    if (normalize && accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        auto* arr_buf = (float*)arr;

        for (std::size_t i = 0; i < accessor.count * type_size; i++) {
            arr_buf[i] = normalize_value(buffer_cursor, accessor.componentType);
            buffer_cursor += buffer_stride;
        }
    } else {
        for (std::size_t i = 0; i < accessor.count; i++) {
            std::memcpy(arr + i * *element_size, buffer_cursor, *element_size);
            buffer_cursor += buffer_stride;
        }
    }

    *size = (uint32_t)accessor.count;
    return arr;
}

engine::Model::Err parse_primitive(engine::Mesh* out, const tinygltf::Model& model,
                                   const tinygltf::Primitive& primitive, engine::collisions::AABB& model_aabb,
                                   Handled& handled) {
    auto it = primitive.attributes.find("POSITION");
    if (it == primitive.attributes.end()) return engine::Model::PositionAttributeMissing;

    auto position_accesor_id = it->second;
    auto normal_accessor_id = -1;
    auto tangent_accessor_id = -1;
    std::array<int, sizeof(out->texcoords) / sizeof(decltype(out->texcoords[0]))> texcoord_accessor_ids = {-1, -1, -1,
                                                                                                           -1};

    it = primitive.attributes.find("NORMAL");
    if (it != primitive.attributes.end()) normal_accessor_id = it->second;

    it = primitive.attributes.find("TANGENT");
    if (it != primitive.attributes.end()) tangent_accessor_id = it->second;

    for (std::size_t i = 0; i < out->texcoords.size(); i++) {
        it = primitive.attributes.find(std::format("TEXCOORD_{}", i));
        if (it != primitive.attributes.end()) texcoord_accessor_ids[i] = it->second;
    }

    switch (primitive.mode) {
        case TINYGLTF_MODE_POINTS: out->vertex_topology = engine::Topology::Point; break;
        case TINYGLTF_MODE_LINE: out->vertex_topology = engine::Topology::LineList; break;
        case TINYGLTF_MODE_LINE_STRIP: out->vertex_topology = engine::Topology::LineStrip; break;
        case TINYGLTF_MODE_TRIANGLES: out->vertex_topology = engine::Topology::TriangleList; break;
        case TINYGLTF_MODE_TRIANGLE_STRIP: out->vertex_topology = engine::Topology::TriangleStrip; break;
        case TINYGLTF_MODE_TRIANGLE_FAN: out->vertex_topology = engine::Topology::TriangleFan; break;
        default: return engine::Model::UnsupportedMeshTopology;
    }

    if (primitive.indices != -1) {
        uint32_t index_size;
        out->indices =
            (uint32_t*)copy_buffer(&out->index_count, &index_size, model, model.accessors[(uint64_t)primitive.indices]);

        if (index_size != sizeof(uint32_t)) {
            uint32_t* tmp_buf = (uint32_t*)malloc(out->index_count * sizeof(uint32_t));

            for (std::size_t i = 0; i < out->index_count; i++) {
                if (index_size == sizeof(uint16_t)) {
                    tmp_buf[i] = ((uint16_t*)out->indices)[i];
                } else if (index_size == sizeof(uint8_t)) {
                    tmp_buf[i] = ((uint8_t*)out->indices)[i];
                } else {
                    return engine::Model::UnsupportedIndexSize;
                }
            }

            free(out->indices);
            out->indices = tmp_buf;
        }

        if (tangent_accessor_id != -1) out->indexed_tangents = true;
    }

    uint32_t elem_size;
    uint32_t vertex_count;

    out->positions =
        (glm::vec3*)copy_buffer(&out->vertex_count, &elem_size, model, model.accessors[(uint64_t)position_accesor_id]);
    if (elem_size != sizeof(glm::vec3)) return engine::Model::InvalidPositionElementSize;

    if (normal_accessor_id != -1) {
        out->normals =
            (glm::vec3*)copy_buffer(&vertex_count, &elem_size, model, model.accessors[(uint64_t)normal_accessor_id]);
        if (elem_size != sizeof(glm::vec3)) return engine::Model::InvalidNormalElementSize;
        if (vertex_count != out->vertex_count) return engine::Model::VertexCountDiffersBetweenAttributes;
    }

    if (tangent_accessor_id != -1) {
        out->tangents =
            (glm::vec4*)copy_buffer(&vertex_count, &elem_size, model, model.accessors[(uint64_t)tangent_accessor_id]);
        if (elem_size != sizeof(glm::vec4)) return engine::Model::InvalidNormalElementSize;
        if (vertex_count != out->vertex_count) return engine::Model::VertexCountDiffersBetweenAttributes;
    }

    for (std::size_t i = 0; i < out->texcoords.size(); i++) {
        if (texcoord_accessor_ids[i] == -1) continue;

        out->texcoords[i] = (glm::vec2*)copy_buffer(&vertex_count, &elem_size, model,
                                                    model.accessors[(uint64_t)texcoord_accessor_ids[i]], true);
        if (elem_size != sizeof(glm::vec2)) return engine::Model::InvalidTexcoordElementSize;
        if (vertex_count != out->vertex_count) return engine::Model::VertexCountDiffersBetweenAttributes;
    }

    auto& accessor = model.accessors[(uint64_t)position_accesor_id];
    engine::collisions::AABB aabb{
        .min = glm::make_vec3(accessor.minValues.data()),
        .max = glm::make_vec3(accessor.maxValues.data()),
    };

    model_aabb.extend(aabb);

    return engine::Model::Ok;
}

engine::Model::Err parse_node(const tinygltf::Model& model, int node_id, std::vector<engine::Mesh>& meshes,
                              std::vector<glm::mat4>& mesh_transforms, std::vector<uint32_t>& mesh_indices,
                              glm::mat4 current_transform, engine::collisions::AABB& model_aabb, Handled& handled) {
    auto& node = model.nodes[(uint64_t)node_id];

    glm::mat4 mat;
    if (node.matrix.size() != 0) {
        mat = glm::make_mat4(node.matrix.data());
    } else {
        glm::vec3 scale{1.0f};
        if (node.scale.size() != 0) scale = glm::make_vec3(node.scale.data());

        glm::quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
        if (node.rotation.size() != 0) rotation = glm::make_quat(node.rotation.data());

        glm::vec3 translate{0.0f};
        if (node.translation.size() != 0) translate = glm::make_vec3(node.translation.data());

        mat = glm::translate(glm::identity<glm::mat4>(), translate) * glm::mat4_cast(rotation) *
              glm::scale(glm::identity<glm::mat4>(), scale);
    }

    mat = current_transform * mat;

    if (node.mesh == -1) goto mesh_loaded;

    for (auto [mesh_id, mesh_ixs] : handled.meshes) {
        if (mesh_id != (uint32_t)node.mesh) continue;

        mesh_indices.insert(mesh_indices.end(), mesh_ixs.begin(), mesh_ixs.end());
        for (std::size_t i = 0; i < mesh_ixs.size(); i++) {
            mesh_transforms.emplace_back(mat);
        }

        goto mesh_loaded;
    }

    {
        auto& mesh = model.meshes[(uint64_t)node.mesh];
        std::vector<uint32_t> handled_meshes{};
        for (const auto& primitive : mesh.primitives) {
            meshes.emplace_back();

            auto res = parse_primitive(&meshes.back(), model, primitive, model_aabb, handled);
            if (res != engine::Model::Ok) return res;

            mesh_indices.emplace_back(meshes.size() - 1);
            mesh_transforms.emplace_back(mat);
            handled_meshes.emplace_back(meshes.size() - 1);
        }
        handled.meshes.emplace_back(node.mesh, std::move(handled_meshes));
    }

mesh_loaded:
    for (auto children_id : node.children) {
        auto res = parse_node(model, children_id, meshes, mesh_transforms, mesh_indices, mat, model_aabb, handled);
        if (res != engine::Model::Ok) return res;
    }

    return engine::Model::Ok;
}

engine::Model::Err parse_model(engine::Model* out, const tinygltf::Model& model) {
    auto scene_id = model.defaultScene;
    if (scene_id == -1 && model.scenes.size() > 0) {
        scene_id = 0;
    }

    if (scene_id == -1) {
        return engine::Model::NoRootScene;
    }

    engine::collisions::AABB model_aabb;
    std::vector<engine::Mesh> meshes;
    std::vector<uint32_t> mesh_indices;
    std::vector<glm::mat4> mesh_transforms;
    mesh_indices.reserve(model.nodes.size());
    mesh_transforms.reserve(model.nodes.size());

    auto& scene = model.scenes[(uint64_t)scene_id];

    Handled handled{};
    for (auto node_id : scene.nodes) {
        if (node_id == -1) continue;

        auto err = parse_node(model, node_id, meshes, mesh_transforms, mesh_indices, glm::identity<glm::mat4>(),
                              model_aabb, handled);
        if (err != engine::Model::Ok) return err;
    }
    assert(mesh_indices.size() == mesh_transforms.size());
    assert(meshes.size() <= mesh_indices.size());

    out->mesh_count = (uint32_t)meshes.size();
    out->mesh_indices_count = (uint32_t)mesh_indices.size();

    out->meshes = (engine::Mesh*)malloc(sizeof(engine::Mesh) * out->mesh_count);
    std::memcpy(out->meshes, meshes.data(), sizeof(engine::Mesh) * out->mesh_count);

    out->mesh_indexes = (uint32_t*)malloc(sizeof(uint32_t) * out->mesh_indices_count);
    std::memcpy(out->mesh_indexes, mesh_indices.data(), sizeof(uint32_t) * out->mesh_count);

    out->mesh_transforms = (glm::mat4*)malloc(sizeof(glm::mat4) * out->mesh_indices_count);
    std::memcpy(out->mesh_transforms, mesh_transforms.data(), sizeof(glm::mat4) * out->mesh_indices_count);

    out->bounding_box = model_aabb;

    return engine::Model::Ok;
}

namespace engine {
    tinygltf::TinyGLTF loader{};

    std::size_t Mesh::load_optimized(Mesh* out, uint8_t* data) {
        std::size_t offset = 0;

        std::memcpy(&out->material_id, data, sizeof(material_id));
        offset += sizeof(material_id);

        std::memcpy(&out->material_data_size, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        out->material_data = malloc(out->material_data_size);
        std::memcpy(out->material_data, data + offset, out->material_data_size);
        offset += out->material_data_size;

        std::memcpy(&out->vertex_topology, data + offset, sizeof(engine::Topology));
        offset += sizeof(engine::Topology);

        std::memcpy(&out->index_count, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(&out->vertex_count, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(&out->indexed_tangents, data + offset, sizeof(bool));
        offset += sizeof(bool);

        if (out->index_count != (uint32_t)-1) {
            out->indices = (uint32_t*)malloc(sizeof(uint32_t) * out->index_count);
            std::memcpy(out->indices, data + offset, sizeof(uint32_t) * out->index_count);
            offset += sizeof(uint32_t) * out->index_count;
        }

        out->positions = (glm::vec3*)malloc(sizeof(glm::vec3) * out->vertex_count);
        std::memcpy(out->positions, data + offset, sizeof(glm::vec3) * out->vertex_count);
        offset += sizeof(glm::vec3) * out->vertex_count;

        out->normals = (glm::vec3*)malloc(sizeof(glm::vec3) * out->vertex_count);
        std::memcpy(out->normals, data + offset, sizeof(glm::vec3) * out->vertex_count);
        offset += sizeof(glm::vec3) * out->vertex_count;

        uint32_t tangent_size = 0;
        if (out->indexed_tangents) tangent_size = sizeof(glm::vec4) * out->vertex_count;
        else tangent_size = sizeof(glm::vec4) * out->index_count;

        out->tangents = (glm::vec4*)malloc(tangent_size);
        std::memcpy(out->tangents, data + offset, tangent_size);
        offset += tangent_size;

        for (std::size_t i = 0; i < out->texcoords.size(); i++) {
            out->texcoords[i] = (glm::vec2*)malloc(sizeof(glm::vec2) * out->vertex_count);
            std::memcpy(out->texcoords[i], data + offset, sizeof(glm::vec2) * out->vertex_count);
            offset += sizeof(glm::vec2) * out->vertex_count;
        }

        std::memcpy(&out->bounding_box, data + offset, sizeof(collisions::AABB));
        offset += sizeof(collisions::AABB);

        return offset;
    }

    model::GPUOffset Mesh::calc_offset(uint32_t start_offset, uint32_t* total_size) const {
        uint32_t size = 0;
        size = 0;
        size += material_data_size;

        model::GPUOffset offset{
            .start = start_offset,
            .material_offset = 0,
        };

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

        if (tangents != nullptr) {
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

        offset.stride = stride;

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
            std::memcpy(buf + offset.tangent_offset, tangents, index_count * sizeof(glm::vec4));
        }

        if (offset.position_offset != (uint32_t)-1) {
            auto buf_ = buf + offset.position_offset;
            for (std::size_t i = 0; i < vertex_count; i++) {
                std::memcpy(buf_, &positions[i], sizeof(glm::vec3));
                buf_ += offset.stride;
            }
        }

        if (offset.normal_offset != (uint32_t)-1) {
            auto buf_ = buf + offset.normal_offset;
            for (std::size_t i = 0; i < vertex_count; i++) {
                std::memcpy(buf_, &normals[i], sizeof(glm::vec3));
                buf_ += offset.stride;
            }
        }

        if (offset.tangent_offset != (uint32_t)-1) {
            auto buf_ = buf + offset.tangent_offset;
            for (std::size_t i = 0; i < vertex_count; i++) {
                std::memcpy(buf_, &tangents[i], sizeof(glm::vec4));
                buf_ += offset.stride;
            }
        }

        for (std::size_t t = 0; t < texcoords.size(); t++) {
            if (offset.texcoords_offset[t] == (uint32_t)-1) continue;

            auto buf_ = buf + offset.texcoords_offset[t];
            for (std::size_t i = 0; i < vertex_count; i++) {
                std::memcpy(buf_, &texcoords[t][i], sizeof(glm::vec2));
                buf_ += offset.stride;
            }
        }

        return total_size;
    }

    Model::Err Model::load_gltf(Model* out, std::span<uint8_t> data, std::string* tinygltf_error,
                                std::string* tinygltf_warning) {
        tinygltf::Model model;

        if (!loader.LoadASCIIFromString(&model, tinygltf_error, tinygltf_warning, (const char*)data.data(),
                                        (uint32_t)data.size(), "")) {
            return TinyGLTFErr;
        }

        auto parse_res = parse_model(out, model);

        return parse_res;
    }

    Model::Err Model::load_glb(Model* out, std::span<uint8_t> data, std::string* tinygltf_error,
                               std::string* tinygltf_warning) {
        tinygltf::Model model;

        if (!loader.LoadBinaryFromMemory(&model, tinygltf_error, tinygltf_warning, data.data(),
                                         (uint32_t)data.size())) {
            return TinyGLTFErr;
        }

        auto parse_res = parse_model(out, model);

        return parse_res;
    }

    bool Model::load_optimized(Model* out, uint8_t* data) {
        uint32_t offset = 0;

        std::memcpy(&out->bounding_box, data, sizeof(collisions::AABB));
        offset += sizeof(collisions::AABB);

        std::memcpy(&out->mesh_count, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        out->meshes = (Mesh*)malloc(out->mesh_count * sizeof(Mesh));
        for (std::size_t i = 0; i < out->mesh_count; i++) {
            offset += Mesh::load_optimized(&out->meshes[i], data + offset);
        }

        std::memcpy(&out->mesh_indices_count, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        out->mesh_indexes = (uint32_t*)malloc(out->mesh_indices_count * sizeof(uint32_t));
        std::memcpy(out->mesh_indexes, data + offset, out->mesh_indices_count * sizeof(uint32_t));
        offset += out->mesh_indices_count * sizeof(uint32_t);

        out->mesh_transforms = (glm::mat4*)malloc(out->mesh_indices_count * sizeof(glm::mat4));
        std::memcpy(out->mesh_transforms, data + offset, out->mesh_indices_count * sizeof(glm::mat4));
        offset += out->mesh_indices_count + sizeof(glm::mat4);

        return true;
    }

    void GPUGroup::destroy() {
        free(material_ids);
        free(offsets);
        free(mesh_transforms);
        free(mesh_vertex_counts);

        vertex_data.destroy();
        texture_pool::free(texture_block);
    }
}

namespace engine::model {
    std::vector<const Model*> upload_models{};
    uint32_t current_offset = 0;
    uint32_t needed_textures = 0;

    void begin_gpu_upload() {
        upload_models.clear();
        current_offset = 0;
        needed_textures = 0;
    }

    GPUModel upload(const Model* model) {
        upload_models.emplace_back(model);

        for (std::size_t i = 0; i < model->mesh_count; i++) {
            needed_textures += model->meshes[i].material_texture_count;
        }

        auto last_offset = current_offset;
        current_offset += model->mesh_indices_count;
        return {last_offset, current_offset};
    }

    GPUGroup end_gpu_upload(VkBufferMemoryBarrier2* barrier) {
        auto group = GPUGroup{
            .mesh_count = current_offset,
            .material_ids = (material_id*)malloc(sizeof(material_id) * current_offset),
            .offsets = (GPUOffset*)malloc(sizeof(GPUOffset) * current_offset),
            .mesh_transforms = (glm::mat4*)malloc(sizeof(glm::mat4) * current_offset),
            .mesh_vertex_counts = (uint32_t*)malloc(sizeof(uint32_t) * current_offset),
            .vertex_data = Buffer{},
            .texture_block = texture_pool::alloc(needed_textures),
        };

        uint32_t mesh_indicies_offset = 0;
        uint32_t total_size = 0;
        std::vector<GPUOffset> mesh_offsets{};
        for (auto model : upload_models) {
            mesh_offsets.resize(model->mesh_count);
            for (std::size_t i = 0; i < model->mesh_count; i++) {
                uint32_t size = 0;
                mesh_offsets[i] = model->meshes[i].calc_offset(total_size, &size);
                total_size += size;
            }

            for (std::size_t i = 0; i < model->mesh_indices_count; i++) {
                const auto& mesh = model->meshes[i];

                group.material_ids[i + mesh_indicies_offset] = mesh.material_id;
                group.offsets[i + mesh_indicies_offset] = mesh_offsets[i];
                group.mesh_transforms[i + mesh_indicies_offset] = model->mesh_transforms[i + mesh_indicies_offset];
                group.mesh_vertex_counts[i + mesh_indicies_offset] = mesh.index_count;
            }
        }

        group.vertex_data =
            Buffer::create(total_size, VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT,
                           std::nullopt, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        uint8_t* data = (uint8_t*)malloc(total_size);
        uint32_t current_start = 0;
        for (auto model : upload_models) {
            for (std::size_t i = 0; i < model->mesh_count; i++) {
                const auto& mesh = model->meshes[i];
                uint32_t size = mesh.upload_data(data + current_start);

                transport::upload(barrier, data + current_start, size, group.vertex_data.data(), current_start);

                current_start += size;
            }
        }

        barrier->offset = 0;
        barrier->size = total_size;

        free(data);
        return group;
    }
}
