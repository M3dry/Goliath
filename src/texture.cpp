#include "goliath/texture.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/transport2.hpp"
#include <vulkan/vulkan_core.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION

#include <volk.h>

namespace engine {
    Image Image::load8(const char* filename, uint32_t channel_count) {
        Image image;
        image.type = _8;
        image.components = channel_count;
        image.data = stbi_load(filename, (int*)&image.width, (int*)&image.height,
                               channel_count == 0 ? (int*)&image.components : nullptr, (int)channel_count);
        image.calculate_size();
        image.get_format();
        return image;
    }

    Image Image::load8(uint8_t* mem, uint32_t size, uint32_t channel_count) {
        Image image;
        image.type = _8;
        image.components = channel_count;
        image.data = stbi_load_from_memory(mem, (int)size, (int*)&image.width, (int*)&image.height,
                                           channel_count == 0 ? (int*)&image.components : nullptr, (int)channel_count);
        image.calculate_size();
        image.get_format();
        return image;
    }

    Image Image::load16(const char* filename, uint32_t channel_count) {
        Image image;
        image.type = _16;
        image.components = channel_count;
        image.data = stbi_load_16(filename, (int*)&image.width, (int*)&image.height,
                                  channel_count == 0 ? (int*)&image.components : nullptr, (int)channel_count);
        image.calculate_size();
        image.get_format();
        return image;
    }

    Image Image::load16(uint8_t* mem, uint32_t size, uint32_t channel_count) {
        Image image;
        image.type = _16;
        image.components = channel_count;
        image.data =
            stbi_load_16_from_memory(mem, (int)size, (int*)&image.width, (int*)&image.height,
                                     channel_count == 0 ? (int*)&image.components : nullptr, (int)channel_count);
        image.calculate_size();
        image.get_format();
        return image;
    }

    void Image::destroy() {
        stbi_image_free(data);
    }

    void Image::calculate_size() {
        size = components * width * height * (type == _8 ? 1 : 2);
    }

    void Image::get_format() {
        if (type == _8) {
            switch (components) {
                case 1: format = VK_FORMAT_R8_UNORM; break;
                case 2: format = VK_FORMAT_R8G8_UNORM; break;
                case 3: format = VK_FORMAT_R8G8B8_UNORM; break;
                case 4: format = VK_FORMAT_R8G8B8A8_UNORM; break;
            }
        } else if (type == _16) {
            switch (components) {
                case 1: format = VK_FORMAT_R16_UNORM; break;
                case 2: format = VK_FORMAT_R16G16_UNORM; break;
                case 3: format = VK_FORMAT_R16G16B16_UNORM; break;
                case 4: format = VK_FORMAT_R16G16B16A16_UNORM; break;
            }
        }
    }

    VkImageMemoryBarrier2 GPUImage::transition(std::optional<VkImageLayout> new_layout, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = current_stage,
            .srcAccessMask = current_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = current_layout,
            .newLayout = new_layout ? *new_layout : current_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange =
                VkImageSubresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        current_layout = new_layout ? *new_layout : current_layout;
        current_stage = dst_stage;
        current_access = dst_access;

        return barrier;
    }

    GPUImageView::GPUImageView(const GPUImage& img) {
        _info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        _info.pNext = nullptr;
        _info.image = img.image;
        _info.format = img.format;
        _info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        _info.components = VkComponentMapping{
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        };
        _info.subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
    }

    GPUImageView::GPUImageView(VkImage img, VkFormat format) {
        _info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        _info.pNext = nullptr;
        _info.image = img;
        _info.format = format;
        _info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        _info.components = VkComponentMapping{
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        };
        _info.subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
    }
}

