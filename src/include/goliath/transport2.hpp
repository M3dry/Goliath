#pragma once

#include <cstdint>
#include <volk.h>

namespace engine::transport2 {
    struct ticket {
        uint32_t value;

        static constexpr uint32_t id_mask = 0x00FF'FFFFu;
        static constexpr uint32_t gen_mask = 0xFF00'0000u;
        static constexpr uint32_t gen_shift = 24;

        ticket() : value(0) {}
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

    void init();
    void destroy();

    void tick();

    bool is_ready(ticket ticket);

    // bool is_ready(uint64_t timeline);
    //
    // // takes ownership of `src`
    // uint64_t upload(void* src, bool own, uint32_t size, VkBuffer dst, uint32_t dst_offset);
    // // takes ownership of `src`, assumes that the image is transitioned into VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    // uint64_t upload(void* src, bool own, VkImage dst, VkFormat dst_format, VkExtent3D dst_dimension, VkOffset3D dst_offset,
    //                 VkImageSubresourceLayers dst_layers);
}
