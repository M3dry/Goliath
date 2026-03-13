#include "goliath/samplers.hpp"

#include "engine_.hpp"
#include "goliath/engine.hpp"
#include "xxHash/xxhash.h"

#include <vector>

namespace engine {
    void to_json(nlohmann::json& j, const Sampler& sampler) {
        j = nlohmann::json{
            {"addr_u", sampler._info.addressModeU},
            {"addr_v", sampler._info.addressModeV},
            {"addr_w", sampler._info.addressModeW},
            {"mipmap", sampler._info.mipmapMode},
            {"anisotropy", sampler._info.anisotropyEnable},
            {"compare", sampler._info.compareEnable ? std::make_optional(sampler._info.compareOp) : std::nullopt},
            {"border", sampler._info.borderColor},
            {"min_filter", sampler._info.minFilter},
            {"mag_filter", sampler._info.magFilter},
            {"unnormalized_coords", sampler._info.unnormalizedCoordinates},
            {"max_lod", sampler._info.maxLod},
            {"min_lod", sampler._info.minLod},
            {"mip_lod_bias", sampler._info.mipLodBias},
        };
    }

    void from_json(const nlohmann::json& j, Sampler& sampler) {
        sampler = Sampler{};
        j["addr_u"].get_to(sampler._info.addressModeU);
        j["addr_v"].get_to(sampler._info.addressModeV);
        j["addr_w"].get_to(sampler._info.addressModeW);
        j["mipmap"].get_to(sampler._info.mipmapMode);
        j["anisotropy"].get_to(sampler._info.anisotropyEnable);
        if (j["compare"].is_null()) {
            sampler._info.compareEnable = false;
        } else {
            j["compare"].get_to(sampler._info.compareOp);
        }
        j["border"].get_to(sampler._info.borderColor);
        j["min_filter"].get_to(sampler._info.minFilter);
        j["mag_filter"].get_to(sampler._info.magFilter);
        j["unnormalized_coords"].get_to(sampler._info.unnormalizedCoordinates);
        j["max_lod"].get_to(sampler._info.maxLod);
        j["min_lod"].get_to(sampler._info.minLod);
        j["mip_lod_bias"].get_to(sampler._info.mipLodBias);
    }
}

namespace engine::samplers {
    uint64_t hash_sampler(const Sampler& sampler) {
        return XXH3_64bits(&sampler, sizeof(Sampler));
    }

    std::vector<uint64_t> hashes{};
    std::vector<uint32_t> ref_counts{};
    std::vector<Sampler> prototypes{};
    std::vector<VkSampler> samplers{};

    void destroy() {
        for (auto sampler : samplers) {
            engine::destroy_sampler(sampler);
        }
    }
}

namespace engine::sampler {
    VkSampler _create(const Sampler& prototype) {
        VkSampler sampler;
        vkCreateSampler(device(), &prototype._info, nullptr, &sampler);
        return sampler;
    }

    VkSampler create(const Sampler& prototype) {
        auto hash = samplers::hash_sampler(prototype);
        uint32_t empty_spot = -1;

        for (uint32_t i = 0; i < samplers::hashes.size(); i++) {
            if (samplers::ref_counts[i] == 0) {
                empty_spot = i;
                continue;
            }
            if (hash != samplers::hashes[i]) continue;
            if (prototype != samplers::prototypes[i]) continue;

            samplers::ref_counts[i]++;
            return samplers::samplers[i];
        }

        if (empty_spot == -1) {
            samplers::hashes.emplace_back(hash);
            samplers::ref_counts.emplace_back(1);
            samplers::prototypes.emplace_back(prototype);
            samplers::samplers.emplace_back(_create(prototype));

            return samplers::samplers.back();
        } else {
            samplers::hashes[empty_spot] = hash;
            samplers::ref_counts[empty_spot] = 1;
            samplers::prototypes[empty_spot] = prototype;
            samplers::samplers[empty_spot] = _create(prototype);

            return samplers::samplers[empty_spot];
        }
    }

    void destroy(VkSampler sampler) {
        for (size_t i = 0; i < samplers::samplers.size(); i++) {
            if (samplers::samplers[i] != sampler) continue;

            if (samplers::ref_counts[i] == 0) return;
            if (--samplers::ref_counts[i] != 0) return;

            samplers::hashes[i] = 0;
            samplers::prototypes[i] = {};
            samplers::samplers[i] = nullptr;
            engine::destroy_sampler(sampler);
        }
    }
}
