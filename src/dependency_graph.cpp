#include "goliath/dependency_graph.hpp"
#include "goliath/util.hpp"
#include <filesystem>
#include <fstream>
#include <variant>

namespace engine {
    void to_json(nlohmann::json& j, const DependencyGraph::AssetGID& gid) {
        std::visit(
            [&j](auto&& gid) {
                using T = std::decay_t<decltype(gid)>;

                const char* str;
                if constexpr (std::is_same_v<T, models::gid>) {
                    str = "model";
                } else if constexpr (std::is_same_v<T, Textures::gid>) {
                    str = "texture";
                }

                j = {
                    {"type", str},
                    {"gen", gid.gen()},
                    {"id", gid.id()},
                };
            },
            gid);
    }

    void from_json(const nlohmann::json& j, DependencyGraph::AssetGID& gid) {
        std::string type = j["type"];
        uint32_t gen = j["gen"];
        uint32_t id = j["id"];
        if (type == "model") {
            gid = models::gid{gen, id};
        } else if (type == "texture") {
            gid = Textures::gid{gen, id};
        } else {
            assert(false &&
                   "figure out a good way to report this error such that it conveys which asset metadata is corrupted");
        }
    }

    std::span<const DependencyGraph::AssetGID> DependencyGraph::get_deps(AssetGID gid) const {
        if (auto asset = get_asset(gid); asset) {
            return asset->get().deps;
        }

        return {};
    }

    std::span<const DependencyGraph::AssetGID> DependencyGraph::get_r_deps(AssetGID gid) const {
        if (auto asset = get_asset(gid); asset) {
            return asset->get().r_deps;
        }

        return {};
    }

    void DependencyGraph::remove_asset(AssetGID gid) {
        auto asset_ = get_asset(gid);
        if (!asset_) return;
        auto& asset = asset_->get();

        for (const auto& dep : asset.deps) {
            auto ddep = get_asset(dep);
            if (!ddep) continue;
            std::erase_if(ddep->get().r_deps, [&](auto r_dgid) {
                auto assets = get_assets(r_dgid);
                return (assets.size() > get_id(r_dgid) && get_gen(r_dgid) < assets[get_id(r_dgid)].generation) ||
                       gid == r_dgid;
            });
        }

        for (const auto& dep : asset.r_deps) {
            auto ddep = get_asset(dep);
            if (!ddep) continue;
            std::erase_if(ddep->get().deps, [&](auto dgid) {
                auto assets = get_assets(dgid);
                return (assets.size() > get_id(dgid) && get_gen(dgid) < assets[get_id(dgid)].generation) || gid == dgid;
            });
        }

        modified();
        asset = Asset{};
    }

    void DependencyGraph::add_dep(AssetGID asset, AssetGID dep) {
        add_dep(asset, dep, false);
    }

    std::expected<DependencyGraph, util::ReadJsonErr> DependencyGraph::init(std::filesystem::path metadata_dir) {
        DependencyGraph graph{metadata_dir};

        visit_gids([&]<typename GID>() -> std::optional<util::ReadJsonErr> {
            std::filesystem::path entries_path = metadata_dir;
            const char* file_ext;

            if constexpr (std::is_same_v<GID, models::gid>) {
                entries_path /= "models";
                file_ext = "gom";
            } else if constexpr (std::is_same_v<GID, Textures::gid>) {
                entries_path /= "textures";
                file_ext = "goi";
            } else if constexpr (std::is_same_v<GID, Textures::gid>) {
            } else static_assert("impossible");

            auto assets = graph.get_assets<GID>();
            for (const auto& entry : std::filesystem::directory_iterator{entries_path}) {
                if (!entry.is_regular_file()) continue;

                auto [gen, id] = util::parse_gid(entry.path().stem().string(), file_ext);
                while (assets.size() < id) {
                    assets.emplace_back();
                }

                if (assets[id].generation != -1) {
                    fprintf(stderr, "multiple dependency metadata files with same id");
                }
                if (assets[id].generation > gen) continue;

                auto j = util::read_json(entry.path());
                if (!j) return j.error();

                assets[id] = Asset{
                    .generation = gen,
                    .deps = *j,
                };
            }

            return std::nullopt;
        });

        graph.build_r_deps();
        return graph;
    }

    void DependencyGraph::save(std::filesystem::path alternative_dir) const {
        if (alternative_dir.empty()) alternative_dir = metadata_dir;

        visit_gids([&]<typename GID>() {
            std::filesystem::path entries_path = alternative_dir;
            const char* file_ext;

            if constexpr (std::is_same_v<GID, models::gid>) {
                entries_path /= "models";
                file_ext = "gom";
            } else if constexpr (std::is_same_v<GID, Textures::gid>) {
                entries_path /= "textures";
                file_ext = "goi";
            } else static_assert("impossible");

            auto assets = get_assets<GID>();

            for (uint32_t i = 0; i < assets.size(); i++) {
                nlohmann::json j = assets[i].deps;
                std::ofstream o{entries_path / std::format("{:02X}{:06X}.{}", file_ext)};
                o << j;
            }
        });
    }

    void DependencyGraph::build_r_deps() {
        visit_gids([&]<typename GID>() {
            auto assets = get_assets<GID>();
            for (uint32_t i = 0; i < assets.size(); i++) {
                for (const auto& dep : assets[i].deps) {
                    auto r_dep = get_asset(dep);
                    if (!r_dep) continue;

                    r_dep->get().deps.emplace_back(GID{assets[i].generation, i});
                }
            }
        });
    }

    void DependencyGraph::add_dep(AssetGID target, AssetGID dep, bool reverse) {
        auto assets = get_assets(target);
        while (assets.size() < get_id(target)) {
            texture_deps.emplace_back();
        }

        auto& asset = assets[get_id(target)];
        if (asset.generation > get_gen(target)) return;
        if (asset.generation < get_gen(target)) {
            remove_asset(modify_gen(target, asset.generation));
            asset = Asset{};
            asset.generation = get_gen(target);
        }

        asset.deps.emplace_back(dep);
        modified();
        if (reverse) return;

        add_dep(dep, target, true);
    }
}
