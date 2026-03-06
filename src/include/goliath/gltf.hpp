#pragma once

#include "goliath/model.hpp"

namespace engine::gltf {
    using ImageFn = const std::function<Textures::gid(std::span<uint8_t>, int, int, VkFormat, const std::string&, Sampler)>&;

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

    Err load_json(engine::Model* out, std::span<uint8_t> data, const std::string& base_dir, ImageFn image_fn,
                  std::string* tinygltf_error = nullptr, std::string* tinygltf_warning = nullptr);

    Err load_bin(engine::Model* out, std::span<uint8_t> data, const std::string& base_dir, ImageFn image_fn,
                 std::string* tinygltf_error = nullptr, std::string* tinygltf_warning = nullptr);
}
