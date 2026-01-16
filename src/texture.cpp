#include "goliath/texture.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"
#include "goliath/transport.hpp"
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

    std::pair<GPUImage, VkImageMemoryBarrier2> GPUImage::upload(const char* name, const Image& img, VkImageLayout new_layout) {
        GPUImage gpu_img;
        VkImageMemoryBarrier2 barrier{};

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

        printf("%s @%p\n", name, gpu_img.image);
        fflush(stdout);

        // VkDebugMarkerObjectNameInfoEXT name_info{};
        // name_info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
        // name_info.pNext = nullptr;
        // name_info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
        // name_info.pObjectName = name.c_str();
        // name_info.object = (uint64_t)gpu_img.image;
        //
        // vkDebugMarkerSetObjectNameEXT(device, &name_info);

        barrier.dstQueueFamilyIndex = graphics_queue_family;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = new_layout;
        transport::upload(&barrier, img.data, (uint32_t)img.size, img.width, img.height, img.format, gpu_img.image);

        gpu_img.layout = new_layout;
        gpu_img.format = img.format;
        return {gpu_img, barrier};
    }

    std::pair<GPUImage, VkImageMemoryBarrier2> GPUImage::upload(const char* name, const GPUImageInfo& builder) {
        GPUImage gpu_img;
        VkImageMemoryBarrier2 barrier{};

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VK_CHECK(
            vmaCreateImage(allocator, &builder._image_info, &alloc_info, &gpu_img.image, &gpu_img.allocation, nullptr));

        vmaSetAllocationName(allocator, gpu_img.allocation,
                             name);

        printf("%s @%p\n", name, gpu_img.image);
        fflush(stdout);

        barrier.dstQueueFamilyIndex = graphics_queue_family;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = builder._new_image_layout;
        if (builder._img_data != nullptr) {
            transport::upload(&barrier, builder._img_data, builder._size, builder._width, builder._height,
                              builder._image_info.format, gpu_img.image);
            // barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // NOTE: idk if this should be here, but validation errors
            // disappear thanks to this
        } else {
            barrier.image = gpu_img.image;
            barrier.dstQueueFamilyIndex = graphics_queue_family;
            transport::transition(&barrier, builder._aspect_mask);
        }
        gpu_img.layout = builder._new_image_layout;
        gpu_img.format = builder._image_info.format;

        return {gpu_img, barrier};
    }

    void GPUImage::destroy() {
        destroy_image(image, allocation);
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

    VkImageView GPUImageView::create() {
        VkImageView view;
        vkCreateImageView(device, &_info, nullptr, &view);
        return view;
    }

    void GPUImageView::destroy(VkImageView view) {
        destroy_view(view);
    }
}
