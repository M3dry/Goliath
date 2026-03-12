#pragma once

#include "goliath/models.hpp"
#include "goliath/textures.hpp"
#include "goliath/util.hpp"
#include <functional>
#include <utility>

namespace engine {
    class DependencyGraph {
      public:
        using AssetGID = std::variant<models::gid, Textures::gid>;

        std::span<const AssetGID> get_deps(AssetGID gid) const;
        std::span<const AssetGID> get_r_deps(AssetGID gid) const;

        void remove_asset(AssetGID asset);
        void add_dep(AssetGID asset, AssetGID dep);

        static std::expected<DependencyGraph, util::ReadJsonErr> init(std::filesystem::path metadata_dir);
        void save(std::filesystem::path alternative_dir = "") const;

        bool want_to_save() {
            auto res = want_save;
            want_save = false;
            return res;
        }

      private:
        DependencyGraph(std::filesystem::path metadata_dir) : metadata_dir(metadata_dir) {};

        std::filesystem::path metadata_dir;
        bool want_save = false;

        struct Asset {
            uint32_t generation = -1;
            std::vector<DependencyGraph::AssetGID> deps{};
            std::vector<DependencyGraph::AssetGID> r_deps{};
        };

        std::vector<Asset> model_deps;
        std::vector<Asset> texture_deps;

        void modified() {
            want_save = true;
        }

        void build_r_deps();

        void add_dep(AssetGID target, AssetGID dep, bool reverse);

        template <typename GID> std::vector<Asset>& get_assets() {
            if constexpr (std::is_same_v<GID, models::gid>) {
                return model_deps;
            } else if constexpr (std::is_same_v<GID, Textures::gid>) {
                return texture_deps;
            } else {
                static_assert("GID must be either models::gid or Textures::gid");
            }
        }

        template <typename GID> std::span<const Asset> get_assets() const {
            if constexpr (std::is_same_v<GID, models::gid>) {
                return model_deps;
            } else if constexpr (std::is_same_v<GID, Textures::gid>) {
                return texture_deps;
            } else {
                static_assert("GID must be either models::gid or Textures::gid");
            }
        }

        std::vector<Asset>& get_assets(AssetGID gid_type) {
            return std::visit([&](auto&& gid) -> std::vector<Asset>& { return get_assets<std::decay_t<decltype(gid)>>(); }, gid_type);
        }

        std::span<const Asset> get_assets(AssetGID gid_type) const {
            return std::visit([&](auto&& gid) { return get_assets<std::decay_t<decltype(gid)>>(); }, gid_type);
        }

        std::optional<std::reference_wrapper<Asset>> get_asset(AssetGID gid) {
            return std::visit(
                [&](auto&& gid) -> std::optional<std::reference_wrapper<Asset>> {
                    auto assets = get_assets<std::decay_t<decltype(gid)>>();
                    if (assets.size() <= get_id(gid)) return std::nullopt;
                    if (assets[gid.id()].generation != get_gen(gid)) return std::nullopt;

                    return assets[gid.id()];
                },
                gid);
        }

        std::optional<std::reference_wrapper<const Asset>> get_asset(AssetGID gid) const {
            return std::visit(
                [&](auto&& gid) -> std::optional<std::reference_wrapper<const Asset>> {
                    auto assets = get_assets<std::decay_t<decltype(gid)>>();
                    if (assets.size() <= get_id(gid)) return std::nullopt;
                    if (assets[gid.id()].generation != get_gen(gid)) return std::nullopt;

                    return assets[gid.id()];
                },
                gid);
        }

        static uint32_t get_id(AssetGID gid) {
            return std::visit([&](auto&& gid) { return gid.id(); }, gid);
        }

        static uint32_t get_gen(AssetGID gid) {
            return std::visit([&](auto&& gid) { return gid.gen(); }, gid);
        }

        constexpr AssetGID modify_gen(AssetGID dst, uint32_t gen) {
            return std::visit([&](auto&& gid) -> AssetGID { return (std::decay_t<decltype(gid)>){gen, gid.id()}; },
                              dst);
        }

        constexpr AssetGID modify_id(AssetGID dst, uint32_t id) {
            return std::visit([&](auto&& gid) -> AssetGID { return (std::decay_t<decltype(gid)>){gid.gen(), id}; },
                              dst);
        }

        constexpr AssetGID make_gid_from(const AssetGID& src, uint32_t gen, uint32_t id) {
            return std::visit([&](auto&& gid) -> AssetGID { return (std::decay_t<decltype(gid)>){gen, id}; }, src);
        }

        template <typename F>
        static constexpr void visit_gids(F&& f) {
            [&]<typename GID, typename... GIDs>(std::in_place_type_t<std::variant<GID, GIDs...>>) {
               using ErrType = decltype(std::declval<F>().template operator()<GID>());

               if constexpr (std::is_same_v<ErrType, void>) {
                   (f.template operator()<GIDs>(), ...);
               } else {
                   ErrType err{};

                   ((!err && (err = f.template operator()<GIDs>())), ...);

                   return err;
               }
            }(std::in_place_type_t<AssetGID>{});
        }
    };
}
