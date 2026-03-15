#pragma once

#include "goliath/model.hpp"
#include "goliath/models.hpp"

namespace gltf {
    using ImageFn = const std::function<engine::Textures::gid(std::span<uint8_t>, int, int, VkFormat, const std::string&, engine::Sampler)>&;

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
        InvalidFormat,
    };

    struct AddError {
        engine::models::gid model;
        Err loader;
        std::string tinygltf_warning;
        std::string tinygltf_error;

        std::string model_name;
        std::string model_src_file;
    };

    [[nodiscard]] Err load_json(engine::Model* out, std::span<uint8_t> data, const std::string& base_dir, ImageFn image_fn,
                  std::string* tinygltf_error = nullptr, std::string* tinygltf_warning = nullptr);

    [[nodiscard]] Err load_bin(engine::Model* out, std::span<uint8_t> data, const std::string& base_dir, ImageFn image_fn,
                 std::string* tinygltf_error = nullptr, std::string* tinygltf_warning = nullptr);

    engine::models::gid add_model(const std::filesystem::path& path, const std::string& name);
}
