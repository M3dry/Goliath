#include "ui.hpp"

#include "ImGuizmo/ImGuizmo.h"
#include "goliath/materials.hpp"
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
    glm::vec2 game_image_offset{0.0f};
    glm::vec2 game_image_dims{0.0f};

    struct SelectedInstance {
        uint32_t scene;
        size_t instance;
        float timer = 0.1;

        void reset_timer() {
            timer = 0.1;
        }
    };

    std::optional<SelectedInstance> transform_value_changed{};

    void update_instance_transform(scene::Scene& scene) {
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
                engine::GPUImage::upload(std::format("Game window texture #{}", curr_frame).c_str(),
                                         engine::GPUImageInfo{}
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

            // ImViewGuizmo uses some weird coordinate scheme, so I have to change from OpenGL to up = -Y, and forward =
            // +Z
            static const glm::mat4 GL_TO_GIZMO = glm::mat4{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};

            static const glm::mat4 GIZMO_TO_GL = glm::inverse(GL_TO_GIZMO);

            glm::mat4 view_gizmo = GL_TO_GIZMO * cam.view() * GIZMO_TO_GL;
            auto gizmo_quat = glm::quat_cast(glm::inverse(view_gizmo));
            auto gizmo_pos = glm::vec3{GL_TO_GIZMO * glm::vec4{cam.position, 1.0f}};

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
                cam._orientation =
                    glm::normalize(glm::quat_cast(GIZMO_TO_GL * glm::mat4_cast(gizmo_quat) * GL_TO_GIZMO));
                cam.position = glm::vec3{GIZMO_TO_GL * glm::vec4{gizmo_pos, 1.0f}};

                cam.update_matrices();
            }

            auto& scene = scene::selected_scene();
            if (scene.selected_instance != -1) {
                auto win_pos = ImGui::GetWindowPos();

                changed = false;
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::AllowAxisFlip(false);
                ImGuizmo::SetRect(win_pos.x + cursor.x + game_image_offset.x,
                                  win_pos.y + cursor.y + game_image_offset.y, game_image_dims.x, game_image_dims.y);
                ImGuizmo::SetDrawlist();

                auto proj = cam._projection;
                proj[1][1] *= -1;
                changed |= ImGuizmo::Manipulate(
                    glm::value_ptr(cam._view), glm::value_ptr(proj),
                    ImGuizmo::TRANSLATE | ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z, ImGuizmo::WORLD,
                    glm::value_ptr(scene.instances[scene.selected_instance].transform));

                if (changed) update_instance_transform(scene);
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

    void models_pane() {
        if (ImGui::InputText("##search", &state::models_query)) {
            state::modified_value();
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);
        std::array<const char*, 2> combo_values = {
            "Scene models",
            "All models",
        };
        if (ImGui::BeginCombo("##combo", combo_values[state::models_search_scope], ImGuiComboFlags_NoPreview)) {
            for (size_t i = 0; i < combo_values.size(); i++) {
                if (ImGui::Selectable(combo_values[i], state::models_search_scope == i)) {
                    state::models_search_scope = i;
                    state::modified_value();
                }

                if (state::models_search_scope == i) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        std::vector<std::pair<engine::models::gid, uint32_t>> matches{};
        if (state::models_search_scope == 0) {
            auto& scene = scene::selected_scene();

            for (const auto& gid : scene.used_models) {
                auto score = score_search(state::models_query, **engine::models::get_name(gid));
                if (score > std::numeric_limits<int32_t>::min()) {
                    matches.emplace_back(gid, score);
                }
            }
        } else {
            const auto& names = engine::models::get_names();
            for (uint32_t i = 0; i < names.size(); i++) {
                auto score = score_search(state::models_query, names[i]);
                if (score > std::numeric_limits<int32_t>::min()) {
                    matches.emplace_back(engine::models::gid{engine::models::get_generation(i), i}, score);
                }
            }
        }

        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        for (auto [gid, _] : matches) {
            model_entry(gid);
        }
    }

    void model_entry(engine::models::gid gid) {
        ImGui::PushID(gid.value);
        auto name = *engine::models::get_name(gid);

        if (ImGui::InputText("##name", name)) {
            printf("TODO: rename saving\n");
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("+")) {
            scene::selected_scene().add_instance(scene::Instance{
                .model_gid = gid,
                .name = **engine::models::get_name(gid),
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

        if (ImGui::Selectable("", current_scene.selected_instance == ix,
                              ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0.0, ImGui::GetFrameHeight()))) {
            current_scene.selected_instance = current_scene.selected_instance == ix ? -1 : ix;
        }

        ImGui::SameLine();
        ImGui::InputText("##name", &inst.name);
        if (ImGui::IsItemClicked()) {
            current_scene.selected_instance = ix;
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);
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
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(instance.transform), glm::value_ptr(translate),
                                              glm::value_ptr(rotate), glm::value_ptr(scale));

        bool value_changed = false;

        value_changed |= ImGui::InputText("name: ", &instance.name);

        value_changed |= ImGui::DragFloat3("XYZ", glm::value_ptr(translate), 0.1f, 0.0f, 0.0f, "%.2f");

        value_changed |= ImGui::DragFloat("yaw", &rotate.y, 0.1f, 0.0, 0.0, "%.2f");
        value_changed |= ImGui::DragFloat("pitch", &rotate.x, 0.1f, 0.0, 0.0, "%.2f");
        value_changed |= ImGui::DragFloat("roll", &rotate.z, 0.1f, 0.0, 0.0, "%.2f");

        value_changed |= ImGui::DragFloat3("scale", glm::value_ptr(scale), 0.1f, 0.0f, 0.0f, "%.2f");

        ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(translate), glm::value_ptr(rotate),
                                                glm::value_ptr(scale), glm::value_ptr(instance.transform));

        if (!value_changed) return;

        update_instance_transform(scene);
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
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);
            ImGui::InputText("##name", &scenes[i].name);

            ImGui::PopID();
        }
    }

    void selected_model_materials_pane() {
        const auto& curr_scene = scene::selected_scene();
        auto instance_ix = curr_scene.selected_instance;
        if (instance_ix == -1) return;

        const auto& inst = curr_scene.instances[instance_ix];
        const auto model_ = engine::models::get_cpu_model(inst.model_gid);
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

        constexpr auto im = engine::imgui_reflection::Input{};
        for (size_t i = 0; i < schema.attributes.size(); i++) {
            auto& name = schema.names[i];

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
                        // TODO: texture picker
                    } else {
                        engine::imgui_reflection::input(name.c_str(), im, data_ptr);
                    }

                    offset += sizeof(Attr);
                },
                schema.attributes[i]);
        }

        return modified;
    }
}
