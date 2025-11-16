#pragma once

#include <cmath>
#include <volk.h>

namespace engine {
    enum struct CompareOp {
        Never = VK_COMPARE_OP_NEVER,
        Always = VK_COMPARE_OP_ALWAYS,
        Equal = VK_COMPARE_OP_EQUAL,
        NotEqual = VK_COMPARE_OP_NOT_EQUAL,
        Greater = VK_COMPARE_OP_GREATER,
        Less = VK_COMPARE_OP_LESS,
        GreaterOrEqual = VK_COMPARE_OP_GREATER_OR_EQUAL,
        LessOrEqual = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

}

namespace engine::util {
    uint8_t* read_file(const char* path, uint32_t* size);
    void save_file(const char* path, uint8_t* data, uint32_t size);

    // `alignment` needs to be a power of two
    uint32_t constexpr align_up(uint32_t alignment, uint32_t size) {
        return (size + alignment - 1) & ~(alignment - 1);
    }
}
