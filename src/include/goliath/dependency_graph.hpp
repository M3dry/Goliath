#pragma once

#include "goliath/models.hpp"
#include "goliath/textures.hpp"
#include "goliath/util.hpp"

namespace engine {
    class DependencyGraph {
      public:
        using AssetGID = std::variant<models::gid, Textures::gid>;

        std::span<const AssetGID> get_deps_of(models::gid gid) const;
        std::span<const AssetGID> get_deps_of(Textures::gid gid) const;

        std::span<const AssetGID> get_r_deps_of(models::gid gid) const;
        std::span<const AssetGID> get_r_deps_of(Textures::gid gid) const;

        void remove_asset(models::gid asset);
        void remove_asset(Textures::gid asset);

        void add_dep(models::gid asset, AssetGID dep);
        void add_dep(Textures::gid asset, AssetGID dep);

        static std::expected<DependencyGraph, util::ReadJsonErr> init(std::filesystem::path metadata_dir);

      private:
        DependencyGraph(std::filesystem::path metadata_dir) : metadata_dir(metadata_dir) {};

        std::filesystem::path metadata_dir;

        struct Asset {
            uint32_t generation = -1;
            std::vector<DependencyGraph::AssetGID> deps{};
            std::vector<DependencyGraph::AssetGID> r_deps{};
        };

        std::vector<Asset> model_deps;
        std::vector<Asset> texture_deps;

        void build_r_deps();

        void add_dep(models::gid asset, AssetGID dep, bool reverse);
        void add_dep(Textures::gid asset, AssetGID dep, bool reverse);
    };
}
