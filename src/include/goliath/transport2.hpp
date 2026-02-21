#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <volk.h>

namespace engine::transport2 {
    struct ticket {
        uint64_t value;

        static constexpr uint64_t id_mask =  0x00000000FFFFFFFFu;
        static constexpr uint64_t gen_mask = 0xFFFFFFFF00000000u;
        static constexpr uint64_t gen_shift = 32;

        ticket() : value(-1) {}
        ticket(uint64_t generation, uint64_t id) : value((id & id_mask) | ((generation & 0xFFFFFFFFu) << gen_shift)) {}

        uint32_t id() const {
            return value & id_mask;
        }

        uint32_t gen() const {
            return (value & gen_mask) >> gen_shift;
        }

        bool operator==(ticket other) const {
            return value == other.value;
        }
    };

    bool is_ready(ticket ticket);

    VkSemaphoreSubmitInfo wait_on(std::span<ticket> tickets);

    using FreeFn = void(void*);

    ticket upload(bool priority, void* src, std::optional<FreeFn*> own, uint32_t size, VkBuffer dst, uint32_t dst_offset, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

    ticket upload(bool priority, VkFormat format, VkExtent3D dimension, void* src, std::optional<FreeFn*> own, VkImage dst,
                  VkImageSubresourceLayers dst_layers, VkOffset3D dst_offset, VkImageLayout current_layout, VkImageLayout dst_layout, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

    void unqueue(ticket t, bool free = false);

    uint64_t get_timeline();
    uint64_t get_timeline(ticket t);

    void* get_internal_state();
    void set_internal_state(void* s);
}