namespace engine::gpu_image {
    GPUImage upload(bool priority, bool own, const char* name, const Image& img, VkImageLayout new_layout,
                    transport2::ticket& ticket, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        GPUImage gpu_img;

        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.pNext = nullptr;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.arrayLayers = 1;
        info.extent = VkExtent3D{
            .width = img.width,
            .height = img.height,
            .depth = 1,
        };
        info.format = img.format;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.mipLevels = 1;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VK_CHECK(vmaCreateImage(allocator, &info, &alloc_info, &gpu_img.image, &gpu_img.allocation, nullptr));

        vmaSetAllocationName(allocator, gpu_img.allocation, name);

        VkDebugUtilsObjectNameInfoEXT name_info{};
        name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        name_info.pNext = nullptr;
        name_info.objectType = VK_OBJECT_TYPE_IMAGE;
        name_info.objectHandle = (uint64_t)gpu_img.image;
        name_info.pObjectName = name;

        vkSetDebugUtilsObjectNameEXT(device, &name_info);

        ticket = transport2::upload(false, img.format,
                                    VkExtent3D{
                                        .width = img.width,
                                        .height = img.height,
                                        .depth = 1,
                                    },
                                    img.data, own ? std::make_optional(stbi_image_free) : std::nullopt, gpu_img.image,
                                    VkImageSubresourceLayers{
                                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .mipLevel = 0,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                                    VkOffset3D{.x = 0, .y = 0, .z = 0}, VK_IMAGE_LAYOUT_UNDEFINED, new_layout, dst_stage, dst_access);

        gpu_img.current_layout = new_layout;
        gpu_img.current_stage = dst_stage;
        gpu_img.current_access = dst_access;

        gpu_img.format = img.format;
        return gpu_img;
    }

    GPUImage upload(const char* name, const GPUImageInfo& builder, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        GPUImage gpu_img;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VK_CHECK(
            vmaCreateImage(allocator, &builder._image_info, &alloc_info, &gpu_img.image, &gpu_img.allocation, nullptr));

        vmaSetAllocationName(allocator, gpu_img.allocation, name);

        if (builder._img_data != nullptr) {
            *builder._ticket = transport2::upload(builder._priority, builder._image_info.format,
                                                  VkExtent3D{
                                                      .width = builder._width,
                                                      .height = builder._height,
                                                      .depth = 1,
                                                  },
                                                  builder._img_data, builder._own_data, gpu_img.image,
                                                  VkImageSubresourceLayers{
                                                      .aspectMask = builder._aspect_mask,
                                                      .mipLevel = 0,
                                                      .baseArrayLayer = 0,
                                                      .layerCount = 1,
                                                  },
                                                  VkOffset3D{
                                                      .x = 0,
                                                      .y = 0,
                                                      .z = 0,
                                                  },
                                                  VK_IMAGE_LAYOUT_UNDEFINED, builder._new_image_layout, dst_stage, dst_access);
        } else {
            synchronization::begin_barriers();
            synchronization::apply_barrier(VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = dst_stage,
                .dstAccessMask = dst_access,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = builder._new_image_layout,
                .image = gpu_img.image,
                .subresourceRange =
                    VkImageSubresourceRange{
                        .aspectMask = builder._aspect_mask,
                        .baseMipLevel = 0,
                        .levelCount = VK_REMAINING_MIP_LEVELS,
                        .baseArrayLayer = 0,
                        .layerCount = VK_REMAINING_ARRAY_LAYERS,
                    },
            });
            synchronization::end_barriers();
        }
        gpu_img.current_layout = builder._new_image_layout;
        gpu_img.current_stage = dst_stage;
        gpu_img.current_access = dst_access;

        gpu_img.format = builder._image_info.format;

        return gpu_img;
    }

    void destroy(GPUImage& gpu_image) {
        destroy_image(gpu_image.image, gpu_image.allocation);
    }
}

namespace engine::gpu_image_view {
    VkImageView create(const GPUImageView& gpu_image_view) {
        VkImageView view;
        vkCreateImageView(device, &gpu_image_view._info, nullptr, &view);
        return view;
    }

    void destroy(VkImageView view) {
        destroy_view(view);
    }
}
