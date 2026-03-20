#pragma once

#include "goliath/materials.hpp"
#include "goliath/models.hpp"
#include "goliath/textures.hpp"
#include "goliath/util.hpp"
#include <functional>
#include <mutex>
#include <utility>

namespace engine {
    class DependencyGraph {
      public:
        using AssetGID = std::variant<models::gid, Textures::gid, Materials::gid>;

        template <typename F> decltype(auto) with_deps(F&& f, AssetGID gid) {
            std::lock_guard lock{mutex};
            return f(get_deps(gid));
        }

        template <typename F> decltype(auto) with_r_deps(F&& f, AssetGID gid) {
            std::lock_guard lock{mutex};
            return f(get_r_deps(gid));
        }

        void remove_asset(AssetGID gid) {
            std::lock_guard lock{mutex};
            if (auto asset = _remove_asset(gid); asset) {
                modified();
                asset->get() = Asset{};
            }
        }

        void remove_dep(AssetGID gid, AssetGID dgid) {
            std::lock_guard lock{mutex};

            auto asset_ = get_asset(gid);
            if (!asset_) return;
            auto& asset = asset_->get();

            std::erase_if(asset.deps, [&](auto gid) { return gid == dgid; });

            auto dasset_ = get_asset(dgid);
            if (!dasset_) return;
            auto& dasset = dasset_->get();

            std::erase_if(dasset.r_deps, [&](auto rdgid) { return gid == rdgid; });
        }

        void add_dep(AssetGID asset, AssetGID dep);

        static std::expected<DependencyGraph*, std::pair<std::filesystem::path, util::ReadJsonErr>>
        init(std::filesystem::path metadata_dir);
        void save(std::filesystem::path alternative_dir = "");

        bool want_to_save() {
            std::lock_guard lock{mutex};

            auto res = want_save;
            want_save = false;
            return res;
        }

        std::pair<std::vector<AssetGID>, std::vector<std::pair<AssetGID, AssetGID>>> deep_remove(AssetGID root) {
            std::lock_guard lock{mutex};
            std::vector<AssetGID> ret{};
            std::vector<std::pair<AssetGID, AssetGID>> removals{};
            _deep_remove(root, ret, removals);
            if (ret.empty()) {
                ret.emplace_back(root);
            }
            modified();

            // debug_print();
            return {ret, removals};
        }

      private:
        DependencyGraph(std::filesystem::path metadata_dir) : metadata_dir(metadata_dir) {};

        std::filesystem::path metadata_dir;
        bool want_save = false;
        std::recursive_mutex mutex{};

        struct Asset {
            uint32_t generation = -1;
            std::vector<DependencyGraph::AssetGID> deps{};
            std::vector<DependencyGraph::AssetGID> r_deps{};
        };

        std::vector<Asset> model_deps;
        std::vector<Asset> texture_deps;
        std::vector<std::vector<Asset>> material_deps;

        void modified() {
            want_save = true;
        }

        void build_r_deps();

        void add_dep(AssetGID target, AssetGID dep, bool reverse);

        std::optional<std::reference_wrapper<Asset>> _remove_asset(
            AssetGID gid,
            std::optional<std::reference_wrapper<std::vector<std::pair<AssetGID, AssetGID>>>> removals = std::nullopt) {
            auto asset_ = get_asset(gid);
            if (!asset_) return std::nullopt;
            auto& asset = asset_->get();

            for (const auto& dep : asset.deps) {
                auto ddep = get_asset(dep);
                if (!ddep) continue;
                std::erase_if(ddep->get().r_deps, [&](auto r_dgid) {
                    auto assets_ = get_assets(r_dgid);
                    if (!assets_) return false;
                    auto& assets = assets_->get();

                    if ((assets.size() > get_id(r_dgid) && get_gen(r_dgid) < assets[get_id(r_dgid)].generation) ||
                        gid == r_dgid) {
                        if (removals) removals->get().emplace_back(gid, r_dgid);
                        return true;
                    }

                    return false;
                });
            }

            for (const auto& dep : asset.r_deps) {
                auto ddep = get_asset(dep);
                if (!ddep) continue;
                std::erase_if(ddep->get().deps, [&](auto dgid) {
                    auto assets_ = get_assets(dgid);
                    if (!assets_) return false;
                    auto& assets = assets_->get();

                    return (assets.size() > get_id(dgid) && get_gen(dgid) < assets[get_id(dgid)].generation) ||
                           gid == dgid;
                });
            }

            return asset;
        }

