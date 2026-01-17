#include "goliath/buffer.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"
#include <vulkan/vulkan_core.h>

namespace engine {
    void Buffer::flush_mapped(uint32_t start, uint32_t size) {
        vmaFlushAllocation(allocator, _allocation, start, size);
    }

    Buffer Buffer::create(const char* name, uint32_t size, VkBufferUsageFlags usage, std::optional<std::pair<void**, bool*>> host, VmaAllocationCreateFlags alloc_flags) {
        Buffer buf{};

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.pNext = nullptr;
        buffer_info.queueFamilyIndexCount = 1;
        buffer_info.pQueueFamilyIndices = &graphics_queue_family;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_info.size = size;
        buffer_info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = host ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        alloc_info.flags = alloc_flags | (host ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT : 0);

        VmaAllocationInfo out_alloc_info;
        VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buf._buf, &buf._allocation, &out_alloc_info));

        if (host) {
            *host->first = out_alloc_info.pMappedData;
            VkMemoryPropertyFlags props;
            vmaGetMemoryTypeProperties(allocator, out_alloc_info.memoryType, &props);
            *host->second = props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

        VkBufferDeviceAddressInfo address_info{};
        address_info.buffer = buf._buf;
        address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        buf._address = vkGetBufferDeviceAddress(device, &address_info);
        buf._size = size;

        vmaSetAllocationName(allocator, buf._allocation, name);

        VkDebugUtilsObjectNameInfoEXT name_info{};
        name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        name_info.pNext = nullptr;
        name_info.objectType = VK_OBJECT_TYPE_BUFFER;
        name_info.objectHandle = (uint64_t)buf._buf;
        name_info.pObjectName = name;

        vkSetDebugUtilsObjectNameEXT(device, &name_info);

        return buf;
    }

    void Buffer::destroy() {
        destroy_buffer(_buf, _allocation);
    }
}
