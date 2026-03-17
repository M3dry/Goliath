#include "ui.hpp"

#include "ImGuizmo/ImGuizmo.h"
#include "goliath/assets.hpp"
#include "goliath/dependency_graph.hpp"
#include "goliath/materials.hpp"
#include "goliath/scenes.hpp"
#include "goliath/textures.hpp"
#include "state.hpp"
#include "textures.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#define IMVIEWGUIZMO_IMPLEMENTATION
#include "ImViewGuizmo/ImViewGuizmo.h"
#undef IMVIEWGUIZMO_IMPLEMENTATION
#include "goliath/camera.hpp"
#include "goliath/imgui_reflection.hpp"
#include "goliath/models.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "scene.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <vulkan/vulkan_core.h>

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
    struct SelectedInstance {
        size_t scene;
        size_t instance;
        float timer = 0.1;

        void reset_timer() {
            timer = 0.1;
        }
    };

    std::optional<SelectedInstance> transform_value_changed{};

    std::string rename_tmp{};
    using RenameDstFn = std::function<void(const std::string&)>;
    RenameDstFn rename_dst{};

    std::optional<size_t> open_scenes_settings{};

    void update_instance_transform() {
        if (transform_value_changed && transform_value_changed->scene == scene::selected_scene() &&
            transform_value_changed->instance == scene::selected_instance()) {
            transform_value_changed->reset_timer();
            return;
        } else if (transform_value_changed != std::nullopt) {
            engine::scenes::modified(transform_value_changed->scene);
            return;
        }

        transform_value_changed = {
            scene::selected_scene(),
            scene::selected_instance(),
        };
    }

    void init() {}

    void destroy() {}

    void tick(float dt) {
        if (transform_value_changed) {
            if ((transform_value_changed->timer -= dt) <= 0) {
                engine::scenes::modified(transform_value_changed->scene);
                transform_value_changed = std::nullopt;
            }
        }
    }

    void begin() {
        ImGuizmo::BeginFrame();
        ImViewGuizmo::BeginFrame();

        auto& style = ImViewGuizmo::GetStyle();
        style.scale = 0.8;

        style.labelSize = 1.5;
        style.labelColor = IM_COL32(0, 0, 0, 255);

        style.toolButtonColor = IM_COL32(0, 0, 0, 130);
        style.toolButtonHoveredColor = IM_COL32(75, 75, 75, 230);
        style.toolButtonIconColor = IM_COL32(255, 255, 255, 255);
    }

    void viewport_window(GameView& scene_viewport, bool& scene_focused, GameView* game_viewport, bool* game_focused) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGuiDockNode* node = ImGui::GetWindowDockNode()) {
            node->LocalFlags |= ImGuiDockNodeFlags_NoDockingOverCentralNode;
            node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        }

        scene_focused = false;
        if (game_focused) *game_focused = false;
        if (ImGui::BeginTabBar("viewport_tabbar", ImGuiTabBarFlags_Reorderable)) {
            if (ImGui::BeginTabItem("Scene")) {
                ImVec2 win_pos = ImGui::GetWindowPos();
                ImVec2 cursor = ImGui::GetCursorPos();
                ImVec2 avail = ImGui::GetContentRegionAvail();

                scene_viewport.process_pane(avail);
                scene_viewport.draw_pane();
                auto image_size = ImGui::GetItemRectSize();
                scene_focused = ImGui::IsWindowFocused();

                if (ImGui::BeginDragDropTarget()) {
                    auto payload = ImGui::AcceptDragDropPayload("model");
                    if (payload != nullptr && payload->IsDelivery()) {
                        auto gid = *(engine::models::gid*)payload->Data;
                        scene::add_instance(gid);
                    }
                    ImGui::EndDragDropTarget();
                }

                ImVec2 gizmo_offset{win_pos.x + cursor.x, win_pos.y + cursor.y};
                avail.x -= cursor.x;
                avail.y -= cursor.y;
                auto scale = ImViewGuizmo::GetStyle().scale;

                // ImViewGuizmo uses some weird coordinate scheme, so I have to change from OpenGL to up = -Y, and
                // forward = +Z
                static const glm::mat4 GL_TO_GIZMO = glm::mat4{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};

                static const glm::mat4 GIZMO_TO_GL = glm::inverse(GL_TO_GIZMO);

                auto cam_info = scene::camera();
                glm::mat4 view_gizmo = GL_TO_GIZMO * cam_info.cam.view() * GIZMO_TO_GL;
                auto gizmo_quat = glm::quat_cast(glm::inverse(view_gizmo));
                auto gizmo_pos = glm::vec3{GL_TO_GIZMO * glm::vec4{cam_info.cam.position, 1.0f}};

                bool changed =
                    ImViewGuizmo::Rotate(gizmo_pos, gizmo_quat, glm::vec3{0.0},
                                         ImVec2{gizmo_offset.x + (avail.x - scale * 90), gizmo_offset.y + scale * 90});
                changed |= ImViewGuizmo::Pan(
                    gizmo_pos, gizmo_quat,
                    ImVec2{gizmo_offset.x + (avail.x - scale * 50 - 10), gizmo_offset.y + (avail.y - scale * 50 - 10)});
                changed |= ImViewGuizmo::Dolly(gizmo_pos, gizmo_quat,
                                               ImVec2{gizmo_offset.x + (avail.x - scale * 50 - 10),
                                                      gizmo_offset.y + (avail.y - scale * 50 - 10 - scale * 50 - 10)});

                if (changed) {
                    cam_info.cam._orientation =
                        glm::normalize(glm::quat_cast(GIZMO_TO_GL * glm::mat4_cast(gizmo_quat) * GL_TO_GIZMO));
                    cam_info.cam.position = glm::vec3{GIZMO_TO_GL * glm::vec4{gizmo_pos, 1.0f}};

                    cam_info.cam.update_matrices();
                    scene::update_camera(cam_info);
                }

                if (scene::selected_instance() != -1) {
                    auto win_pos = ImGui::GetWindowPos();

                    changed = false;
                    ImGuizmo::SetOrthographic(false);
                    ImGuizmo::AllowAxisFlip(false);
                    ImGuizmo::SetRect(win_pos.x + cursor.x, win_pos.y + cursor.y, image_size.x, image_size.y);
                    ImGuizmo::SetDrawlist();

                    auto proj = cam_info.cam._projection;
                    proj[1][1] *= -1;
                    changed |= ImGuizmo::Manipulate(glm::value_ptr(cam_info.cam._view), glm::value_ptr(proj),
                                                    ImGuizmo::TRANSLATE | ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y |
                                                        ImGuizmo::ROTATE_Z,
                                                    ImGuizmo::WORLD,
                                                    glm::value_ptr(engine::scenes::get_instance_transforms(
                                                        scene::selected_scene())[scene::selected_instance()]));

                    if (changed) {
                        engine::scenes::update_transforms_buffer(scene::selected_scene());
                        scene::update_camera(cam_info);
                        update_instance_transform();
                    }
                }

                ImGui::EndTabItem();
            } else {
                scene_viewport.process_pane(ImVec2{0, 0});
            }

            if (game_viewport && ImGui::BeginTabItem("Game")) {
                game_viewport->process_pane(ImGui::GetContentRegionAvail());
                game_viewport->draw_pane();
                *game_focused = ImGui::IsWindowFocused();

                ImGui::EndTabItem();
            } else if (game_viewport) {
                game_viewport->process_pane(ImVec2{0, 0});
            }

            ImGui::EndTabBar();
        } else {
            scene_viewport.process_pane(ImVec2{0, 0});
            if (game_viewport) game_viewport->process_pane(ImVec2{0, 0});
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    using scored_entry = std::pair<engine::DependencyGraph::AssetGID, int32_t>;

    void score_models(bool current_scene, const std::string& query, std::vector<scored_entry>& out) {
        if (current_scene) {
            for (const auto& gid : engine::scenes::get_used_models(scene::selected_scene())) {
                auto score = score_search(query, **engine::models::get_name(gid));
                if (score > std::numeric_limits<int32_t>::min()) {
                    out.emplace_back(gid, score);
                }
            }
        } else {
            auto names = engine::models::get_names();
            for (uint32_t i = 0; i < names.size(); i++) {
                auto gid = engine::models::gid{engine::models::get_generation(i), i};
                if (engine::models::is_deleted(gid)) continue;

                auto score = score_search(query, names[i]);
                if (score > std::numeric_limits<int32_t>::min()) {
                    out.emplace_back(gid, score);
                }
            }
        }
    }

    void score_materials(bool current_scene, const std::string& query, std::vector<scored_entry>& out) {
        if (current_scene) {

        } else {
            state::materials->get_names([&](const auto& name, auto gid) {
                auto score = score_search(query, name);
                if (score > std::numeric_limits<int32_t>::min()) {
                    out.emplace_back(gid, score);
                }
            });
        }
    }

    void score_textures(bool current_scene, const std::string& query, std::vector<scored_entry>& out) {
        if (current_scene) {

        } else {
            auto names = game_textures->get_names();
            for (uint32_t i = 0; i < names.size(); i++) {
                auto gid = engine::Textures::gid{game_textures->get_generation(i), i};
                if (game_textures->is_deleted(gid)) continue;

                auto score = score_search(query, names[i]);
                if (score > std::numeric_limits<int32_t>::min()) {
                    out.emplace_back(engine::Textures::gid{game_textures->get_generation(i), i}, score);
                }
            }
        }
    }

    void assets_pane() {
        if (ImGui::InputText("##search", &state::models_query)) {
            state::modified_value();
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);
        std::array<const char*, 4> combo_values = {"All assets", "Models", "Materials", "Textues"};
        if (ImGui::BeginCombo("##scope", combo_values[state::assets_scope], ImGuiComboFlags_NoPreview)) {
            for (size_t i = 0; i < combo_values.size(); i++) {
                if (ImGui::Selectable(combo_values[i], state::assets_scope == i)) {
                    state::assets_scope = i;
                    state::modified_value();
                }

                if (state::assets_scope == i) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::Separator();
            ImGui::Checkbox("Current scene only", &state::assets_scene_only_scope);
            ImGui::EndCombo();
        }

        std::vector<scored_entry> matches{};
        if (state::assets_scope == 0 || state::assets_scope == 1) {
            score_models(state::assets_scene_only_scope, state::models_query, matches);
        }
        if (state::assets_scope == 0 || state::assets_scope == 2) {
            score_materials(state::assets_scene_only_scope, state::models_query, matches);
        }
        if (state::assets_scope == 0 || state::assets_scope == 3) {
            score_textures(state::assets_scene_only_scope, state::models_query, matches);
        }

        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        ImGui::Separator();

        if (ImGui::BeginTable("##search_results", 2,
                              ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingStretchSame)) {
            uint32_t i = 0;
            for (auto [gid, _] : matches) {
                ImGui::PushID(i);
                if (i % 2 == 0) ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImVec2 start_pos = ImGui::GetCursorScreenPos();
                float available_width = ImGui::GetContentRegionAvail().x;

                auto* draw_list = ImGui::GetWindowDrawList();
                draw_list->ChannelsSplit(2);
                draw_list->ChannelsSetCurrent(1);

                ImGui::BeginGroup();
                std::visit([&i](auto gid) { assets_entry_pre(gid, i); }, gid);
                ImGui::EndGroup();

                ImVec2 content_size = ImGui::GetItemRectSize();

                draw_list->ChannelsSetCurrent(0);
                ImGui::SetCursorScreenPos(start_pos);
                ImGui::InvisibleButton("##drag_surface", ImVec2(available_width, content_size.y));
                draw_list->AddRectFilled(
                    start_pos, ImVec2(start_pos.x + available_width, start_pos.y + content_size.y),
                    ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_HeaderHovered : ImGuiCol_Header));
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    std::visit([](auto gid) { assets_entry_drag_preview(gid); }, gid);

                    ImGui::EndDragDropSource();
                }

                ImGui::BeginGroup();
                std::visit([&i](const auto& gid) { assets_entry_post(gid, i); }, gid);
                ImGui::EndGroup();

                draw_list->ChannelsMerge();

                ImGui::PopID();
                i++;
            }

            ImGui::EndTable();
        }
    }

    void assets_entry_pre(engine::models::gid gid, uint32_t ix) {
        const auto& name = **engine::models::get_name(gid);
        ImGui::TextWrapped("%s", name.c_str());
    }

    void assets_entry_pre(engine::Materials::gid gid, uint32_t ix) {
        ImGui::TextWrapped("%s", state::materials->get_name(gid)->get().c_str());
    }

    void assets_entry_pre(engine::Textures::gid gid, uint32_t ix) {
        const auto& name = **game_textures->get_name(gid);
        ImGui::TextWrapped("%s", name.c_str());
    }

    void assets_entry_post(engine::models::gid gid, uint32_t ix) {
        const auto& name = **engine::models::get_name(gid);

        bool add = false;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered()) {
            add = true;
        }

        if (ImGui::BeginPopupContextItem("ModelEntryContextMenu")) {
            if (ImGui::MenuItem("Rename")) {
                rename_tmp = name;
                rename_dst = [gid](const auto& str) {
                    if (auto name = engine::models::get_name(gid); name) {
                        **name = str;
                        engine::models::modified();
                    }
                };
            }

            if (ImGui::MenuItem("Add to scene")) {
                add = true;
            }

            ImGui::EndPopup();
        }

        if (add) {
            scene::add_instance(gid);
        }
    }

    void assets_entry_post(engine::Materials::gid gid, uint32_t ix) {
        const auto& name = state::materials->get_name(gid)->get().c_str();

        if (ImGui::BeginPopupContextItem("MaterialEntryContextMenu")) {
            if (ImGui::MenuItem("Rename")) {
                rename_tmp = name;
                rename_dst = [gid](const auto& str) {
                    if (auto name = state::materials->get_name(gid); name) {
                        name->get() = str;
                        state::materials->modified();
                    }
                };
            }

            if (ImGui::MenuItem("Modify")) {
                if (std::find(state::opened_material_instances.begin(), state::opened_material_instances.end(), gid) ==
                    state::opened_material_instances.end()) {
                    state::opened_material_instances.emplace_back(gid);
                }
            }

            ImGui::EndPopup();
        }
    }

    void assets_entry_post(engine::Textures::gid gid, uint32_t ix) {
        const auto& name = **game_textures->get_name(gid);

        if (ImGui::BeginPopupContextItem("TextureEntryContextMenu")) {
            if (ImGui::MenuItem("Rename")) {
                rename_tmp = name;
                rename_dst = [gid](const auto& str) {
                    if (auto name = game_textures->get_name(gid); name) {
                        **name = str;
                        game_textures->modified();
                    }
                };
            }

            ImGui::EndPopup();
        }
    }

    void assets_entry_drag_preview(engine::models::gid gid) {
        ImGui::SetDragDropPayload("model", &gid, sizeof(gid));
    }

    void assets_entry_drag_preview(engine::Materials::gid gid) {
        ImGui::SetDragDropPayload("material", &gid, sizeof(gid));
    }

    void assets_entry_drag_preview(engine::Textures::gid gid) {
        ImGui::SetDragDropPayload("texture", &gid, sizeof(gid));
    }

    void instances_pane() {
        size_t to_delete = -1;

        auto scenes = engine::scenes::get_names();
        if (ImGui::BeginCombo("##scene_picker", scenes[scene::selected_scene()].c_str())) {
            auto button_size = ImVec2(ImGui::GetContentRegionAvail().x / 2.0f, 0);
            if (ImGui::Button("New scene##new_scene", ImVec2(ImGui::GetContentRegionAvail().x / 2.0f, 0))) {
                scene::add("New scene");

                scenes = engine::scenes::get_names();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Settings##settings", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                open_scenes_settings = -1;
            }

            ImGui::Separator();

            for (size_t i = 0; i < scenes.size(); i++) {
                ImGui::PushID(i);
                if (ImGui::Selectable(scenes[i].c_str(), i == scene::selected_scene())) {
                    scene::select_scene(i);
                }

                if (ImGui::BeginPopupContextItem("SceneEntryContextMenu")) {
                    if (ImGui::MenuItem("Rename")) {
                        rename_tmp = scenes[i];
                        rename_dst = [i](const auto& str) {
                            engine::scenes::get_name(i) = str;
                            engine::scenes::modified(i);
                        };
                    }

                    if (scenes.size() != 1 && ImGui::MenuItem("Delete")) {
                        to_delete = i;
                    }

                    ImGui::EndPopup();
                }

                if (scene::selected_scene() == i) {
                    ImGui::SetItemDefaultFocus();
                }

                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        if (to_delete != -1) scene::remove(to_delete);

        ImGui::Separator();

        size_t counter = 0;
        for (size_t i = 0; i < engine::scenes::get_instance_names(scene::selected_scene()).size(); i++) {
            ImGui::PushID(counter);

            i = instance_entry(scene::selected_scene(), i);

            ImGui::PopID();
            counter++;
        }
    }

    size_t instance_entry(size_t scene_ix, size_t instance_ix) {
        auto& inst_name = engine::scenes::get_instance_names(scene_ix)[instance_ix];

        if (ImGui::Selectable("", scene::selected_instance() == instance_ix, ImGuiSelectableFlags_SpanAllColumns)) {
            scene::select_instance(scene::selected_instance() == instance_ix ? -1 : instance_ix);
        }
        if (ImGui::BeginPopupContextItem("InstanceEntryContextMenu")) {
            if (ImGui::MenuItem("Rename")) {
                rename_tmp = inst_name;
                rename_dst = [scene_ix, instance_ix](const auto& str) {
                    engine::scenes::get_instance_names(scene_ix)[instance_ix] = str;
                    engine::scenes::modified(scene_ix);
                };
            }
            if (ImGui::MenuItem("Delete")) {
                scene::remove_instance(instance_ix);

                ImGui::EndPopup();
                return instance_ix - 1;
            }

            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextWrapped("%s", inst_name.c_str());

        return instance_ix;
    }

    void transform_pane() {
        if (scene::selected_instance() == -1) return;
        auto& inst_name = engine::scenes::get_instance_names(scene::selected_scene())[scene::selected_instance()];
        auto& inst = engine::scenes::get_instance_transforms(scene::selected_scene())[scene::selected_instance()];

        glm::vec3 translate{};
        glm::vec3 rotate{};
        glm::vec3 scale{};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(inst), glm::value_ptr(translate), glm::value_ptr(rotate),
                                              glm::value_ptr(scale));

        bool value_changed = false;

        value_changed |= ImGui::InputText("name: ", &inst_name);

        value_changed |= ImGui::DragFloat3("XYZ", glm::value_ptr(translate), 0.1f, 0.0f, 0.0f, "%.2f");

        value_changed |= ImGui::DragFloat("yaw", &rotate.y, 0.1f, 0.0, 0.0, "%.2f");
        value_changed |= ImGui::DragFloat("pitch", &rotate.x, 0.1f, 0.0, 0.0, "%.2f");
        value_changed |= ImGui::DragFloat("roll", &rotate.z, 0.1f, 0.0, 0.0, "%.2f");

        value_changed |= ImGui::DragFloat3("scale", glm::value_ptr(scale), 0.1f, 0.0f, 0.0f, "%.2f");

        ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(translate), glm::value_ptr(rotate),
                                                glm::value_ptr(scale), glm::value_ptr(inst));

        if (!value_changed) return;

        engine::scenes::update_transforms_buffer(scene::selected_scene());
        update_instance_transform();
    }

    void selected_model_materials_pane() {
        if (scene::selected_instance() == -1) return;

        auto mgid = engine::scenes::get_instance_models(scene::selected_scene())[scene::selected_instance()];
        const auto model_ = engine::models::get_cpu_model(mgid);
        if (!model_.has_value() || model_.value() == nullptr) return;
        const auto& model = **model_;

        for (size_t m = 0; m < model.mesh_count; m++) {
            auto mat_gid = model.meshes[m].material_instance;

            auto name_ = state::materials->get_name(mat_gid);
            std::string name{};
            if (name_) {
                name = name_->get();
            }

            ImGui::InputText(std::format("Mesh #{}", m).c_str(), &name, ImGuiInputTextFlags_ReadOnly);

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered()) {
                state::opened_material_instances.emplace_back(mat_gid);
            }

            if (ImGui::BeginDragDropTarget()) {
                if (auto payload = ImGui::AcceptDragDropPayload("material");
                    payload != nullptr && payload->IsDelivery()) {
                    auto gid = *(engine::Materials::gid*)payload->Data;

                    if (gid != mat_gid) {
                        state::materials->acquire_instance(gid);
                        state::materials->release_instance(model.meshes[m].material_instance);

                        state::materials->with_textures([](auto t_gid) { game_textures->acquire({&t_gid, 1}); }, gid);
                        state::materials->with_textures([](auto t_gid) { game_textures->release({&t_gid, 1}); },
                                                        model.meshes[m].material_instance);

                        model.meshes[m].material_instance = gid;

                        engine::models::reupload(mgid);
                        engine::models::modified();
                    }
                }

                ImGui::EndDragDropTarget();
            }
        }
    }

    void material_windows() {
        bool modified = false;
        std::erase_if(state::opened_material_instances, [&](auto mat_gid) -> bool {
            auto name_ = state::materials->get_name(mat_gid);
            if (!name_) return true;
            std::string name = name_->get();

            bool opened = true;
            if (ImGui::Begin(name.c_str(), &opened)) {
                auto schema = state::materials->get_schema(mat_gid.dim());
                auto data = state::materials->get_instance_data(mat_gid);
                if (!schema || !data) {
                    ImGui::End();
                    return true;
                }

                if (material_inputs(*schema, *data)) {
                    modified = true;
                    state::materials->update_instance_data(mat_gid, data->data());
                }
            }
            ImGui::End();

            return !opened;
        });

        if (modified) state::materials->modified();
    }

    bool material_inputs(const engine::Material& schema, std::span<uint8_t> data) {
        bool modified = false;
        size_t offset = 0;

        float avail = ImGui::GetContentRegionAvail().x;
        float item_width = 500.0f;
        int columns = std::min(std::max(1, int(avail / item_width)), 4);

        if (ImGui::BeginTable("grid", columns)) {
            constexpr auto im = engine::imgui_reflection::Input{};
            for (size_t i = 0; i < schema.attributes.size(); i++) {
                auto& name = schema.names[i];

                ImGui::TableNextColumn();
                ImGui::PushItemWidth(std::max<float>(item_width - ImGui::CalcTextSize(name.c_str()).x, 0));
                engine::material::visit(
                    [&]<typename Attr>() {
                        auto* data_ptr = (Attr*)(data.data() + offset);

                        if constexpr (engine::util::is_vec_v<Attr>) {
                            using VecData = engine::util::vec_data<Attr>;

                            modified |= engine::imgui_reflection::input(
                                name.c_str(), im, (typename VecData::Component*)data_ptr, {VecData::dimension, 1});
                        } else if constexpr (engine::util::is_mat_v<Attr>) {
                            using MatData = engine::util::mat_data<Attr>;

                            modified |= engine::imgui_reflection::input(
                                name.c_str(), im, (typename MatData::Component*)data_ptr, MatData::dimension);
                        } else if constexpr (std::same_as<engine::Textures::gid, Attr>) {
                            ImGui::InputText(name.c_str(), *game_textures->get_name(*data_ptr),
                                             ImGuiInputTextFlags_ReadOnly);
                            if (ImGui::BeginDragDropTarget()) {
                                if (auto payload = ImGui::AcceptDragDropPayload("texture");
                                    payload != nullptr && payload->IsDelivery()) {
                                    auto gid = *(engine::Textures::gid*)payload->Data;

                                    game_textures->release({data_ptr, 1});
                                    *data_ptr = gid;
                                    game_textures->acquire({data_ptr, 1});

                                    modified = true;
                                }

                                ImGui::EndDragDropTarget();
                            }
                        } else {
                            modified |= engine::imgui_reflection::input(name.c_str(), im, data_ptr);
                        }

                        offset += sizeof(Attr);
                    },
                    schema.attributes[i]);
                ImGui::PopItemWidth();
            }

            ImGui::EndTable();
        }

        return modified;
    }

    void rename_popup() {
        if (rename_dst != nullptr) {
            ImGui::OpenPopup("RenamePopup");
        }

        if (ImGui::BeginPopupModal("RenamePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            assert(rename_dst != nullptr);

            ImGui::Text("Rename:");
            ImGui::InputText("##rename", &rename_tmp);

            if (ImGui::Button("Rename")) {
                rename_dst(rename_tmp);
                rename_tmp = "";
                rename_dst = nullptr;

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                rename_dst = nullptr;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void scenes_settings_pane() {
        if (!open_scenes_settings) return;

        bool opened = true;
        if (ImGui::Begin("Scene settings", &opened)) {
            if (*open_scenes_settings == -1) {
                const auto& names = engine::scenes::get_names();

                for (size_t i = 0; i < names.size(); i++) {
                    if (ImGui::CollapsingHeader(names[i].c_str(),
                                                (i == scene::selected_scene() ? ImGuiTreeNodeFlags_DefaultOpen
                                                                              : ImGuiTreeNodeFlags_None))) {
                        scene_settings_pane(i);
                    }
                }
            } else {
                scene_settings_pane(*open_scenes_settings);
            }
        }
        ImGui::End();
        if (!opened) open_scenes_settings = std::nullopt;
    }

    void scene_settings_pane(size_t scene_ix) {
        auto& name = engine::scenes::get_name(scene_ix);
        auto& info = scene::get_camera_infos(scene_ix);

        static glm::vec3 look_at{0};
        bool changed = false;
        bool update_matrices = false;

        changed |= ImGui::InputText("Name", &name);
        changed |= ImGui::DragFloat("Sensitivity", &info.sensitivity, 0.05f, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::DragFloat("Movement speed", &info.movement_speed, 0.1f, 0.0f, 10.0f, "%.1f");
        auto fov_changed = ImGui::DragFloat("FOV", &info.fov, 1.0f, 0.0f, 180.0f, "%.1f");
        changed |= fov_changed;
        update_matrices |= ImGui::DragFloat3("Position", glm::value_ptr(info.cam.position), 0.5f);
        changed |= update_matrices;

        ImGui::InputFloat3("##look_at", glm::value_ptr(look_at));
        ImGui::SameLine();
        if (ImGui::Button("Look at")) {
            info.cam.look_at(look_at);
            look_at = {};
            update_matrices = true;
        }

        if (fov_changed) {
            info.cam.set_projection(engine::camera::Perspective{
                .fov = glm::radians(info.fov),
                .aspect_ratio = 16.0f / 9.0f,
            });
            update_matrices = true;
        }

        if (update_matrices) {
            info.cam.update_matrices();
        }

        if (changed) scene::modified();
    }

    void assets_inputs_pane(engine::Assets& assets) {
        auto scene_names = assets.get_scene_names();
        auto model_names = assets.get_model_names();
        auto texture_names = assets.get_texture_names();

        auto scene_gids = assets.get_scene_gids();
        auto model_gids = assets.get_model_gids();
        auto texture_gids = assets.get_texture_gids();

        if (ImGui::CollapsingHeader("Scenes")) {
            for (size_t i = 0; i < scene_names.size(); i++) {
                auto gid = scene_gids[i];
                if (gid == -1) continue;

                ImGui::InputText(scene_names[i].c_str(), &engine::scenes::get_name(gid), ImGuiInputTextFlags_ReadOnly);
                if (ImGui::BeginDragDropTarget()) {
                    if (auto payload = ImGui::AcceptDragDropPayload("scene");
                        payload != nullptr && payload->IsDelivery()) {
                        assets.set(i, *(size_t*)payload->Data);
                    }

                    ImGui::EndDragDropTarget();
                }
            }
        }

        if (ImGui::CollapsingHeader("Models")) {
            for (size_t i = 0; i < model_names.size(); i++) {
                auto gid = model_gids[i];
                if (gid == engine::models::gid{}) continue;

                auto name = engine::models::get_name(gid);
                if (!name.has_value()) {
                    assets.set(i, engine::models::gid{});
                }

                ImGui::InputText(model_names[i].c_str(), *name, ImGuiInputTextFlags_ReadOnly);
                if (ImGui::BeginDragDropTarget()) {
                    if (auto payload = ImGui::AcceptDragDropPayload("model");
                        payload != nullptr && payload->IsDelivery()) {
                        assets.set(i, *(engine::models::gid*)payload->Data);
                    }

                    ImGui::EndDragDropTarget();
                }
            }
        }

        if (ImGui::CollapsingHeader("Textures")) {
            for (size_t i = 0; i < texture_names.size(); i++) {
                auto gid = texture_gids[i];
                if (gid == engine::Textures::gid{}) continue;

                auto name = game_textures->get_name(gid);
                if (!name.has_value()) {
                    assets.set(i, engine::Textures::gid{});
                }

                ImGui::InputText(texture_names[i].c_str(), *name, ImGuiInputTextFlags_ReadOnly);
                if (ImGui::BeginDragDropTarget()) {
                    if (auto payload = ImGui::AcceptDragDropPayload("texture");
                        payload != nullptr && payload->IsDelivery()) {
                        assets.set(i, *(engine::Textures::gid*)payload->Data);
                    }

                    ImGui::EndDragDropTarget();
                }
            }
        }
    }

    struct InstanceCreation {
        uint32_t mat_id;
        std::string name;
    };

    std::vector<InstanceCreation> instance_creations{};

    void new_material_instance_creation() {
        instance_creations.emplace_back(InstanceCreation{
            .mat_id = 0,
            .name = "New material",
        });
    }

    void material_instance_creation() {
        std::erase_if(instance_creations, [](auto& ic) -> bool {
            auto selected_name = state::materials->get_schema_name(ic.mat_id);
            if (!selected_name) {
                ic.mat_id = 0;
                return false;
            }

            bool opened = true;
            if (ImGui::Begin("Material creation", &opened)) {
                if (ImGui::BeginCombo("Schema", selected_name->get().c_str())) {
                    state::materials->get_schema_names([&](const auto& name, uint32_t mat_id) {
                        if (ImGui::Selectable(name.c_str(), mat_id == ic.mat_id)) {
                            ic.mat_id = mat_id;
                        }

                        if (mat_id == ic.mat_id) {
                            ImGui::SetItemDefaultFocus();
                        }
                    });
                    ImGui::EndCombo();
                }

                ImGui::InputText("Instance name", &ic.name);

                if (ImGui::Button("Create")) {
                    state::opened_material_instances.emplace_back(state::materials->add_instance(ic.mat_id, ic.name));
                    opened = false;
                }
            }
            ImGui::End();

            return !opened;
        });
    }

    void material_schema_creation();
}
