#include "goliath/buffer.hpp"
#include "goliath/engine.hpp"

namespace engine {
    Buffer Buffer::create(uint32_t size, VkBufferUsageFlags usage, bool host, VmaAllocationCreateFlags alloc_flags) {
        Buffer buf{};

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.pNext = nullptr;
        buffer_info.queueFamilyIndexCount = 1;
        buffer_info.pQueueFamilyIndices = &graphics_queue_family;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_info.size = size;
        buffer_info.usage = usage;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = host ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        alloc_info.flags = alloc_flags;

        VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buf._buf, &buf._allocation, nullptr));

        VkBufferDeviceAddressInfo address_info{};
        address_info.buffer = buf._buf;
        address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        buf._address = vkGetBufferDeviceAddress(device, &address_info);

        return buf;
    }

    void Buffer::destroy() {
        vmaDestroyBuffer(allocator, _buf, _allocation);
    }
}
