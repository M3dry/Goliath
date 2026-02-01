#include "goliath/gpu_group.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <vulkan/vulkan_core.h>

void engine::GPUGroup::destroy() {
    data.destroy();
    textures::release(acquired_texture_gids, acquired_texture_count);
    free(acquired_texture_gids);
}

namespace engine::gpu_group {
    struct UploadFunc {
        void (*f)(uint8_t*, uint32_t, uint32_t, textures::gid*, uint32_t, void*);
        void* ctx;
        uint32_t start;
        uint32_t size;
        uint32_t texture_count;

        std::pair<uint8_t*, textures::gid*> operator()(uint8_t* data, textures::gid* acquired_texture_gids) const {
            f(data, start, size, acquired_texture_gids, texture_count, ctx);
            return {data + size, acquired_texture_gids + texture_count};
        }
    };
    std::vector<UploadFunc> upload_ptrs{};
    uint32_t acquired_texture_count = 0;
    uint32_t needed_data_size = 0;

    void begin() {
        upload_ptrs.clear();
        needed_data_size = 0;
        acquired_texture_count = 0;
    }

    uint32_t upload(uint32_t texture_gid_count, uint32_t data_size,
                    void (*upload_ptr)(uint8_t*, uint32_t, uint32_t, textures::gid*, uint32_t, void*), void* ctx) {
        upload_ptrs.emplace_back(UploadFunc{upload_ptr, ctx, needed_data_size, data_size, texture_gid_count});
        acquired_texture_count += texture_gid_count;

        needed_data_size += data_size;

        return needed_data_size - data_size;
    }

    GPUGroup end(bool priority, VkBufferUsageFlags usage_flags, VkPipelineStageFlags2 stage, VkAccessFlagBits2 access) {
        if (needed_data_size == 0) return GPUGroup{};

        auto group = GPUGroup{
            .data = Buffer::create("GPU group buffer", needed_data_size,
                                   VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | usage_flags, std::nullopt),
            .acquired_texture_count = acquired_texture_count,
            .acquired_texture_gids = (textures::gid*)malloc(acquired_texture_count * sizeof(textures::gid)),
        };

        uint8_t* data = (uint8_t*)malloc(needed_data_size);
        uint8_t* start_of_data = data;
        auto* texture_gids = group.acquired_texture_gids;
        for (const auto& upload_func : upload_ptrs) {
            auto res = upload_func(data, texture_gids);
            data = res.first;
            texture_gids = res.second;
        }
        textures::acquire(group.acquired_texture_gids, group.acquired_texture_count);

        group.ticket = transport2::upload(priority, start_of_data, free, needed_data_size, group.data, 0, stage, access);

        return group;
    }
}
