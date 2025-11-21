#include "goliath/gpu_group.hpp"
#include "goliath/transport.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

void engine::GPUGroup::destroy() {
    data.destroy();
}

namespace engine::gpu_group {
    struct UploadFunc {
        void(*f)(uint8_t*, uint32_t, uint32_t, void*);
        void* ctx;
        uint32_t start;
        uint32_t size;

        uint8_t* operator()(uint8_t* data) const {
            f(data, start, size, ctx);
            return data + size;
        }
    };
    std::vector<UploadFunc> upload_ptrs{};
    uint32_t needed_data_size = 0;

    void begin() {
        upload_ptrs.clear();
        needed_data_size = 0;
    }

    uint32_t upload( uint32_t data_size, void(*upload_ptr)(uint8_t*, uint32_t, uint32_t, void*), void* ctx) {
        upload_ptrs.emplace_back(UploadFunc{upload_ptr, ctx, needed_data_size, data_size});

        needed_data_size += data_size;

        return needed_data_size - data_size;
    }

    GPUGroup end(VkBufferMemoryBarrier2* barrier, VkBufferUsageFlags usage_flags) {
        if (needed_data_size == 0) return GPUGroup{};

        auto group = GPUGroup{
            .data = Buffer::create(needed_data_size, VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | usage_flags, std::nullopt),
        };

        uint8_t* data = (uint8_t*)malloc(needed_data_size);

        uint8_t* start_of_data = data;
        for (const auto& upload_func : upload_ptrs) {
            data = upload_func(data);
        }

        transport::upload(barrier, start_of_data, needed_data_size, group.data.data(), 0);

        return group;
    }
}
