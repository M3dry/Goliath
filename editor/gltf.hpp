#pragma once

#include "goliath/model.hpp"

namespace gltf {
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

    Err load_json(engine::Model* out, std::span<uint8_t> data, const std::string& base_dir,
                  std::string* tinygltf_error = nullptr, std::string* tinygltf_warning = nullptr);

    Err load_bin(engine::Model* out, std::span<uint8_t> data, const std::string& base_dir,
                 std::string* tinygltf_error = nullptr, std::string* tinygltf_warning = nullptr);
}
