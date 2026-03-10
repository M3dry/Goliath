#include "goliath/dependency_graph.hpp"
#include "goliath/util.hpp"
#include <filesystem>
#include <variant>

namespace engine {
    void to_json(nlohmann::json& j, const DependencyGraph::AssetGID& gid) {
        std::visit([&j](auto&& gid) {
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
        }, gid);
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
            assert(false && "figure out a good way to report this error such that it conveys which asset metadata is corrupted");
        }
    }

    std::span<const DependencyGraph::AssetGID> DependencyGraph::get_deps_of(models::gid gid) const {
        auto& asset = model_deps[gid.id()];
        if (asset.generation != gid.gen()) return {};

        return asset.deps;
    }

    std::span<const DependencyGraph::AssetGID> DependencyGraph::get_deps_of(Textures::gid gid) const {
        auto& asset = texture_deps[gid.id()];
        if (asset.generation != gid.gen()) return {};

        return asset.deps;
    }

    std::span<const DependencyGraph::AssetGID> DependencyGraph::get_r_deps_of(models::gid gid) const {
        auto& asset = model_deps[gid.id()];
        if (asset.generation != gid.gen()) return {};

        return asset.r_deps;
    }

    std::span<const DependencyGraph::AssetGID> DependencyGraph::get_r_deps_of(Textures::gid gid) const {
        auto& asset = texture_deps[gid.id()];
        if (asset.generation != gid.gen()) return {};

        return asset.r_deps;
    }

    void DependencyGraph::remove_asset(models::gid asset) {
        if (model_deps.size() <= asset.id()) return;
        auto& deps = model_deps[asset.id()];
        if (deps.generation > asset.gen()) return;

        for (const auto& dep : deps.deps) {
            std::visit([&](auto&& gid) {
                using T = std::decay_t<decltype(gid)>;

                if constexpr (std::is_same_v<T, models::gid>) {
                    std::erase_if(model_deps[gid.id()].r_deps, [&](auto r_gid) {
                        return std::holds_alternative<models::gid>(r_gid) && std::get<models::gid>(r_gid) == gid;
                    });
                } else if constexpr (std::is_same_v<T, Textures::gid>) {
                    std::erase_if(texture_deps[gid.id()].r_deps, [&](auto r_gid) {
                        return std::holds_alternative<Textures::gid>(r_gid) && std::get<Textures::gid>(r_gid) == gid;
                    });
                }
            }, dep);
        }

        for (const auto& dep : deps.r_deps) {
            std::visit([&](auto&& gid) {
                using T = std::decay_t<decltype(gid)>;

                if constexpr (std::is_same_v<T, models::gid>) {
                    std::erase_if(model_deps[gid.id()].deps, [&](auto r_gid) {
                        return std::holds_alternative<models::gid>(r_gid) && std::get<models::gid>(r_gid) == gid;
                    });
                } else if constexpr (std::is_same_v<T, Textures::gid>) {
                    std::erase_if(texture_deps[gid.id()].deps, [&](auto r_gid) {
                        return std::holds_alternative<Textures::gid>(r_gid) && std::get<Textures::gid>(r_gid) == gid;
                    });
                }
            }, dep);
        }

        model_deps[asset.id()] = Asset{};
    }

    void DependencyGraph::remove_asset(Textures::gid asset) {
        if (texture_deps.size() <= asset.id()) return;
        auto& deps = texture_deps[asset.id()];
        if (deps.generation > asset.gen()) return;

        for (const auto& dep : deps.deps) {
            std::visit([&](auto&& gid) {
                using T = std::decay_t<decltype(gid)>;

                if constexpr (std::is_same_v<T, models::gid>) {
                    std::erase_if(texture_deps[gid.id()].r_deps, [&](auto r_gid) {
                        return std::holds_alternative<models::gid>(r_gid) && std::get<models::gid>(r_gid) == gid;
                    });
                } else if constexpr (std::is_same_v<T, Textures::gid>) {
                    std::erase_if(texture_deps[gid.id()].r_deps, [&](auto r_gid) {
                        return std::holds_alternative<Textures::gid>(r_gid) && std::get<Textures::gid>(r_gid) == gid;
                    });
                }
            }, dep);
        }

        for (const auto& dep : deps.r_deps) {
            std::visit([&](auto&& gid) {
                using T = std::decay_t<decltype(gid)>;

                if constexpr (std::is_same_v<T, models::gid>) {
                    std::erase_if(texture_deps[gid.id()].deps, [&](auto r_gid) {
                        return std::holds_alternative<models::gid>(r_gid) && std::get<models::gid>(r_gid) == gid;
                    });
                } else if constexpr (std::is_same_v<T, Textures::gid>) {
                    std::erase_if(texture_deps[gid.id()].deps, [&](auto r_gid) {
                        return std::holds_alternative<Textures::gid>(r_gid) && std::get<Textures::gid>(r_gid) == gid;
                    });
                }
            }, dep);
        }

        texture_deps[asset.id()] = Asset{};
    }

    void DependencyGraph::add_dep(models::gid asset, AssetGID dep) {
        add_dep(asset, dep, false);
    }

    void DependencyGraph::add_dep(Textures::gid asset, AssetGID dep) {
        add_dep(asset, dep, false);
    }

    std::expected<DependencyGraph, util::ReadJsonErr> DependencyGraph::init(std::filesystem::path metadata_dir) {
        DependencyGraph graph{metadata_dir};

        for (const auto& entry : std::filesystem::directory_iterator{ metadata_dir / "models" }) {
            if (!entry.is_regular_file()) continue;

            auto [gen, id] = util::parse_gom(entry.path().stem().string());

            while (graph.model_deps.size() <= id) {
                graph.model_deps.emplace_back();
            }

            if (graph.model_deps[id].generation != -1) { fprintf(stderr, "multiple dependency metadata files with same id"); };
            if (graph.model_deps[id].generation > gen) continue;

            auto j = util::read_json(entry.path());
            if (!j) return std::unexpected(j.error());

            graph.model_deps[id] = Asset{
                .generation = gen,
                .deps = *j,
            };
        }

        for (const auto& entry : std::filesystem::directory_iterator{ metadata_dir / "textures" }) {
            if (!entry.is_regular_file()) continue;

            auto [gen, id] = util::parse_goi(entry.path().stem().string());

            while (graph.texture_deps.size() <= id) {
                graph.texture_deps.emplace_back();
            }

            if (graph.texture_deps[id].generation != -1) { fprintf(stderr, "multiple dependency metadata files with same id"); };
            if (graph.texture_deps[id].generation > gen) continue;

            auto j = util::read_json(entry.path());
            if (!j) return std::unexpected(j.error());

            graph.texture_deps[id] = Asset{
                .generation = gen,
                .deps = *j,
            };
        }

        graph.build_r_deps();
        return graph;
    }

    void DependencyGraph::build_r_deps() {
        for (uint32_t i = 0; i < model_deps.size(); i++) {
            for (const auto& dep : model_deps[i].deps) {
                std::visit([&](auto&& gid) {
                    using T = std::decay_t<decltype(gid)>;

                    if constexpr (std::is_same_v<T, models::gid>) {
                        model_deps[gid.id()].deps.emplace_back(models::gid{model_deps[i].generation, i});
                    } else if constexpr (std::is_same_v<T, Textures::gid>) {
                        texture_deps[gid.id()].deps.emplace_back(models::gid{model_deps[i].generation, i});
                    }
                }, dep);
            }
        }

        for (uint32_t i = 0; i < texture_deps.size(); i++) {
            for (const auto& dep : texture_deps[i].deps) {
                std::visit([&](auto&& gid) {
                    using T = std::decay_t<decltype(gid)>;

                    if constexpr (std::is_same_v<T, models::gid>) {
                        model_deps[gid.id()].deps.emplace_back(Textures::gid{texture_deps[i].generation, i});
                    } else if constexpr (std::is_same_v<T, Textures::gid>) {
                        texture_deps[gid.id()].deps.emplace_back(Textures::gid{texture_deps[i].generation, i});
                    }
                }, dep);
            }
        }
    }

    void DependencyGraph::add_dep(models::gid asset, AssetGID dep, bool reverse) {
        while (model_deps.size() <= asset.id()) {
            model_deps.emplace_back();
        }

        auto& deps = model_deps[asset.id()];
        if (deps.generation > asset.gen()) return;
        if (deps.generation < asset.gen()) {
            remove_asset(asset);
            deps = Asset{};
            deps.generation = asset.gen();
        }

        deps.deps.emplace_back(dep);
        if (reverse) return;

        std::visit([&](auto&& gid) {
            add_dep(gid, asset, true);
        }, dep);
    }

    void DependencyGraph::add_dep(Textures::gid asset, AssetGID dep, bool reverse) {
        while (texture_deps.size() <= asset.id()) {
            texture_deps.emplace_back();
        }

        auto& deps = texture_deps[asset.id()];
        if (deps.generation > asset.gen()) return;
        if (deps.generation < asset.gen()) {
            remove_asset(asset);
            deps = Asset{};
            deps.generation = asset.gen();
        }

        deps.deps.emplace_back(dep);
        if (reverse) return;

        std::visit([&](auto&& gid) {
            add_dep(gid, asset, true);
        }, dep);
    }
}
