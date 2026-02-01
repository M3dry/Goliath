#pragma once

#include "goliath/transport2.hpp"
#include <utility>
#include <vector>

#include <vk_mem_alloc.h>
#include <volk.h>

namespace engine {
    class Image {
      public:
        enum Type {
            _8,
            _16,
        };

        void* data = nullptr;
        std::size_t size = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t components = 0;
        Type type;
        VkFormat format;

        static Image load8(const char* filename, uint32_t channel_count = 0);
        static Image load8(uint8_t* mem, uint32_t size, uint32_t channel_count = 0);
        static Image load16(const char* filename, uint32_t channel_count = 0);
        static Image load16(uint8_t* mem, uint32_t size, uint32_t channel_count = 0);

        void destroy();

      private:
        void calculate_size();
        void get_format();
    };

    struct GPUImageInfo {
        VkImageCreateInfo _image_info{};
        void* _img_data = nullptr;
        std::optional<transport2::FreeFn*> _own_data = std::nullopt;
        bool _priority = false;
        transport2::ticket* _ticket;
        uint32_t _width = 0;
        uint32_t _height = 0;
        uint32_t _size = 0;
        VkImageLayout _new_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags _aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;

        GPUImageInfo() {
            _image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            _image_info.pNext = nullptr;
            _image_info.imageType = VK_IMAGE_TYPE_2D;
            _image_info.arrayLayers = 1;
            _image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            _image_info.mipLevels = 1;
            _image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            _image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            _image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            _image_info.samples = VK_SAMPLE_COUNT_1_BIT;
            _image_info.extent.depth = 1;
        };

        GPUImageInfo&& image_type(VkImageType type) {
            _image_info.imageType = type;
            return std::move(*this);
        }

        GPUImageInfo&& layer_count(uint32_t count) {
            _image_info.arrayLayers = count;
            return std::move(*this);
        }

        GPUImageInfo&& mip_levels(uint32_t level_count) {
            _image_info.mipLevels = level_count;
            return std::move(*this);
        }

        GPUImageInfo&& sharing_mode(VkSharingMode mode) {
            _image_info.sharingMode = mode;
            return std::move(*this);
        }

        GPUImageInfo&& usage(VkImageUsageFlags flags) {
            _image_info.usage = flags;
            return std::move(*this);
        }

        GPUImageInfo&& tiling(VkImageTiling tile) {
            _image_info.tiling = tile;
            return std::move(*this);
        }

        GPUImageInfo&& samples(VkSampleCountFlagBits sample_count) {
            _image_info.samples = sample_count;
            return std::move(*this);
        }

        GPUImageInfo&& format(VkFormat fmt) {
            _image_info.format = fmt;
            return std::move(*this);
        }

        // `families` needs to live until GPUImage creation
        GPUImageInfo&& queue_families(std::vector<uint32_t> families) {
            _image_info.queueFamilyIndexCount = (uint32_t)families.size();
            _image_info.pQueueFamilyIndices = families.data();
            return std::move(*this);
        }

        GPUImageInfo&& image(const Image& img, std::optional<transport2::FreeFn*> own, transport2::ticket& ticket,
                             bool priority) {
            _priority = priority;
            _ticket = &ticket;
            _img_data = img.data;
            _own_data = own;
            _width = img.width;
            _height = img.height;
            _image_info.format = img.format;
            return std::move(*this);
        }

        GPUImageInfo&& data(void* ptr, std::optional<transport2::FreeFn*> own, transport2::ticket& ticket,
                            bool priority) {
            _priority = priority;
            _ticket = &ticket;
            _img_data = ptr;
            _own_data = own;
            return std::move(*this);
        }

        GPUImageInfo&& width(uint32_t width) {
            _width = width;
            _image_info.extent.width = width;
            return std::move(*this);
        }

        GPUImageInfo&& height(uint32_t height) {
            _height = height;
            _image_info.extent.height = height;
            return std::move(*this);
        }

        GPUImageInfo&& depth(uint32_t depth) {
            _image_info.extent.depth = depth;
            return std::move(*this);
        }

        GPUImageInfo&& size(uint32_t val) {
            _size = val;
            return std::move(*this);
        }

        GPUImageInfo&& new_layout(VkImageLayout layout) {
            _new_image_layout = layout;
            return std::move(*this);
        }

        GPUImageInfo&& aspect_mask(VkImageAspectFlags flags) {
            _aspect_mask = flags;
            return std::move(*this);
        }
    };

    struct GPUImage {
        VkImage image;
        VmaAllocation allocation;
        VkImageLayout layout;
        VkFormat format;
    };

    struct GPUImageView {
        VkImageViewCreateInfo _info{};

        GPUImageView(const GPUImage& img);
        GPUImageView(VkImage img, VkFormat format);

        GPUImageView&& image(VkImage img) {
            _info.image = img;
            return std::move(*this);
        }

        GPUImageView&& format(VkFormat fmt) {
            _info.format = fmt;
            return std::move(*this);
        }

        GPUImageView&& view_type(VkImageViewType type) {
            _info.viewType = type;
            return std::move(*this);
        }

        GPUImageView&& component_r(VkComponentSwizzle swizzle) {
            _info.components.r = swizzle;
            return std::move(*this);
        }

        GPUImageView&& component_g(VkComponentSwizzle swizzle) {
            _info.components.g = swizzle;
            return std::move(*this);
        }

        GPUImageView&& component_b(VkComponentSwizzle swizzle) {
            _info.components.b = swizzle;
            return std::move(*this);
        }

        GPUImageView&& component_a(VkComponentSwizzle swizzle) {
            _info.components.a = swizzle;
            return std::move(*this);
        }

        GPUImageView&& aspect_mask(VkImageAspectFlags mask) {
            _info.subresourceRange.aspectMask = mask;
            return std::move(*this);
        }

        GPUImageView&& base_mip_level(uint32_t lvl) {
            _info.subresourceRange.baseMipLevel = lvl;
            return std::move(*this);
        }

        GPUImageView&& level_count(uint32_t count) {
            _info.subresourceRange.levelCount = count;
            return std::move(*this);
        }

        GPUImageView&& base_array_layer(uint32_t layer) {
            _info.subresourceRange.baseArrayLayer = layer;
            return std::move(*this);
        }

        GPUImageView&& layer_count(uint32_t count) {
            _info.subresourceRange.layerCount = count;
            return std::move(*this);
        }
    };
}

namespace engine::gpu_image {
    GPUImage upload(bool priority, bool own, const char* name, const Image& img, VkImageLayout new_layout,
                    transport2::ticket& ticket, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);
    GPUImage upload(const char* name, const GPUImageInfo& builder, VkPipelineStageFlags2 dst_stage,
                    VkAccessFlags2 dst_access);

    void destroy(const GPUImage& gpu_image);
}

namespace engine::gpu_image_view {
    VkImageView create(const GPUImageView& gpu_image_view);
    void destroy(VkImageView view);
}