        void _deep_remove(AssetGID root, std::vector<AssetGID>& out,
                          std::vector<std::pair<AssetGID, AssetGID>>& removals) {
            auto asset_ = _remove_asset(root, removals);
            if (!asset_) return;
            auto& asset = asset_->get();

            out.emplace_back(root);

            auto deps = std::move(asset.deps);
            asset = Asset{};

            bool acc = false;
            for (const auto& dep_gid : deps) {
                auto dep_asset = get_asset(dep_gid);
                if (!dep_asset) continue;
                if (!dep_asset->get().r_deps.empty()) continue;

                _deep_remove(dep_gid, out, removals);
            }
        }

        std::span<const AssetGID> get_deps(AssetGID gid) const;
        std::span<const AssetGID> get_r_deps(AssetGID gid) const;

        template <typename GID> std::span<std::vector<Asset>> get_assets() {
            if constexpr (std::is_same_v<GID, models::gid>) {
                return {&model_deps, 1};
            } else if constexpr (std::is_same_v<GID, Textures::gid>) {
                return {&texture_deps, 1};
            } else if constexpr (std::is_same_v<GID, Materials::gid>) {
                return material_deps;
            } else {
                static_assert("GID must be either models::gid or Textures::gid");
            }
        }

        template <typename GID> std::span<const std::vector<Asset>> get_assets() const {
            if constexpr (std::is_same_v<GID, models::gid>) {
                return {&model_deps, 1};
            } else if constexpr (std::is_same_v<GID, Textures::gid>) {
                return {&texture_deps, 1};
            } else if constexpr (std::is_same_v<GID, Materials::gid>) {
                return material_deps;
            } else {
                static_assert("GID must be either models::gid or Textures::gid");
            }
        }

        template <bool CreateDims = false>
        std::conditional_t<CreateDims, std::vector<Asset>&, std::optional<std::reference_wrapper<std::vector<Asset>>>> get_assets(AssetGID gid_type) {
            return std::visit(
                [&](auto&& gid) -> std::conditional_t<CreateDims, std::vector<Asset>&, std::optional<std::reference_wrapper<std::vector<Asset>>>> {
                    using GID = std::decay_t<decltype(gid)>;

                    if constexpr (std::is_same_v<GID, models::gid>) {
                        return model_deps;
                    } else if constexpr (std::is_same_v<GID, Textures::gid>) {
                        return texture_deps;
                    } else if constexpr (std::is_same_v<GID, Materials::gid>) {
                        if (material_deps.size() <= gid.dim()) {
                            if constexpr (!CreateDims) return {};
                            else while (material_deps.size() <= gid.dim()) material_deps.emplace_back();
                        }

                        return material_deps[gid.dim()];
                    } else {
                        static_assert("GID must be either models::gid or Textures::gid");
                    }
                },
                gid_type);
        }

        std::span<const Asset> get_assets(AssetGID gid_type) const {
            return std::visit(
                [&](auto&& gid) -> std::span<const Asset> {
                    using GID = std::decay_t<decltype(gid)>;
                    if constexpr (std::is_same_v<GID, models::gid>) {
                        return model_deps;
                    } else if constexpr (std::is_same_v<GID, Textures::gid>) {
                        return texture_deps;
                    } else if constexpr (std::is_same_v<GID, Materials::gid>) {
                        if (material_deps.size() <= gid.dim()) return {};
                        return material_deps[gid.dim()];
                    } else {
                        static_assert("GID must be either models::gid or Textures::gid");
                    }
                },
                gid_type);
        }

