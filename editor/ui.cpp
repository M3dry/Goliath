#include "ui.hpp"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "models.hpp"
#include "scene.hpp"
#include <limits>

int32_t score_search(std::string_view query, std::string_view candidate) {
    if (query.empty()) return 0;

    uint32_t score = 0;
    uint32_t consecutive = 0;
    size_t q_i = 0;
    for (size_t n_i = 0; n_i < candidate.size() && q_i < query.size(); n_i++) {
        if (std::tolower(candidate[n_i]) == std::tolower(query[q_i])) {
            score += 10 + consecutive * 5;
            consecutive++;
            q_i++;
        } else {
            consecutive = 0;
            score -= 1;
        }
    }

    if (q_i != query.size()) return std::numeric_limits<int>::min();
    score -= static_cast<int>(candidate.size());

    return score;
}

namespace ui {
    std::string models_query{};
    models::gid selected_model{
        .generation = (uint8_t)-1,
        .id = (uint32_t)-1,
    };

    void models_pane() {
        ImGui::InputText("Search", &models_query);

        std::vector<std::pair<models::gid, uint32_t>> matches{};
        if (models_query.empty()) {
            auto& scene = scene::selected_scene();

            if (selected_model.generation != (uint8_t)-1 && selected_model.id != -1) {
                matches.emplace_back(selected_model, std::numeric_limits<int32_t>::max());
            }

            for (const auto& gid : scene.used_models) {
                if (gid.id == selected_model.id && gid.generation == selected_model.generation) continue;

                auto score = score_search(models_query, **models::get_name(gid));
                if (score > std::numeric_limits<int32_t>::min()) {
                    matches.emplace_back(gid, score);
                }
            }
        } else {
            const auto& names = models::get_names();
            for (uint32_t i = 0; i < names.size(); i++) {
                auto score = score_search(models_query, names[i]);
                if (selected_model == models::gid{models::get_generation(i), i}) {
                    score = std::numeric_limits<int32_t>::max();
                }

                if (score > std::numeric_limits<int32_t>::min()) {
                    matches.emplace_back(models::gid{models::get_generation(i), i}, score);
                }
            }
        }

        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        for (auto [gid, _] : matches) {
            model_entry(gid);
        }
    }

    void model_entry(models::gid gid) {
        ImGui::PushID(&gid);
        auto name = *models::get_name(gid);

        bool double_click = false;
        (ImGui::Selectable("##region", selected_model == gid,
                           ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                           ImVec2(0.0, ImGui::GetFrameHeight())));
        if (ImGui::IsItemClicked()) {
            int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
            if (clicks == 1) {
                if (selected_model == gid) {
                    selected_model.generation = -1;
                    selected_model.id = -1;
                } else {
                    selected_model = gid;
                }
            } else if (clicks == 2) {
                double_click = true;
            }
        }

        ImGui::SameLine();
        ImGui::InputText("##name", name);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            selected_model.generation = -1;
            selected_model.id = -1;
        } else if (!ImGui::IsItemActive() && double_click) {
            selected_model = gid;
            scene::selected_scene().add_instance(scene::Instance{
                .model_gid = gid,
                .name = **models::get_name(gid),
                .translate = glm::vec3{0.0f},
                .rotate = glm::vec3{0.0f},
                .scale = glm::vec3{1.0f},
            });
        }

        ImGui::PopID();
    }

    void instances_pane(glm::mat4* transforms) {
        auto& scene = scene::selected_scene();
        for (size_t i = 0; i < scene.instances.size(); i++) {
            i = instance_entry(scene, i, *(transforms + i));
        }
    }

    size_t instance_entry(scene::Scene& current_scene, size_t ix, glm::mat4& transform) {
        ImGui::PushID(ix);
        auto& inst = current_scene.instances[ix];

        if (ImGui::Selectable("", current_scene.selected_instance == ix,
                              ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0.0, ImGui::GetFrameHeight()))) {
            current_scene.selected_instance = ix;
        }

        ImGui::SameLine();
        ImGui::InputText("##name", &inst.name);
        if (ImGui::IsItemClicked()) {
            current_scene.selected_instance = ix;
        }

        ImGui::SameLine();
        if (ImGui::Button("X")) {
            current_scene.remove_instance(ix);

            ix--;
            ImGui::PopID();
            return ix;
        }

        inst.update_transform(transform);

        ImGui::PopID();
        return ix;
    }

    void transform_pane(glm::mat4* transforms) {
        auto& scene = scene::selected_scene();
        if (scene.selected_instance == -1) return;
        auto& instance = scene.instances[scene.selected_instance];

        ImGui::InputText("name: ", &instance.name);

        ImGui::DragFloat3("XYZ", glm::value_ptr(instance.translate), 0.1f, 0.0f, 0.0f, "%.2f");

        ImGui::DragFloat("yaw", &instance.rotate.x, 0.1f, 0.0, 0.0, "%.2f");
        ImGui::DragFloat("pitch", &instance.rotate.y, 0.1f, 0.0, 0.0, "%.2f");
        ImGui::DragFloat("roll", &instance.rotate.z, 0.1f, 0.0, 0.0, "%.2f");

        ImGui::DragFloat3("scale", glm::value_ptr(instance.scale), 0.1f, 0.0f, 0.0f, "%.2f");
    }

    void scene_pane() {
        if (ImGui::Button("+")) {
            scene::emplace_scene("New scene");
            scene::select_scene(scene::get_scenes().size() - 1);
        }

        auto scenes = scene::get_scenes();
        for (size_t i = 0; i < scenes.size(); i++) {
            ImGui::PushID(i);

            if (ImGui::Selectable("", i == scene::selected_scene_ix(),
                                  ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                                  ImVec2(0.0, ImGui::GetFrameHeight()))) {
                scene::select_scene(i);
            }

            ImGui::SameLine();
            if (ImGui::Button("X") && scenes.size() != 1) {
                auto selected_ix = scene::selected_scene_ix();
                if (selected_ix == i) selected_ix = i == 0 ? 0 : i - 1;
                else if (selected_ix > i) selected_ix--;
                scene::select_scene(selected_ix);

                scene::remove_scene(i);

                i--;
                ImGui::PopID();
                continue;
            }

            ImGui::SameLine();
            ImGui::InputText("##name", &scenes[i].name);

            ImGui::PopID();
        }
    }
}
