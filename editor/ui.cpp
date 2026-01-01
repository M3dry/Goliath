#include "ui.hpp"

#include "ImGuizmo/ImGuizmo.h"
#include <glm/ext/quaternion_trigonometric.hpp>
#define IMVIEWGUIZMO_IMPLEMENTATION
#include "ImViewGuizmo/ImViewGuizmo.h"
#undef IMVIEWGUIZMO_IMPLEMENTATION
#include "goliath/camera.hpp"
#include "goliath/engine.hpp"
#include "goliath/samplers.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "models.hpp"
#include "project.hpp"
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
    std::pair<bool, VkImageMemoryBarrier2> game_window_barriers[engine::frames_in_flight]{};
    VkSampler game_window_sampler;
    ;

    std::string models_query{};
    models::gid selected_model{
        (uint8_t)-1,
        (uint32_t)-1,
    };

    struct SelectedInstance {
        uint32_t scene;
        size_t instance;
        float timer = 0.1;

        void reset_timer() {
            timer = 0.1;
        }
    };

    std::optional<SelectedInstance> transform_value_changed{};

    void init() {
        game_window_sampler = engine::samplers::get(0);

        for (size_t i = 0; i < engine::frames_in_flight; i++) {
            game_windows[i].x = -1.0f;
            game_windows[i].y = -1.0f;
        }
    }

    void destroy() {
        for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
            game_window_images[i].destroy();
            engine::GPUImageView::destroy(game_window_image_views[i]);
            ImGui_ImplVulkan_RemoveTexture(game_window_textures[i]);
        }
    }

    void tick(float dt) {
        if (transform_value_changed) {
            if ((transform_value_changed->timer -= dt) <= 0) {
                scene::save(project::scenes_file);
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

    std::optional<VkImageMemoryBarrier2> game_window(engine::Camera& cam) {
        auto curr_frame = engine::get_current_frame();
        auto& game_window_ = game_windows[curr_frame];
        auto& game_window_image = game_window_images[curr_frame];
        auto& game_window_image_view = game_window_image_views[curr_frame];
        auto& game_window_texture = game_window_textures[curr_frame];
        auto& [game_window_barrier_applied, game_window_barrier] = game_window_barriers[curr_frame];

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGuiDockNode* node = ImGui::GetWindowDockNode()) {
            node->LocalFlags |= ImGuiDockNodeFlags_NoDockingOverCentralNode | ImGuiDockNodeFlags_NoTabBar;
        }

        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 avail = ImGui::GetWindowSize();
        skip_game_window = avail.x <= 0 || avail.y <= 0;

        bool new_barrier = false;
        if ((avail.x != game_window_.x || avail.y != game_window_.y) && !skip_game_window) {
            game_window_image.destroy();
            engine::GPUImageView::destroy(game_window_image_view);
            ImGui_ImplVulkan_RemoveTexture(game_window_texture);

            auto image_upload =
                engine::GPUImage::upload(engine::GPUImageInfo{}
                                             .new_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
                                             .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                             .width(avail.x)
                                             .height(avail.y)
                                             .format(VK_FORMAT_R8G8B8A8_UNORM)
                                             .usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT));
            game_window_image = image_upload.first;
            game_window_barrier_applied = false;
            game_window_barrier = image_upload.second;
            game_window_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            game_window_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            game_window_image_view =
                engine::GPUImageView{game_window_image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).create();

            game_window_texture = ImGui_ImplVulkan_AddTexture(game_window_sampler, game_window_image_view,
                                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            game_window_ = avail;

            new_barrier = true;
        }

        if (!skip_game_window) {
            ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::Image(game_window_texture, game_window_);

            ImVec2 gizmo_offset{win_pos.x + cursor.x, win_pos.y + cursor.y};
            avail.x -= cursor.x;
            avail.y -= cursor.y;
            auto scale = ImViewGuizmo::GetStyle().scale;

            // ImViewGuizmo uses some weird coordinate scheme, so I have to change from OpenGL to up = -Y, and forward = +Z
            static const glm::mat4 GL_TO_GIZMO = glm::mat4{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};

            static const glm::mat4 GIZMO_TO_GL = glm::inverse(GL_TO_GIZMO);

            glm::mat4 view_gizmo = GL_TO_GIZMO * cam.view() * GIZMO_TO_GL;
            auto gizmo_quat = glm::quat_cast(glm::inverse(view_gizmo));
            auto gizmo_pos = glm::vec3{GL_TO_GIZMO * glm::vec4{cam.position, 1.0f}};

            bool changed = ImViewGuizmo::Rotate(gizmo_pos, gizmo_quat, glm::vec3{0.0},
                                                ImVec2{gizmo_offset.x + (avail.x - scale*90), gizmo_offset.y + scale*90});
            changed |= ImViewGuizmo::Pan(gizmo_pos, gizmo_quat,
                                         ImVec2{gizmo_offset.x + (avail.x - scale*50 - 10), gizmo_offset.y + (avail.y - scale*50 - 10)});
            changed |= ImViewGuizmo::Dolly(gizmo_pos, gizmo_quat,
                                           ImVec2{gizmo_offset.x + (avail.x - scale*50 - 10), gizmo_offset.y + (avail.y - scale*50 - 10 - scale*50 - 10)});

            if (changed) {
                cam._orientation = glm::normalize(glm::quat_cast(GIZMO_TO_GL * glm::mat4_cast(gizmo_quat) * GL_TO_GIZMO));
                cam.position = glm::vec3{GIZMO_TO_GL * glm::vec4{gizmo_pos, 1.0f}};

                cam.update_matrices();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        return new_barrier ? std::make_optional(game_window_barrier) : std::nullopt;
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
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

        blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blit_info.dstImage = game_window_image.image;
        blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        VkImageBlit2 region = blit_info.pRegions[0];

        int32_t width = std::min((int32_t)game_window_.x, region.srcOffsets[1].x);
        int32_t height = std::min((int32_t)game_window_.y, region.srcOffsets[1].y);

        region.dstOffsets[0] = VkOffset3D{
            .x = 0,
            .y = 0,
            .z = 0,
        };
        region.srcOffsets[1] = region.dstOffsets[1] = VkOffset3D{
            .x = width,
            .y = height,
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

    void models_pane() {
        ImGui::InputText("Search", &models_query);

        std::vector<std::pair<models::gid, uint32_t>> matches{};
        if (models_query.empty()) {
            auto& scene = scene::selected_scene();

            if (selected_model != models::gid{(uint8_t)-1, (uint32_t)-1}) {
                matches.emplace_back(selected_model, std::numeric_limits<int32_t>::max());
            }

            for (const auto& gid : scene.used_models) {
                if (gid == selected_model) continue;

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
        ImGui::PushID(gid.value);
        auto name = *models::get_name(gid);

        bool double_click = false;
        (ImGui::Selectable("##region", selected_model == gid,
                           ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                           ImVec2(0.0, ImGui::GetFrameHeight())));
        if (ImGui::IsItemClicked()) {
            int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
            if (clicks == 1) {
                if (selected_model == gid) {
                    selected_model = {(uint8_t)-1, (uint32_t)-1};
                } else {
                    selected_model = gid;
                }
            } else if (clicks == 2) {
                double_click = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::InputText("##name", name)) {
            printf("TODO: rename saving\n");
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            selected_model = {(uint8_t)-1, (uint32_t)-1};
        } else if (!ImGui::IsItemActive() && double_click) {
            selected_model = gid;
            scene::selected_scene().add_instance(scene::Instance{
                .model_gid = gid,
                .name = **models::get_name(gid),
                .transform = glm::identity<glm::mat4>(),
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

        bool selected = ImGui::Selectable("", current_scene.selected_instance == ix,
                ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                ImVec2(0.0, ImGui::GetFrameHeight()));

        ImGui::SameLine();
        ImGui::InputText("##name", &inst.name);
        if (ImGui::IsItemClicked() || selected) {
            current_scene.selected_instance = current_scene.selected_instance == ix ? -1 : ix;
        }

        ImGui::SameLine();
        if (ImGui::Button("X")) {
            current_scene.remove_instance(ix);

            ix--;
            ImGui::PopID();
            return ix;
        }

        transform = inst.transform;

        ImGui::PopID();
        return ix;
    }

    void transform_pane(engine::Camera& cam) {
        auto& scene = scene::selected_scene();
        if (scene.selected_instance == -1) return;
        auto& instance = scene.instances[scene.selected_instance];

        glm::vec3 translate{};
        glm::vec3 rotate{};
        glm::vec3 scale{};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(instance.transform), glm::value_ptr(translate), glm::value_ptr(rotate), glm::value_ptr(scale));

        bool value_changed = false;

        value_changed |= ImGui::InputText("name: ", &instance.name);

        value_changed |= ImGui::DragFloat3("XYZ", glm::value_ptr(translate), 0.1f, 0.0f, 0.0f, "%.2f");

        value_changed |= ImGui::DragFloat("yaw", &rotate.y, 0.1f, 0.0, 0.0, "%.2f");
        value_changed |= ImGui::DragFloat("pitch", &rotate.x, 0.1f, 0.0, 0.0, "%.2f");
        value_changed |= ImGui::DragFloat("roll", &rotate.z, 0.1f, 0.0, 0.0, "%.2f");

        value_changed |= ImGui::DragFloat3("scale", glm::value_ptr(scale), 0.1f, 0.0f, 0.0f, "%.2f");

        ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(translate), glm::value_ptr(rotate), glm::value_ptr(scale), glm::value_ptr(instance.transform));

        auto win_pos = ImGui::GetWindowPos();
        auto win_size = ImGui::GetWindowSize();

        auto win = ImGui::FindWindowByName("Viewport");
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::AllowAxisFlip(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(win->Pos.x, win->Pos.y, engine::swapchain_extent.width, engine::swapchain_extent.height);
        ImGuizmo::SetAlternativeWindow(win);

        auto proj = cam._projection;
        proj[1][1] *= -1;
        value_changed |=
            ImGuizmo::Manipulate(glm::value_ptr(cam._view), glm::value_ptr(proj),
                                 ImGuizmo::TRANSLATE | ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z,
                                 ImGuizmo::WORLD, glm::value_ptr(instance.transform));

        if (!value_changed) return;

        if (transform_value_changed && transform_value_changed->scene == scene::selected_scene_ix() &&
            transform_value_changed->instance == scene.selected_instance) {
            transform_value_changed->reset_timer();
            return;
        } else if (transform_value_changed != std::nullopt) {
            scene::save(project::scenes_file);
            return;
        }

        transform_value_changed = {
            scene::selected_scene_ix(),
            scene.selected_instance,
        };
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
