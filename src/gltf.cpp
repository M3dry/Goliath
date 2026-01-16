#include "goliath/gltf.hpp"
#include "goliath/materials.hpp"
#include "goliath/textures.hpp"
#include "goliath/material.hpp"

#define TINYGLTF_IMPLEMENTATION
#define NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tinygltf/tiny_gltf.h"

struct Handled {
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> meshes{};
    std::vector<std::pair<uint32_t, engine::textures::gid>> textures{};
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

engine::textures::gid parse_texture(const std::string& tex_name, const tinygltf::Model& model, int tex_id, bool srgb, Handled& handled) {
    if (tex_id == -1) return {0, 0};
    for (const auto& [id, gid] : handled.textures) {
        if (tex_id == id) return gid;
    }

    const auto& texture = model.textures[tex_id];
    if (texture.source == -1) return {0,0};

    const auto& image = model.images[texture.source];

bool resize = false;
    VkFormat format;
    if (srgb) {
        format = VK_FORMAT_R8G8B8A8_SRGB;
        assert(image.component == 4);
        assert(image.bits == 8);
    } else {
        switch (image.bits) {
            case 8:
                switch (image.component) {
                    case 1: format = VK_FORMAT_R8_UNORM; break;
                    case 2: format = VK_FORMAT_R8G8_UNORM; break;
                    case 3: resize = true;
                    case 4: format = VK_FORMAT_R8G8B8A8_UNORM; break;
                    default: assert(false && "Invalid `component` size of a source");
                }
                break;
            case 16:
                switch (image.component) {
                    case 1: format = VK_FORMAT_R16_UNORM; break;
                    case 2: format = VK_FORMAT_R16G16_UNORM; break;
                    case 3: resize = true;
                    case 4: format = VK_FORMAT_R16G16B16A16_UNORM; break;
                    default: assert(false && "Invalid `component` size of a source");
                }
                break;
            case 32: assert(false && "Unsupported source bit depth size: 32");
            default: assert(false && "Invalid `bits` size of a source");
        }
    }

    engine::Sampler sampler{};
    if (texture.sampler != -1) {
        const auto& gltf_sampler = model.samplers[texture.sampler];

        switch (gltf_sampler.minFilter) {
            case TINYGLTF_TEXTURE_FILTER_LINEAR: sampler.min_filter(engine::FilterMode::Linear); break;
            case TINYGLTF_TEXTURE_FILTER_NEAREST: sampler.min_filter(engine::FilterMode::Nearest); break;
            case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
                sampler.min_filter(engine::FilterMode::Linear);
                sampler.mipmap(engine::MipMapMode::Linear);
                break;
            case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
                sampler.min_filter(engine::FilterMode::Linear);
                sampler.mipmap(engine::MipMapMode::Nearest);
                break;
            case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
                sampler.min_filter(engine::FilterMode::Nearest);
                sampler.mipmap(engine::MipMapMode::Linear);
                break;
            case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
                sampler.min_filter(engine::FilterMode::Nearest);
                sampler.mipmap(engine::MipMapMode::Nearest);
                break;
            default: break;
        }

        switch (gltf_sampler.wrapS) {
            case TINYGLTF_TEXTURE_WRAP_REPEAT: sampler.address_u(engine::AddressMode::Repeat); break;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: sampler.address_u(engine::AddressMode::MirroredRepeat); break;
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE: sampler.address_u(engine::AddressMode::ClampToEdge); break;
            default: break;
        }

        switch (gltf_sampler.wrapT) {
            case TINYGLTF_TEXTURE_WRAP_REPEAT: sampler.address_v(engine::AddressMode::Repeat); break;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: sampler.address_v(engine::AddressMode::MirroredRepeat); break;
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE: sampler.address_v(engine::AddressMode::ClampToEdge); break;
            default: break;
        }
    }

    uint32_t image_size = resize ? 4 * image.bits / 8 * image.width * image.height : image.image.size();
    void* image_data = malloc(image_size);
    if (resize) {
        auto orig_chunk = image.component * image.bits / 8;
        auto new_chunk = 4 * image.bits / 8;
        for (std::size_t i = 0; i < image.width * image.height; i++) {
            std::memcpy((uint8_t*)image_data + i * new_chunk, image.image.data() + i * orig_chunk, orig_chunk);
            std::memset((uint8_t*)image_data + i * new_chunk + orig_chunk, 0, new_chunk - orig_chunk);
        }
    } else {
        std::memcpy(image_data, image.image.data(), image_size);
    }

    auto gid = engine::textures::add({(uint8_t*)image_data, image_size}, image.width, image.height, format,
                                             tex_name, sampler);
    handled.textures.emplace_back(tex_id, gid);
    return gid;
}

void parse_material(const std::string& prim_name, engine::Mesh* out, const tinygltf::Model& model, const tinygltf::Material& material,
                    Handled& handled) {
    out->material_id = 0;
    out->material_data_size = engine::material::pbr::schema.total_size;
    out->material_data = malloc(out->material_data_size);
    out->material_texture_count = 5;

    engine::material::pbr::Data pbr_data{
        .albedo_map = parse_texture(prim_name + ": Albedo", model, material.pbrMetallicRoughness.baseColorTexture.index, true, handled),
        .metallic_roughness_map =
            parse_texture(prim_name + ": Metallic Roughness", model, material.pbrMetallicRoughness.metallicRoughnessTexture.index, false, handled),
        .normal_map = parse_texture(prim_name + ": Normal", model, material.normalTexture.index, false, handled),
        .occlusion_map = parse_texture(prim_name + ": Occlusion", model, material.occlusionTexture.index, false, handled),
        .emissive_map = parse_texture(prim_name + ": Emissive", model, material.emissiveTexture.index, true, handled),

        .albedo_texcoord = (uint32_t)material.pbrMetallicRoughness.baseColorTexture.texCoord,
        .metallic_roughness_texcoord = (uint32_t)material.pbrMetallicRoughness.metallicRoughnessTexture.texCoord,
        .normal_texcoord = (uint32_t)material.normalTexture.texCoord,
        .occlusion_texcoord = (uint32_t)material.occlusionTexture.texCoord,
        .emissive_texcoord = (uint32_t)material.emissiveTexture.texCoord,

        .albedo = glm::make_vec4(material.pbrMetallicRoughness.baseColorFactor.data()),
        .metallic_factor = (float)material.pbrMetallicRoughness.metallicFactor,
        .roughness_factor = (float)material.pbrMetallicRoughness.roughnessFactor,
        .normal_factor = (float)material.normalTexture.scale,
        .occlusion_factor = (float)material.occlusionTexture.strength,
        .emissive_factor = glm::make_vec3(material.emissiveFactor.data()),
    };

    engine::material::pbr::write_data_blob(pbr_data, out->material_data);
    engine::materials::add_instance(0, prim_name.c_str(), (uint8_t*)out->material_data);
}

engine::gltf::Err parse_primitive(const std::string& prim_name, engine::Mesh* out, const tinygltf::Model& model,
                                   const tinygltf::Primitive& primitive, engine::collisions::AABB& model_aabb,
                                   Handled& handled) {
    auto it = primitive.attributes.find("POSITION");
    if (it == primitive.attributes.end()) return engine::gltf::PositionAttributeMissing;

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
        default: return engine::gltf::UnsupportedMeshTopology;
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
                    return engine::gltf::UnsupportedIndexSize;
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
    if (elem_size != sizeof(glm::vec3)) return engine::gltf::InvalidPositionElementSize;

    if (normal_accessor_id != -1) {
        out->normals =
            (glm::vec3*)copy_buffer(&vertex_count, &elem_size, model, model.accessors[(uint64_t)normal_accessor_id]);
        if (elem_size != sizeof(glm::vec3)) return engine::gltf::InvalidNormalElementSize;
        if (vertex_count != out->vertex_count) return engine::gltf::VertexCountDiffersBetweenAttributes;
    }

    if (tangent_accessor_id != -1) {
        out->tangents =
            (glm::vec4*)copy_buffer(&vertex_count, &elem_size, model, model.accessors[(uint64_t)tangent_accessor_id]);
        if (elem_size != sizeof(glm::vec4)) return engine::gltf::InvalidNormalElementSize;
        if (vertex_count != out->vertex_count) return engine::gltf::VertexCountDiffersBetweenAttributes;
    }

    for (std::size_t i = 0; i < out->texcoords.size(); i++) {
        if (texcoord_accessor_ids[i] == -1) continue;

        out->texcoords[i] = (glm::vec2*)copy_buffer(&vertex_count, &elem_size, model,
                                                    model.accessors[(uint64_t)texcoord_accessor_ids[i]], true);
        if (elem_size != sizeof(glm::vec2)) return engine::gltf::InvalidTexcoordElementSize;
        if (vertex_count != out->vertex_count) return engine::gltf::VertexCountDiffersBetweenAttributes;
    }

    auto& accessor = model.accessors[(uint64_t)position_accesor_id];
    engine::collisions::AABB aabb{
        .min = glm::make_vec3(accessor.minValues.data()),
        .max = glm::make_vec3(accessor.maxValues.data()),
    };

    out->bounding_box = aabb;
    model_aabb.extend(aabb);

    assert(primitive.material >= 0 && "NOTE: if this asserts add default material creation");
    parse_material(prim_name, out, model, model.materials[primitive.material], handled);

    return engine::gltf::Ok;
}

engine::gltf::Err parse_node(const tinygltf::Model& model, int node_id, std::vector<engine::Mesh>& meshes,
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
        for (const auto& [i, primitive] : mesh.primitives | std::ranges::views::enumerate) {
            meshes.emplace_back();

            auto res = parse_primitive(i == 0 ? mesh.name : std::format("{} #{}", mesh.name, i), &meshes.back(), model, primitive, model_aabb, handled);
            if (res != engine::gltf::Ok) return res;

            mesh_indices.emplace_back(meshes.size() - 1);
            mesh_transforms.emplace_back(mat);
            handled_meshes.emplace_back(meshes.size() - 1);
        }
        handled.meshes.emplace_back(node.mesh, std::move(handled_meshes));
    }

mesh_loaded:
    for (auto children_id : node.children) {
        auto res = parse_node(model, children_id, meshes, mesh_transforms, mesh_indices, mat, model_aabb, handled);
        if (res != engine::gltf::Ok) return res;
    }

