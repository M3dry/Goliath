#pragma once

#include "goliath/util.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>

namespace engine {
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

namespace engine::samplers {
    void init();
    void destroy();

    void load(nlohmann::json j);
    nlohmann::json save();

    uint32_t add(const Sampler& new_sampler);
    void remove(uint32_t ix);

    VkSampler get(uint32_t ix);
}
