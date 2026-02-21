#include "goliath/util.hpp"
#include "goliath/engine.hpp"
#include <cassert>
#include <expected>
#include <fstream>

namespace engine::rendering {
    bool in_block = false;
    bool close = false;

    void begin_mark_block() {
        assert(!in_block);
        in_block = true;
        close = false;
    }

    void end_mark_block() {
        assert(in_block);
        if (close) {
            vkCmdEndDebugUtilsLabelEXT(get_cmd_buf());
            close = false;
        }
        in_block = false;
    }

    void mark(const char* name) {
        assert(in_block);

        if (close) {
            vkCmdEndDebugUtilsLabelEXT(get_cmd_buf());
            close = false;
        }
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        label.color[0] = 1.0f;
        label.color[1] = 1.0f;
        label.color[2] = 1.0f;
        label.color[3] = 1.0f;

        vkCmdBeginDebugUtilsLabelEXT(get_cmd_buf(), &label);
        close = true;
    }
}

uint8_t* engine::util::read_file(const std::filesystem::path& path, uint32_t* size) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};

    auto size_signed = file.tellg();
    if (size_signed < 0) {
        *size = (uint32_t)-1;
        return nullptr;
    }

    *size = (uint32_t)size_signed;

    void* data = malloc(*size);

    file.seekg(0, std::ios::beg);
    file.read((char*)data, size_signed);

    return (uint8_t*)data;
}

void engine::util::save_file(const std::filesystem::path& path, uint8_t* data, uint32_t size) {
    std::ofstream file{path, std::ios::binary};
    file.write((const char*)data, size);
}

std::expected<nlohmann::json, engine::util::ReadJsonErr> engine::util::read_json(const std::filesystem::path& path) {
    std::ifstream i{path};
    if (!i) return std::unexpected(ReadJsonErr::FileErr);

    try {
        return nlohmann::json::parse(i);
    } catch (const nlohmann::json::parse_error& e) {
        return std::unexpected(ReadJsonErr::ParseErr);
    }
}