    return engine::gltf::Ok;
}

engine::gltf::Err parse_model(engine::Model* out, const tinygltf::Model& model) {
    auto scene_id = model.defaultScene;
    if (scene_id == -1 && model.scenes.size() > 0) {
        scene_id = 0;
    }

    if (scene_id == -1) {
        return engine::gltf::NoRootScene;
    }

    engine::collisions::AABB model_aabb{};
    std::vector<engine::Mesh> meshes{};
    std::vector<uint32_t> mesh_indices{};
    std::vector<glm::mat4> mesh_transforms{};
    mesh_indices.reserve(model.nodes.size());
    mesh_transforms.reserve(model.nodes.size());

    auto& scene = model.scenes[(uint64_t)scene_id];

    Handled handled{};
    for (auto node_id : scene.nodes) {
        if (node_id == -1) continue;

        auto err = parse_node(model, node_id, meshes, mesh_transforms, mesh_indices, glm::identity<glm::mat4>(),
                              model_aabb, handled);
        if (err != engine::gltf::Ok) return err;
    }
    assert(mesh_indices.size() == mesh_transforms.size());
    assert(meshes.size() <= mesh_indices.size());

    out->mesh_count = (uint32_t)meshes.size();
    out->mesh_indices_count = (uint32_t)mesh_indices.size();

    if (out->mesh_count != 0) {
        out->meshes = (engine::Mesh*)malloc(sizeof(engine::Mesh) * out->mesh_count);
        std::memcpy(out->meshes, meshes.data(), sizeof(engine::Mesh) * out->mesh_count);
    }

    if (out->mesh_indices_count != 0) {
        out->mesh_indexes = (uint32_t*)malloc(sizeof(uint32_t) * out->mesh_indices_count);
        std::memcpy(out->mesh_indexes, mesh_indices.data(), sizeof(uint32_t) * out->mesh_count);
    }

    if (out->mesh_indices_count != 0) {
        out->mesh_transforms = (glm::mat4*)malloc(sizeof(glm::mat4) * out->mesh_indices_count);
        std::memcpy(out->mesh_transforms, mesh_transforms.data(), sizeof(glm::mat4) * out->mesh_indices_count);
    }

    out->bounding_box = model_aabb;

    return engine::gltf::Ok;
}

namespace engine::gltf {
    tinygltf::TinyGLTF loader{};

    Err load_json(engine::Model* out, std::span<uint8_t> data, const std::string& base_dir, std::string* tinygltf_error,
                  std::string* tinygltf_warning) {
        tinygltf::Model model;

        if (!loader.LoadASCIIFromString(&model, tinygltf_error, tinygltf_warning, (const char*)data.data(),
                                        (uint32_t)data.size(), base_dir)) {
            return TinyGLTFErr;
        }

        auto parse_res = parse_model(out, model);

        return parse_res;
    }

    Err load_bin(engine::Model* out, std::span<uint8_t> data, const std::string& base_dir, std::string* tinygltf_error,
                 std::string* tinygltf_warning) {
        tinygltf::Model model;

        if (!loader.LoadBinaryFromMemory(&model, tinygltf_error, tinygltf_warning, data.data(), (uint32_t)data.size(),
                                         base_dir)) {
            return TinyGLTFErr;
        }

        return parse_model(out, model);
    }
}
