#pragma once

#include "goliath/models.hpp"
#include "goliath/textures.hpp"

namespace engine::dep_graph {
    using AssetGID = std::variant<models::gid, Textures::gid>;

    std::span<const AssetGID> get_deps_of(models::gid gid);
    std::span<const AssetGID> get_deps_of(Textures::gid gid);

    std::span<const AssetGID> get_r_deps_of(models::gid gid);
    std::span<const AssetGID> get_r_deps_of(Textures::gid gid);
}
