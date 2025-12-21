#pragma once

#include "goliath/util.hpp"
#include <optional>
#include <utility>
#include <vector>
#include <volk.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

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
        uint32_t _width = 0;
        uint32_t _height = 0;
        uint32_t _size = 0;
        VkImageLayout _new_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags _aspect_mask;

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

        GPUImageInfo&& image(const Image& img) {
            _img_data = img.data;
            _width = img.width;
            _height = img.height;
            _image_info.format = img.format;
            return std::move(*this);
        }

        GPUImageInfo&& data(void* ptr) {
            _img_data = ptr;
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

        // `transport::begin` must have been called before these two are called, unless the second function is supplied
        // with `builder._img_data == nulltpr`, then no upload happens the returned barrier needs `dstStageMask` and
        // `dstAccessMask` set by the caller before application
        static std::pair<GPUImage, VkImageMemoryBarrier2> upload(const Image& img, VkImageLayout new_layout);
        static std::pair<GPUImage, VkImageMemoryBarrier2> upload(const GPUImageInfo& builder);

        void destroy();
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

        VkImageView create();

        static void destroy(VkImageView view);
    };

    enum struct AddressMode {
        Repeat = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        MirroredRepeat = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        ClampToBorder = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        ClampToEdge = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        MirrorClampToEdge = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
    };

    enum struct MipMapMode {
        Linear = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        Nearest = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    };

    enum struct FilterMode {
        Linear = VK_FILTER_LINEAR,
        Nearest = VK_FILTER_NEAREST,
    };

    struct Sampler {
        VkSamplerCreateInfo _info{};

        Sampler() {
            _info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            _info.pNext = nullptr;
            _info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            _info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            _info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            _info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            _info.anisotropyEnable = false;
            _info.maxAnisotropy = 0.0f;
            _info.compareEnable = false;
            _info.compareOp = VK_COMPARE_OP_NEVER;
            _info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            _info.minFilter = VK_FILTER_LINEAR;
            _info.magFilter = VK_FILTER_LINEAR;
            _info.unnormalizedCoordinates = false;
            _info.maxLod = 0.0f;
            _info.minLod = 0.0f;
            _info.mipLodBias = 0.0f;
        };

        bool operator==(const Sampler& other) const {
            return _info.sType == other._info.sType && _info.pNext == other._info.pNext &&
                   _info.addressModeU == other._info.addressModeU && _info.addressModeV == other._info.addressModeV &&
                   _info.addressModeW == other._info.addressModeW && _info.mipmapMode == other._info.mipmapMode &&
                   _info.anisotropyEnable == other._info.anisotropyEnable &&
                   (_info.anisotropyEnable && _info.maxAnisotropy == other._info.maxAnisotropy) &&
                   _info.compareEnable == other._info.compareEnable && (_info.compareOp == other._info.compareOp) &&
                   _info.borderColor == other._info.borderColor && _info.minFilter == other._info.minFilter &&
                   _info.magFilter == other._info.magFilter &&
                   _info.unnormalizedCoordinates == other._info.unnormalizedCoordinates &&
                   _info.maxLod == other._info.maxLod && _info.minLod == other._info.minLod &&
                   _info.mipLodBias == other._info.mipLodBias && _info.flags == other._info.flags;
        }

        Sampler&& address_u(AddressMode mode) {
            _info.addressModeU = static_cast<VkSamplerAddressMode>(mode);
            return std::move(*this);
        }

        Sampler&& address_v(AddressMode mode) {
            _info.addressModeV = static_cast<VkSamplerAddressMode>(mode);
            return std::move(*this);
        }

        Sampler&& address_w(AddressMode mode) {
            _info.addressModeW = static_cast<VkSamplerAddressMode>(mode);
            return std::move(*this);
        }

        Sampler&& address(AddressMode mode) {
            return address_u(mode).address_v(mode).address_w(mode);
        }

        Sampler&& mipmap(MipMapMode mode) {
            _info.mipmapMode = static_cast<VkSamplerMipmapMode>(mode);
            return std::move(*this);
        }

        Sampler&& anisotropy(std::optional<float> max_anisotropy) {
            if (max_anisotropy) {
                _info.anisotropyEnable = true;
                _info.maxAnisotropy = *max_anisotropy;
            } else {
                _info.anisotropyEnable = false;
            }

            return std::move(*this);
        }

        Sampler&& compare(std::optional<CompareOp> compare_op) {
            if (compare_op) {
                _info.compareEnable = true;
                _info.compareOp = static_cast<VkCompareOp>(*compare_op);
            } else {
                _info.compareEnable = false;
            }

            return std::move(*this);
        }

        Sampler&& border_color(VkBorderColor color) {
            _info.borderColor = color;
            return std::move(*this);
        }

        Sampler&& min_filter(FilterMode filter) {
            _info.minFilter = static_cast<VkFilter>(filter);
            return std::move(*this);
        }

        Sampler&& mag_filter(FilterMode filter) {
            _info.magFilter = static_cast<VkFilter>(filter);
            return std::move(*this);
        }

        Sampler&& unnormalized_coords(bool flag) {
            _info.unnormalizedCoordinates = flag;
            return std::move(*this);
        }

        Sampler&& lod(float min, float max, float mip_bias) {
            _info.minLod = min;
            _info.maxLod = max;
            _info.mipLodBias = mip_bias;
            return std::move(*this);
        }

        VkSampler create() const;

        static void destroy(VkSampler sampler);
    };

    void to_json(nlohmann::json& j, const Sampler& sampler);
    void from_json(const nlohmann::json& j, Sampler& sampler);
}
