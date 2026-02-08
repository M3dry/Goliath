#include "goliath/transport.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vulkan/vulkan_core.h>

struct FormatInfo {
    uint32_t blockWidth;
    uint32_t blockHeight;
    uint32_t bytesPerBlock;
};

FormatInfo get_format_info(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM: return {1, 1, 1};
        case VK_FORMAT_R8G8_UNORM: return {1, 1, 2};
        case VK_FORMAT_R8G8B8_UNORM: return {1, 1, 3};
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: return {1, 1, 4};
        case VK_FORMAT_R16_UNORM: return {1, 1, 2};
        case VK_FORMAT_R16G16_UNORM: return {1, 1, 4};
        case VK_FORMAT_R16G16B16_UNORM: return {1, 1, 6};
        case VK_FORMAT_R16G16B16A16_UNORM: return {1, 1, 8};
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return {4, 4, 8};
        case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_R32G32B32_UINT: return {1, 1, 12};
        default: fprintf(stderr, "invalid format: %d", format); assert(false && "Unsupported format");
    }
}
