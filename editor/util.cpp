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

uint32_t pick_id_3x3(const uint32_t* a) {
    // Layout assumed:
    // 0 1 2
    // 3 4 5
    // 6 7 8
    constexpr int center_idx = 4;

    std::array<uint32_t, 9> values{};
    std::array<int, 9> counts{};
    int unique = 0;

    auto add = [&](uint32_t v, int weight = 1) {
        if (v == 0) return;

        for (int i = 0; i < unique; ++i) {
            if (values[i] == v) {
                counts[i] += weight;
                return;
            }
        }
        values[unique] = v;
        counts[unique] = weight;
        unique++;
    };

    // Fill counts
    for (int i = 0; i < 9; ++i) {
        int weight = (i == center_idx) ? 3 : 1; // <-- bias here
        add(a[i], weight);
    }

    // Find best
    int best = 0;
    for (int i = 1; i < unique; ++i) {
        if (counts[i] > counts[best]) {
            best = i;
        }
    }

    uint32_t winner = values[best];

    // --- Confidence check ---
    // Max possible weight = 3 (center) + 8 = 11
    // Require at least ~4–5 to avoid edge noise
    if (counts[best] < 4) {
        return a[center_idx]; // fallback
    }

    return winner;
}
