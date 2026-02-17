#include "ui.hpp"

#include "ImGuizmo/ImGuizmo.h"
#include "goliath/materials.hpp"
#include "goliath/scenes.hpp"
#include "goliath/textures.hpp"
#include "state.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#define IMVIEWGUIZMO_IMPLEMENTATION
#include "ImViewGuizmo/ImViewGuizmo.h"
#undef IMVIEWGUIZMO_IMPLEMENTATION
#include "goliath/camera.hpp"
#include "goliath/engine.hpp"
#include "goliath/imgui_reflection.hpp"
#include "goliath/models.hpp"
#include "goliath/samplers.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
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
    bool skip_game_window = false;
    ImVec2 game_windows[engine::frames_in_flight]{};
    engine::GPUImage game_window_images[engine::frames_in_flight]{};
    VkImageView game_window_image_views[engine::frames_in_flight]{};
    VkDescriptorSet game_window_textures[engine::frames_in_flight]{};
    VkDescriptorSet game_window_textures_freeup[engine::frames_in_flight]{};
    std::pair<bool, VkImageMemoryBarrier2> game_window_barriers[engine::frames_in_flight]{};
    VkSampler game_window_sampler;
    glm::vec2 game_image_offset{0.0f};
    glm::vec2 game_image_dims{0.0f};

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

    void init() {
        game_window_sampler = engine::samplers::get(0);

        for (size_t i = 0; i < engine::frames_in_flight; i++) {
            game_windows[i].x = -1.0f;
            game_windows[i].y = -1.0f;
        }
    }

    void destroy() {
        for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
            engine::gpu_image::destroy(game_window_images[i]);
            engine::gpu_image_view::destroy(game_window_image_views[i]);
            ImGui_ImplVulkan_RemoveTexture(game_window_textures[i]);
            ImGui_ImplVulkan_RemoveTexture(game_window_textures_freeup[i]);
        }
    }

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

    bool game_window() {
        auto curr_frame = engine::get_current_frame();
        auto& game_window_ = game_windows[curr_frame];
        auto& game_window_image = game_window_images[curr_frame];
        auto& game_window_image_view = game_window_image_views[curr_frame];
        auto& game_window_texture = game_window_textures[curr_frame];

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGuiDockNode* node = ImGui::GetWindowDockNode()) {
            node->LocalFlags |= ImGuiDockNodeFlags_NoDockingOverCentralNode | ImGuiDockNodeFlags_NoTabBar;
        }

        bool focused = ImGui::IsWindowFocused();

        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 avail = ImGui::GetWindowSize();
        skip_game_window = avail.x <= 0 || avail.y <= 0;

        ImGui_ImplVulkan_RemoveTexture(game_window_textures_freeup[engine::get_current_frame()]);
        game_window_textures_freeup[engine::get_current_frame()] = nullptr;
        if ((avail.x != game_window_.x || avail.y != game_window_.y) && !skip_game_window) {
            engine::gpu_image::destroy(game_window_image);
            engine::gpu_image_view::destroy(game_window_image_view);
            game_window_textures_freeup[(engine::get_current_frame() + 1) % engine::frames_in_flight] =
                game_window_texture;

            game_window_image =
                engine::gpu_image::upload(std::format("Game window texture #{}", curr_frame).c_str(),
                                          engine::GPUImageInfo{}
                                              .new_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
                                              .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                              .width(avail.x)
                                              .height(avail.y)
                                              .format(VK_FORMAT_R8G8B8A8_UNORM)
                                              .usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
                                          VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

            game_window_image_view = engine::gpu_image_view::create(
                engine::GPUImageView{game_window_image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));

            game_window_texture = ImGui_ImplVulkan_AddTexture(game_window_sampler, game_window_image_view,
                                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            game_window_ = avail;
        }

        if (!skip_game_window) {
            ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::Image(game_window_texture, game_window_);
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

            // ImViewGuizmo uses some weird coordinate scheme, so I have to change from OpenGL to up = -Y, and forward =
            // +Z
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
                ImGuizmo::SetRect(win_pos.x + cursor.x + game_image_offset.x,
                                  win_pos.y + cursor.y + game_image_offset.y, game_image_dims.x, game_image_dims.y);
                ImGuizmo::SetDrawlist();

                auto proj = cam_info.cam._projection;
                proj[1][1] *= -1;
                changed |= ImGuizmo::Manipulate(
                    glm::value_ptr(cam_info.cam._view), glm::value_ptr(proj),
                    ImGuizmo::TRANSLATE | ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z, ImGuizmo::WORLD,
                    glm::value_ptr(
                        engine::scenes::get_instance_transforms(scene::selected_scene())[scene::selected_instance()]));

                if (changed) {
                    engine::scenes::update_transforms_buffer(scene::selected_scene());
                    scene::update_camera(cam_info);
                    update_instance_transform();
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        return focused;
    }

    bool skipped_game_window() {
        return skip_game_window;
    }

    std::optional<VkImageMemoryBarrier2> blit_game_window(VkBlitImageInfo2 blit_info) {
        if (ui::skipped_game_window()) return std::nullopt;

        auto curr_frame = engine::get_current_frame();
        auto& game_window_ = game_windows[curr_frame];
        auto& game_window_image = game_window_images[curr_frame];

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.pNext = nullptr;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = game_window_image.image;
        barrier.subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        VkClearColorValue clear_color{};
        clear_color.float32[0] = 0.0f;
        clear_color.float32[1] = 0.0f;
        clear_color.float32[2] = 0.0f;
        clear_color.float32[3] = 255.0f;
        VkImageSubresourceRange clear_range = barrier.subresourceRange;
        vkCmdClearColorImage(engine::get_cmd_buf(), game_window_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear_color, 1, &clear_range);

        engine::synchronization::begin_barriers();
        engine::synchronization::apply_barrier(barrier);
        engine::synchronization::end_barriers();

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

        blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blit_info.dstImage = game_window_image.image;
        blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        VkImageBlit2 region = blit_info.pRegions[0];

        game_image_dims = {region.srcOffsets[1].x, region.srcOffsets[1].y};
        glm::vec2 window{game_window_.x, game_window_.y};

        float scale = std::min(window.x / game_image_dims.x, window.y / game_image_dims.y);
        if (scale > 1.0) {
            scale = floor(scale);
        }

        game_image_dims *= scale;
        game_image_offset = (window - game_image_dims) * 0.5f;

        region.dstOffsets[0] = VkOffset3D{
            .x = (int32_t)game_image_offset.x,
            .y = (int32_t)game_image_offset.y,
            .z = 0,
        };
        region.dstOffsets[1] = VkOffset3D{
            .x = region.dstOffsets[0].x + (int32_t)game_image_dims.x,
            .y = region.dstOffsets[0].y + (int32_t)game_image_dims.y,
            .z = 1,
        };
        region.dstSubresource = VkImageSubresourceLayers{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        blit_info.pRegions = &region;

        vkCmdBlitImage2(engine::get_cmd_buf(), &blit_info);

        engine::synchronization::begin_barriers();
        engine::synchronization::apply_barrier(barrier);
        engine::synchronization::end_barriers();

        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        return barrier;
    }

    ImVec2 game_window_size() {
        return game_windows[engine::get_current_frame()];
    }

    engine::GPUImage get_window_image() {
        return game_window_images[engine::get_current_frame()];
    }

    using scored_entry = std::pair<std::variant<engine::models::gid, engine::textures::gid>, int32_t>;

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
                auto score = score_search(query, names[i]);
                if (score > std::numeric_limits<int32_t>::min()) {
                    out.emplace_back(engine::models::gid{engine::models::get_generation(i), i}, score);
                }
            }
        }
    }

    void score_textures(bool current_scene, const std::string& query, std::vector<scored_entry>& out) {
        if (current_scene) {

        } else {
            auto names = engine::textures::get_names();
            for (uint32_t i = 0; i < names.size(); i++) {
                auto score = score_search(query, names[i]);
                if (score > std::numeric_limits<int32_t>::min()) {
                    out.emplace_back(engine::textures::gid{engine::textures::get_generation(i), i}, score);
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
        std::array<const char*, 3> combo_values = {"All assets", "Models", "Textues"};
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
        if (ImGui::BeginPopupContextItem("TextureEntryContextMenu")) {
            if (ImGui::MenuItem("Rename")) {
                rename_tmp = name;
                rename_dst = [gid](const auto& str) {
                    if (auto name = engine::models::get_name(gid); name) {
                        **name = str;
                        engine::models::modified();
                    }
                };
            }

            ImGui::EndPopup();
        }
    }

    void assets_entry_pre(engine::textures::gid gid, uint32_t ix) {
        const auto& name = **engine::textures::get_name(gid);
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

    void assets_entry_post(engine::textures::gid gid, uint32_t ix) {
        const auto& name = **engine::textures::get_name(gid);

        if (ImGui::BeginPopupContextItem("TextureEntryContextMenu")) {
            if (ImGui::MenuItem("Rename")) {
                rename_tmp = name;
                rename_dst = [gid](const auto& str) {
                    if (auto name = engine::textures::get_name(gid); name) {
                        **name = str;
                        engine::textures::modified();
                    }
                };
            }

            ImGui::EndPopup();
        }
    }

    void assets_entry_drag_preview(engine::models::gid gid) {
        ImGui::SetDragDropPayload("model", &gid, sizeof(gid));
    }

    void assets_entry_drag_preview(engine::textures::gid gid) {
        ImGui::SetDragDropPayload("texture", &gid, sizeof(gid));
    }

    void instances_pane() {
        size_t to_delete = -1;

        auto scenes = engine::scenes::get_names();
        if (ImGui::BeginCombo("##scene_picker", scenes[scene::selected_scene()].c_str())) {
            if (ImGui::Button("New scene##new_scene", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                scene::add("New scene");

                scenes = engine::scenes::get_names();
                ImGui::CloseCurrentPopup();
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

                    if (ImGui::MenuItem("Delete")) {
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

        const auto model_ = engine::models::get_cpu_model(
            engine::scenes::get_instance_models(scene::selected_scene())[scene::selected_instance()]);
        if (!model_.has_value() || model_.value() == nullptr) return;
        const auto& model = **model_;

        bool modified = false;
        for (size_t m = 0; m < model.mesh_count; m++) {
            const auto& mesh = model.meshes[m];

            const auto& schema = engine::materials::get_schema(mesh.material_id);
            auto material_data = engine::materials::get_instance_data(mesh.material_id, mesh.material_instance);

            if (ImGui::CollapsingHeader(std::format("Mesh #{}", m).c_str())) {
                modified |= material_inputs(schema, material_data);
            }

            engine::materials::update_instance_data(mesh.material_id, mesh.material_instance, material_data.data());
        }
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

                            engine::imgui_reflection::input(name.c_str(), im, (typename VecData::Component*)data_ptr,
                                                            {VecData::dimension, 1});
                        } else if constexpr (engine::util::is_mat_v<Attr>) {
                            using MatData = engine::util::mat_data<Attr>;

                            engine::imgui_reflection::input(name.c_str(), im, (typename MatData::Component*)data_ptr,
                                                            MatData::dimension);
                        } else if constexpr (std::same_as<engine::textures::gid, Attr>) {
                            ImGui::InputText(name.c_str(), *engine::textures::get_name(*data_ptr),
                                             ImGuiInputTextFlags_ReadOnly);
                            if (ImGui::BeginDragDropTarget()) {
                                if (auto payload = ImGui::AcceptDragDropPayload("texture");
                                    payload != nullptr && payload->IsDelivery()) {
                                    auto gid = *(engine::textures::gid*)payload->Data;

                                    *data_ptr = gid;
                                    // TODO: acquire texture @gid, then release current texture @*data_ptr - move material
                                    // textures from gpu group to a separate thing
                                }

                                ImGui::EndDragDropTarget();
                            }
                        } else {
                            engine::imgui_reflection::input(name.c_str(), im, data_ptr);
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
}
