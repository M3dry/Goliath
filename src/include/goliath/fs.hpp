#pragma once

#include <filesystem>
namespace engine::fs {
    std::filesystem::path get_runtime_dir();

    inline std::filesystem::path runtime_file(std::filesystem::path file) {
        return get_runtime_dir() / file;
    }
}
