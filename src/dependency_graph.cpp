#include "goliath/dependency_graph.hpp"

namespace engine::dep_graph {
    struct Asset {
        uint32_t generation;
        std::vector<AssetGID> deps;
        std::vector<AssetGID> r_deps;
    };

    std::vector<Asset> model_deps;
    std::vector<Asset> texture_deps;

    std::span<const AssetGID> get_deps_of(models::gid gid) {
        auto& asset = model_deps[gid.id()];
        if (asset.generation != gid.gen()) return {};

        return asset.deps;
    }

    std::span<const AssetGID> get_deps_of(Textures::gid gid) {
        auto& asset = texture_deps[gid.id()];
        if (asset.generation != gid.gen()) return {};

        return asset.deps;
    }

    std::span<const AssetGID> get_r_deps_of(models::gid gid) {
        auto& asset = model_deps[gid.id()];
        if (asset.generation != gid.gen()) return {};

        return asset.r_deps;
    }

    std::span<const AssetGID> get_r_deps_of(Textures::gid gid) {
        auto& asset = texture_deps[gid.id()];
        if (asset.generation != gid.gen()) return {};

        return asset.r_deps;
    }
}
