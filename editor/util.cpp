#include "util.hpp"
#include "goliath/scenes.hpp"
#include "scene.hpp"
#include "state.hpp"
#include "textures.hpp"

void remove_asset(engine::DependencyGraph::AssetGID gid) {
    std::visit(
        [&](auto&& gid) {
            using T = std::decay_t<decltype(gid)>;

            if constexpr (std::is_same_v<T, engine::models::gid>) {
                engine::models::remove(gid);

                for (size_t i = 0; i < engine::scenes::get_names().size(); i++) {
                    auto selected_instance = scene::selected_instance(i);
                    engine::scenes::remove_all_instances_of_model(i, gid, selected_instance);
                    scene::select_instance(selected_instance, i);
                }
            } else if constexpr (std::is_same_v<T, engine::Textures::gid>) {
                game_textures->remove(gid);
            } else if constexpr (std::is_same_v<T, engine::Materials::gid>) {
                state::materials->remove_instance(gid);
            } else {
                static_assert("unhandled");
            }
        },
        gid);
}
