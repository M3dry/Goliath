#pragma once

#include <optional>
#include <utility>
#include <volk.h>

#include <vk_mem_alloc.h>

namespace engine {
    class Buffer {
      public:
        operator VkBuffer() {
            return _buf;
        }

        VkDeviceSize address() const {
            return _address;
        }

        VkBuffer data() {
            return _buf;
        }

        VkDeviceSize size() const {
            return _size;
        }

        VmaAllocation allocation() const {
            return _allocation;
        }

        void flush_mapped(uint32_t start, uint32_t size);

        static Buffer create(uint32_t size, VkBufferUsageFlags usage, std::optional<std::pair<void**, bool*>> host, VmaAllocationCreateFlags alloc_flags = 0);
        void destroy();

      private:
        VkDeviceSize _address;
        VkBuffer _buf;
        VkDeviceSize _size;
        VmaAllocation _allocation;
    };
}
