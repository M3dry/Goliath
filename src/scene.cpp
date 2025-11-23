#include "goliath/scene.hpp"
#include "goliath/gpu_group.hpp"
#include "goliath/model.hpp"
#include "goliath/transport.hpp"
#include <filesystem>
#include <vulkan/vulkan_core.h>

void engine::Scene::load(Scene* out, uint8_t* data, uint32_t size) {
    uint32_t offset = 0;
    assert(offset < size);

    std::memcpy(&out->model_count, data, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    assert(offset < size);

    uint32_t total_instance_count;
    std::memcpy(&total_instance_count, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    assert(offset < size);

    std::memcpy(&out->external_count, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    assert(offset < size);

    uint32_t names_size;
    std::memcpy(&names_size, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    assert(offset < size);

    out->model_names = (Str*)malloc(sizeof(Str) * out->model_count);
    out->model_instances = (Instances*)malloc(sizeof(Instances) * out->model_count);
    out->models = (Model*)malloc(sizeof(Model) * out->model_count);

    out->instance_transforms = (glm::mat4*)malloc(sizeof(glm::mat4) * total_instance_count);

    out->external_names = (Str*)malloc(sizeof(Str) * out->external_count);
    out->external = (uint32_t*)malloc(sizeof(uint32_t) * out->model_count);

    out->name_data = (char*)malloc(sizeof(char) * names_size);

    uint32_t external_offset = 0;
    uint32_t name_offset = 0;
    uint32_t instance_offset = 0;
    for (std::size_t i = 0; i < out->model_count; i++) {
        uint8_t is_external;
        std::memcpy(&is_external, data + offset, sizeof(uint8_t));
        offset += sizeof(uint8_t);
        assert(offset < size);

        uint32_t name_size;
        std::memcpy(&name_size, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        assert(offset < size);

        out->model_names[i] = Str{
            .start = name_offset,
            .length = name_size,
        };
        if (is_external != 0) {
            out->external_names[external_offset] = out->model_names[i];
            out->external[external_offset++] = i;
        }

        if (name_size != 0) {
            std::memcpy(&out->name_data[name_offset], data + offset, sizeof(char) * name_size);
            offset += sizeof(char) * name_size;
            assert(offset < size);
        }
        name_offset += name_size;

        uint32_t instance_count;
        std::memcpy(&instance_count, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        assert(offset < size);

        std::memcpy(&out->instance_transforms[instance_offset], data + offset, sizeof(glm::mat4) * instance_count);
        offset += sizeof(glm::mat4) * instance_count;
        assert(offset < size);

        out->model_instances[i] = Instances{
            .start = instance_offset,
            .count = instance_count,
        };
        instance_offset += instance_count;

        uint8_t model_type;
        std::memcpy(&model_type, data + offset, sizeof(uint8_t));
        offset += sizeof(uint8_t);
        assert(offset < size);

        uint32_t data_size;
        std::memcpy(&data_size, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        assert(offset < size);

        if (model_type == 0) {
            assert(Model::load_optimized(&out->models[i], data));
        } else if (model_type == 1) {
            uint32_t file_size;
            auto file = engine::util::read_file((const char*)(data + offset), &file_size);
            assert(file != nullptr);

            std::filesystem::path path{(const char*)data};
            auto res = Model::load_glb(&out->models[i], {file, file_size}, path.parent_path());
            assert(res == Model::Ok);

            free(file);
        } else if (model_type == 2) {
            uint32_t file_size;
            auto file = engine::util::read_file((const char*)(data + offset), &file_size);
            assert(file != nullptr);

            std::filesystem::path path{(const char*)data};
            auto res = Model::load_gltf(&out->models[i], {file, file_size}, path.parent_path());
            assert(res == Model::Ok);

            free(file);
        }
        offset += data_size;
        assert(offset <= size);
    }
}

void engine::Scene::destroy() {
    for (std::size_t i = 0; i < model_count; i++) {
        models[i].destroy();
    }

    free(model_names);
    free(model_instances);
    free(models);

    free(instance_transforms);

    free(external_names);
    free(external);

    free(name_data);
}

void instance_transforms_upload_func(uint8_t* out, uint32_t start, uint32_t size, uint32_t* texture_gids,
                                     uint32_t texture_gid_count, void* ctx) {
    std::memcpy(out, ctx, size);
}

void engine::GPUScene::upload(engine::GPUScene* out, const Scene* scene, VkBufferMemoryBarrier2* barrier) {
    std::tuple<GPUModel, uint32_t, uint32_t>* gpu_models = (std::tuple<GPUModel, uint32_t, uint32_t>*)malloc(
        sizeof(std::tuple<GPUModel, uint32_t, uint32_t>) * scene->model_count);

    uint32_t total_mesh_count = 0;
    for (std::size_t i = 0; i < scene->model_count; i++) {
        std::get<0>(gpu_models[i]) = model::upload_raw(&scene->models[i]);
        std::get<1>(gpu_models[i]) =
            gpu_group::upload(0, scene->model_instances[i].count * sizeof(glm::mat4), instance_transforms_upload_func,
                              &scene->instance_transforms[scene->model_instances[i].start]);
        std::get<2>(gpu_models[i]) = scene->model_instances[i].count;

        total_mesh_count += std::get<0>(gpu_models[i]).mesh_count;
    }

    out->draw_indirect =
        Buffer::create(total_mesh_count * sizeof(DrawCommand),
                       VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);
    out->draw_count = total_mesh_count;

    uint32_t mesh_offset = 0;
    DrawCommand* data = (DrawCommand*)malloc(total_mesh_count * sizeof(DrawCommand));
    for (uint32_t i = 0; i < scene->model_count; i++) {
        auto [gpu_model, instance_transforms_offset, instance_count] = gpu_models[i];
        for (uint32_t j = 0; j < gpu_model.mesh_count; j++) {
            auto& mesh = scene->models[i].meshes[scene->models[i].mesh_indexes[j]];
            data[mesh_offset] = DrawCommand{
                .cmd =
                    VkDrawIndirectCommand{
                        .vertexCount = mesh.indices != nullptr ? mesh.index_count : mesh.vertex_count,
                        .instanceCount = instance_count,
                        .firstVertex = 0,
                        .firstInstance = 0,
                    },
                .start_offset = gpu_model.data_start + j * (uint32_t)sizeof(model::GPUMeshData),
                .instance_transform_start = instance_transforms_offset,
            };

            mesh_offset++;
        }
    }

    transport::upload(barrier, data, out->draw_indirect.size(), out->draw_indirect.data(), 0);
    free(data);
    free(gpu_models);
}

void engine::GPUScene::destroy() {
    draw_indirect.destroy();
}
