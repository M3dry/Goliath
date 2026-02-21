#pragma once

#include <volk.h>

#include <vk_mem_alloc.h>

namespace engine::vma_ptrs {
    void create_buffer(const VkBufferCreateInfo* buf_create_info, const VmaAllocationCreateInfo* alloc_create_info,
                       VkBuffer* buf, VmaAllocation* alloc, VmaAllocationInfo* alloc_info);
    void create_image(const VkImageCreateInfo* image_create_info, const VmaAllocationCreateInfo* alloc_create_info,
                      VkImage* img, VmaAllocation* alloc, VmaAllocationInfo* alloc_info);

    void destroy_buffer(VkBuffer buf, VmaAllocation alloc);
    void destroy_image(VkImage img, VmaAllocation alloc);

    void flush_alloc(VmaAllocation alloc, VkDeviceSize offset, VkDeviceSize size);
    void set_name(VmaAllocation alloc, const char* name);
    void get_memory_type_properties(uint32_t mem_type, VkMemoryPropertyFlags* flags);

    void* get_internal_state();
    void set_internal_state(void* s);
}
