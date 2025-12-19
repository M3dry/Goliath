#include "goliath/util.hpp"
#include <cassert>
#include <fstream>

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
