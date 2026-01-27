#pragma once

#include <cstdint>
#include <span>
#include <volk.h>

namespace engine::transport2 {
    struct ticket {
        uint32_t value;

        static constexpr uint32_t id_mask = 0x00FF'FFFFu;
        static constexpr uint32_t gen_mask = 0xFF00'0000u;
        static constexpr uint32_t gen_shift = 24;

        ticket() : value(-1) {}
        ticket(uint32_t generation, uint32_t id) : value((id & id_mask) | ((generation & 0xFFu) << gen_shift)) {}

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

    ticket upload(bool priority, void* src, bool own, uint32_t size, VkBuffer dst, uint32_t dst_offset);
}
