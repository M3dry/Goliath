#include "goliath/vma_ptrs.hpp"
#include "vma_ptrs_.hpp"
#include "goliath/engine.hpp"

namespace engine::vma_ptrs {
    struct State {
        decltype(vmaCreateBuffer)* create_buffer;
        decltype(vmaCreateImage)* create_image;
        decltype(vmaDestroyBuffer)* destroy_buffer;
        decltype(vmaDestroyImage)* destroy_image;
        decltype(vmaFlushAllocation)* flush_alloc;
        decltype(vmaSetAllocationName)* set_name;
        decltype(vmaGetMemoryTypeProperties)* get_memory_type_properties;
    };

    State* state;

    void init() {
        state = new State{};
        state->create_buffer = vmaCreateBuffer;
        state->create_image = vmaCreateImage;
        state->destroy_buffer = vmaDestroyBuffer;
        state->destroy_image = vmaDestroyImage;
        state->flush_alloc = vmaFlushAllocation;
        state->set_name = vmaSetAllocationName;
        state->get_memory_type_properties = vmaGetMemoryTypeProperties;
    }

    void destroy() {
        delete state;
    }

    void create_buffer(const VkBufferCreateInfo* buf_create_info, const VmaAllocationCreateInfo* alloc_create_info,
                       VkBuffer* buf, VmaAllocation* alloc, VmaAllocationInfo* alloc_info) {
        VK_CHECK(state->create_buffer(allocator(), buf_create_info, alloc_create_info, buf, alloc, alloc_info));
    }

    void create_image(const VkImageCreateInfo* image_create_info, const VmaAllocationCreateInfo* alloc_create_info,
                      VkImage* img, VmaAllocation* alloc, VmaAllocationInfo* alloc_info) {
        VK_CHECK(state->create_image(allocator(), image_create_info, alloc_create_info, img, alloc, alloc_info));
    }

    void destroy_buffer(VkBuffer buf, VmaAllocation alloc) {
        state->destroy_buffer(allocator(), buf, alloc);
    }

    void destroy_image(VkImage img, VmaAllocation alloc) {
        state->destroy_image(allocator(), img, alloc);
    }

    void flush_alloc(VmaAllocation alloc, VkDeviceSize offset, VkDeviceSize size) {
        VK_CHECK(state->flush_alloc(allocator(), alloc, offset, size));
    }

    void set_name(VmaAllocation alloc, const char* name) {
        state->set_name(allocator(), alloc, name);
    }

    void get_memory_type_properties(uint32_t mem_type, VkMemoryPropertyFlags* flags) {
        state->get_memory_type_properties(allocator(), mem_type, flags);
    }

    void* get_internal_state() {
        return state;
    }

    void set_internal_state(void* s) {
        state = (State*)s;
    }
}