        std::optional<std::reference_wrapper<Asset>> get_asset(AssetGID gid) {
            auto assets_ = get_assets(gid);
            if (!assets_) return {};
            auto& assets = assets_->get();

            if (assets.size() <= get_id(gid)) return std::nullopt;
            if (assets[get_id(gid)].generation != get_gen(gid)) return std::nullopt;

            return assets[get_id(gid)];
        }

        std::optional<std::reference_wrapper<const Asset>> get_asset(AssetGID gid) const {
            auto assets = get_assets(gid);
            if (assets.size() <= get_id(gid)) return std::nullopt;
            if (assets[get_id(gid)].generation != get_gen(gid)) return std::nullopt;

            return assets[get_id(gid)];
        }

        static uint32_t get_id(AssetGID gid) {
            return std::visit([&](auto&& gid) { return gid.id(); }, gid);
        }

        static uint32_t get_gen(AssetGID gid) {
            return std::visit([&](auto&& gid) { return gid.gen(); }, gid);
        }

        static uint32_t get_dim(AssetGID gid) {
            return std::visit([&](auto&& gid) -> uint32_t { return gid.dim(); }, gid);
        }

        template <typename GID> static constexpr GID construct_gid(uint32_t dim, uint32_t gen, uint32_t id) {
            if constexpr (requires {
                              GID::dim_mask;
                              GID::dim_shift;
                          }) {
                return GID{dim, gen, id};
            } else {
                return GID{gen, id};
            }
        }

        constexpr AssetGID modify_gen(AssetGID dst, uint32_t gen) {
            return std::visit(
                [&](auto&& gid) -> AssetGID {
                    return construct_gid<std::decay_t<decltype(gid)>>(get_dim(gid), gen, get_id(gid));
                },
                dst);
        }

        constexpr AssetGID modify_id(AssetGID dst, uint32_t id) {
            return std::visit(
                [&](auto&& gid) -> AssetGID {
                    return construct_gid<std::decay_t<decltype(gid)>>(get_dim(gid), get_gen(gid), id);
                },
                dst);
        }

        constexpr AssetGID make_gid_from(const AssetGID& src, uint32_t dim, uint32_t gen, uint32_t id) {
            return std::visit(
                [&](auto&& gid) -> AssetGID { return construct_gid<std::decay_t<decltype(gid)>>(dim, gen, id); }, src);
        }

        template <typename F> static constexpr decltype(auto) visit_gids(F&& f) {
            return [&]<typename GID, typename... GIDs>(std::in_place_type_t<std::variant<GID, GIDs...>>) {
                using ErrType = decltype(std::declval<F>().template operator()<GID>());

                if constexpr (std::is_same_v<ErrType, void>) {
                    f.template operator()<GID>();
                    (f.template operator()<GIDs>(), ...);
                } else {
                    ErrType err = f.template operator()<GID>();

                    ((!err && (err = f.template operator()<GIDs>())), ...);

                    return err;
                }
            }(std::in_place_type_t<AssetGID>{});
        }

        void debug_print() const {
            printf("models:\n");
            for (const auto& asset : model_deps) {
                printf("{ .gen = %d, .deps = [\n", asset.generation);

                for (const auto& dep : asset.deps) {
                    printf(" \t{ .gen = %d, .id = %d }\n", get_gen(dep), get_id(dep));
                }
                printf(" ], .r_deps = [\n");

                for (const auto& dep : asset.r_deps) {
                    printf(" \t{ .gen = %d, .id = %d }\n", get_gen(dep), get_id(dep));
                }
                printf(" ]\n}\n");
            }

            printf("textures:\n");
            for (const auto& asset : texture_deps) {
                printf("{ .gen = %d, .deps = [\n", asset.generation);

                for (const auto& dep : asset.deps) {
                    printf(" \t{ .gen = %d, .id = %d }\n", get_gen(dep), get_id(dep));
                }
                printf(" ], .r_deps = [\n");

                for (const auto& dep : asset.r_deps) {
                    printf(" \t{ .gen = %d, .id = %d }\n", get_gen(dep), get_id(dep));
                }
                printf(" ]\n}\n");
            }

            printf("-------------------------------------\n");
        }
    };
}
