#include "goliath/samplers.hpp"

#include "goliath/engine.hpp"
#include "xxHash/xxhash.h"

#include <vector>

namespace engine {
    VkSampler Sampler::create() const {
        VkSampler sampler;
        vkCreateSampler(device, &_info, nullptr, &sampler);
        return sampler;
    }

    void Sampler::destroy(VkSampler sampler) {
        vkDestroySampler(device, sampler, nullptr);
    }

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

    void init() {
        hashes.emplace_back(hash_sampler(Sampler{}));
        ref_counts.emplace_back(1);
        prototypes.emplace_back(Sampler{});
        samplers.emplace_back(Sampler{}.create());
    }

    void destroy() {
        for (auto sampler : samplers) {
            Sampler::destroy(sampler);
        }
    }

    struct JsonEntry {
        uint32_t ix;
        uint32_t ref_count;
        Sampler prototype;
    };
    void to_json(nlohmann::json& j, const JsonEntry& entry) {
        j = nlohmann::json{
            {"ix", entry.ix},
            {"ref_count", entry.ref_count},
            {"prototype", entry.prototype},
        };
    }

    void from_json(const nlohmann::json& j, JsonEntry& entry) {
        j["ix"].get_to(entry.ix);
        j["ref_count"].get_to(entry.ref_count);
        j["prototype"].get_to(entry.prototype);
    }

    void load(nlohmann::json j) {
        std::vector<JsonEntry> entries = j;

        auto ix_counter = 1;
        for (auto&& entry : entries) {
            auto ix = entry.ix;
            while (ix > ix_counter) {
                hashes.emplace_back(0);
                ref_counts.emplace_back(0);
                prototypes.emplace_back();
                samplers.emplace_back(nullptr);

                ix_counter++;
            }

            hashes.emplace_back(hash_sampler(entry.prototype));
            ref_counts.emplace_back(entry.ref_count);
            prototypes.emplace_back(entry.prototype);
            samplers.emplace_back(entry.prototype.create());

            ix_counter++;
        }
    }

    nlohmann::json save() {
        std::vector<JsonEntry> entries{};

        for (uint32_t i = 1; i < hashes.size(); i++) {
            if (ref_counts[i] == 0) continue;

            entries.emplace_back(JsonEntry{
                .ix = i,
                .ref_count = ref_counts[i],
                .prototype = prototypes[i],
            });
        }

        return entries;
    }

    uint32_t add(const Sampler& new_sampler) {
        uint64_t sampler_hash = hash_sampler(new_sampler);
        uint32_t empty_spot = -1;

        for (uint32_t i = 0; i < hashes.size(); i++) {
            if (ref_counts[i] == 0) {
                empty_spot = i;
                continue;
            }
            if (sampler_hash != hashes[i]) continue;
            if (new_sampler != prototypes[i]) continue;

            ref_counts[i]++;
            return i;
        }

        if (empty_spot == -1) {
            hashes.emplace_back(sampler_hash);
            ref_counts.emplace_back(1);
            prototypes.emplace_back(new_sampler);
            samplers.emplace_back(new_sampler.create());

            return hashes.size() - 1;
        } else {
            hashes[empty_spot] = sampler_hash;
            ref_counts[empty_spot] = 1;
            prototypes[empty_spot] = new_sampler;
            samplers[empty_spot] = new_sampler.create();

            return empty_spot;
        }
    }

    void remove(uint32_t ix) {
        if (--ref_counts[ix] != 0) return;

        Sampler::destroy(samplers[ix]);

        hashes[ix] = 0;
        samplers[ix] = nullptr;
    }

    VkSampler get(uint32_t ix) {
        return samplers[ix];
    }
}
