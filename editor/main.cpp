#include "exvars.hpp"
#include "game.hpp"
#include "goliath/buffer.hpp"
#include "goliath/camera.hpp"
#include "goliath/compute.hpp"
#include "goliath/culling.hpp"
#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/exvar.hpp"
#include "goliath/imgui.hpp"
#include "goliath/materials.hpp"
#include "goliath/models.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/rendering.hpp"
#include "goliath/scenes.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/transport2.hpp"
#include "goliath/util.hpp"
#include "goliath/visbuffer.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "project.hpp"
#include "scene.hpp"
#include "state.hpp"
#include "ui.hpp"
#include <GLFW/glfw3.h>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <nfd.h>

#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX

#define GLFW_EXPOSE_NATIVE_WAYLAND
#define GLFW_EXPOSE_NATIVE_EGL
#elif _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#endif

#include <nfd_glfw3.h>

#ifdef _WIN32
#include <windows.h>
#endif

struct LoadedGame {
    bool focused;
    Game game;
    float time_accum;
};

void update_depth(engine::GPUImage* images, VkImageView* image_views, uint32_t frames_in_flight) {
    for (std::size_t i = 0; i < frames_in_flight; i++) {
        images[i] = engine::gpu_image::upload(std::format("Depth texture #{}", i).c_str(),
                                              engine::GPUImageInfo{}
                                                  .format(VK_FORMAT_D16_UNORM)
                                                  .width(engine::swapchain_extent.width)
                                                  .height(engine::swapchain_extent.height)
                                                  .aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT)
                                                  .new_layout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                                  .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT),
                                              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        image_views[i] =
            engine::gpu_image_view::create(engine::GPUImageView{images[i]}.aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT));
    }
}

void update_target(engine::GPUImage* images, VkImageView* image_views, uint32_t frames_in_flight) {
    for (std::size_t i = 0; i < frames_in_flight; i++) {
        images[i] = engine::gpu_image::upload(
            std::format("Target texture #{}", i).c_str(),
            engine::GPUImageInfo{}
                .format(VK_FORMAT_R32G32B32A32_SFLOAT)
                .width(engine::swapchain_extent.width)
                .height(engine::swapchain_extent.height)
                .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                .new_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
                .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        image_views[i] =
            engine::gpu_image_view::create(engine::GPUImageView{images[i]}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
    }
}

void rebuild(engine::GPUImage* depth_images, VkImageView* depth_image_views, engine::GPUImage* target_images,
             VkImageView* target_image_views, engine::GraphicsPipeline& visbuffer_raster_pipeline,
             engine::VisBuffer& visbuffer, engine::GraphicsPipeline& grid_pipeline) {
    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
        engine::gpu_image_view::destroy(depth_image_views[i]);
        engine::gpu_image::destroy(depth_images[i]);

        engine::gpu_image_view::destroy(target_image_views[i]);
        engine::gpu_image::destroy(target_images[i]);
    }

    update_depth(depth_images, depth_image_views, engine::frames_in_flight);

    update_target(target_images, target_image_views, engine::frames_in_flight);

    visbuffer_raster_pipeline.update_viewport_to_swapchain();
    visbuffer_raster_pipeline.update_scissor_to_viewport();

    grid_pipeline.update_viewport_to_swapchain();
    grid_pipeline.update_scissor_to_viewport();

    engine::visbuffer::resize(visbuffer, {engine::swapchain_extent.width, engine::swapchain_extent.height});
}

using VisbufferRasterPC = engine::PushConstant<uint64_t, uint64_t, glm::mat4>;
using PBRPC = engine::PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t, uint64_t, uint32_t,
                                   engine::util::padding32, uint64_t>;
using GridPC = engine::PushConstant<glm::mat4, glm::mat4, glm::vec3, engine::util::padding32, glm::vec2>;
using PostprocessingPC = engine::PushConstant<glm::vec<2, uint32_t>>;

struct PBRShadingSet {
    glm::vec3 cam_pos;
    float _1;
    glm::vec3 light_pos;
    float _2;
    glm::vec3 light_intensity;
    float _3;
    glm::mat4 view_proj_matrix;
};

int main(int argc, char** argv) {
    // EXVAR_SLIDER(exvar_reg, "Editor/Camera/fov", float, fov, = 90.0f, 0.0f, 180.0f);
    EXVAR_INPUT(exvar_reg, "Editor/Camera/locked", bool, lock_cam, = true, engine::imgui_reflection::Input_ReadOnly);
    // EXVAR_DRAG(exvar_reg, "Editor/Camera/sensitivity", float, sensitivity, = 0.5f, 0.0f, 1.0f);
    // EXVAR_DRAG(exvar_reg, "Editor/Camera/movement speed", float, movement_speed, = 0.5f, 0.0f, 1.0f);

    EXVAR_DRAG(exvar_reg, "Editor/Light/intensity", glm::vec3, light_intensity, {1.0f});
    EXVAR_DRAG(exvar_reg, "Editor/Light/position", glm::vec3, light_position, {5.0f});

    // glm::vec3 look_at{0.0f};
    // engine::Camera cam{};
    // cam.set_projection(engine::camera::Perspective{
    //     // .fov = glm::radians(fov),
    //     .fov = glm::radians(90.0f),
    //     .aspect_ratio = 16.0f / 10.0f,
    // });
    // cam.position = glm::vec3{10.0f};
    // cam.look_at(look_at);
    // cam.update_matrices();

    // exvar_reg.add_drag_reference("Editor/Camera/position", &cam.position);

    if (argc >= 2 && std::strcmp(argv[1], "init") == 0) {
        project::init();
        return 0;
    }

    if (!project::find_project()) {
        printf("Project root couldn't be found. To initialize a project use `%s init`\n", argv[0]);
        return 0;
    }
    std::filesystem::current_path(project::project_root);

    engine::init(engine::Init{
        .window_name = "Goliath editor",
        .texture_capacity = 1000,
        .fullscreen = false,
        .textures_directory = project::textures_directory,
        .models_directory = project::models_directory,
    });
    glfwSetWindowAttrib(engine::window, GLFW_DECORATED, GLFW_TRUE);
    glfwSetWindowAttrib(engine::window, GLFW_RESIZABLE, GLFW_TRUE);
    glfwSetWindowAttrib(engine::window, GLFW_AUTO_ICONIFY, GLFW_TRUE);
    GameView::init();

    std::optional<LoadedGame> game{};
    GameView game_viewport{};

    auto state_json = engine::util::read_json(project::editor_state);
    if (!state_json.has_value()) {
        state_json = nlohmann::json{
            {"state", state::default_json()},
            {"exvars", nlohmann::json::array()},
            {"scenes", scene::default_json()},
        };
    }

    state::load((*state_json)["state"]);
    exvar_reg.override((*state_json)["exvars"]);

    auto tex_reg_json = engine::util::read_json(project::textures_registry);
    if (!tex_reg_json.has_value() && tex_reg_json.error() == engine::util::ReadJsonErr::FileErr &&
        !std::filesystem::exists(project::textures_registry)) {
        tex_reg_json = nlohmann::json{
            {"textures", nlohmann::json::array()},
            {"samplers", nlohmann::json::array()},
        };
    } else if (!tex_reg_json.has_value()) {
        printf("Texture registry file is corrupted\n");
        return 0;
    }

    engine::samplers::load((*tex_reg_json)["samplers"]);
    engine::textures::load((*tex_reg_json)["textures"]);

    auto mats_json = engine::util::read_json(project::materials);
    if (!mats_json.has_value() && mats_json.error() == engine::util::ReadJsonErr::FileErr &&
        !std::filesystem::exists(project::materials)) {
        mats_json = engine::materials::default_json();
    } else if (!mats_json.has_value()) {
        printf("materials.json file is corrupted\n");
        return 0;
    }

    engine::materials::load(*mats_json);

    auto models_registry_json = engine::util::read_json(project::models_registry);
    if (!models_registry_json.has_value() && models_registry_json.error() == engine::util::ReadJsonErr::FileErr &&
        !std::filesystem::exists(project::models_registry)) {
        models_registry_json = nlohmann::json::array();
    } else if (!models_registry_json.has_value()) {
        printf("Models registry file is corrupted\n");
        return 0;
    }
    engine::models::load(*models_registry_json);

    scene::load((*state_json)["scenes"]);

    NFD_Init();
    engine::culling::init(8192);

    if (argc >= 2) {
        auto game_ = Game::load(argv[1]);
        if (!game_) {
            printf("Game load error: %d\n", game_.error());
            return -1;
        }

        game = LoadedGame{
            .focused = false,
            .game = *game_,
            .time_accum = 0,
        };

        game->game.init(argc - 2, argv + 2);
    }

    auto visbuffer = engine::visbuffer::create({engine::swapchain_extent.width, engine::swapchain_extent.height});

    uint32_t visbuffer_raster_vertex_spv_size;
    auto visbuffer_raster_vertex_spv_data =
        engine::util::read_file("visbuffer_raster_vertex.spv", &visbuffer_raster_vertex_spv_size);
    auto visbuffer_raster_vertex_module =
        engine::shader::create({visbuffer_raster_vertex_spv_data, visbuffer_raster_vertex_spv_size});
    free(visbuffer_raster_vertex_spv_data);

    uint32_t visbuffer_raster_fragment_spv_size;
    auto visbuffer_raster_fragment_spv_data =
        engine::util::read_file("visbuffer_raster_fragment.spv", &visbuffer_raster_fragment_spv_size);
    auto visbuffer_raster_fragment_module =
        engine::shader::create({visbuffer_raster_fragment_spv_data, visbuffer_raster_fragment_spv_size});
    free(visbuffer_raster_fragment_spv_data);

    auto visbuffer_raster_pipeline =
        engine::rendering::create_pipeline(engine::GraphicsPipelineBuilder{}
                                               .vertex(visbuffer_raster_vertex_module)
                                               .fragment(visbuffer_raster_fragment_module)
                                               .push_constant_size(VisbufferRasterPC::size)
                                               .add_color_attachment(engine::visbuffer::format)
                                               .depth_format(VK_FORMAT_D16_UNORM))
            .depth_test(true)
            .depth_write(true)
            .depth_compare_op(engine::CompareOp::Less)
            .cull_mode(engine::CullMode::Back);

    uint32_t pbr_spv_size;
    auto pbr_spv_data = engine::util::read_file("pbr.spv", &pbr_spv_size);
    auto pbr_module = engine::shader::create({pbr_spv_data, pbr_spv_size});
    free(pbr_spv_data);

    auto pbr_shading_set_layout = engine::descriptor::create_layout(engine::DescriptorSet<engine::descriptor::Binding{
                                                                        .count = 1,
                                                                        .type = engine::descriptor::Binding::UBO,
                                                                        .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                                    }>{});
    auto pbr_pipeline =
        engine::compute::create(engine::ComputePipelineBuilder{}
                                    .shader(pbr_module)
                                    .descriptor_layout(0, engine::visbuffer::shading_layout)
                                    .descriptor_layout(1, pbr_shading_set_layout)
                                    .descriptor_layout(2, engine::textures::get_texture_pool().set_layout)
                                    .push_constant(PBRPC::size));

    uint32_t fullscreen_triangle_spv_size;
    auto fullscreen_triangle_spv_data =
        engine::util::read_file("fullscreen_triangle.spv", &fullscreen_triangle_spv_size);
    auto fullscreen_triangle_module =
        engine::shader::create({fullscreen_triangle_spv_data, fullscreen_triangle_spv_size});
    free(fullscreen_triangle_spv_data);

    uint32_t grid_spv_size;
    auto grid_spv_data = engine::util::read_file("grid.spv", &grid_spv_size);
    auto grid_module = engine::shader::create({grid_spv_data, grid_spv_size});
    free(grid_spv_data);

    auto grid_pipeline = engine::rendering::create_pipeline(
                             engine::GraphicsPipelineBuilder{}
                                 .vertex(fullscreen_triangle_module)
                                 .fragment(grid_module)
                                 .push_constant_size(GridPC::size)
                                 .add_color_attachment(VK_FORMAT_R32G32B32A32_SFLOAT,
                                                       engine::BlendState{}
                                                           .blend(true)
                                                           .src_color_blend_factor(VK_BLEND_FACTOR_ONE)
                                                           .src_alpha_blend_factor(VK_BLEND_FACTOR_ONE)
                                                           .dst_color_blend_factor(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
                                                           .dst_alpha_blend_factor(VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
                                                           .color_blend_op(VK_BLEND_OP_ADD)
                                                           .alpha_blend_op(VK_BLEND_OP_ADD))
                                 .depth_format(VK_FORMAT_D16_UNORM))
                             .depth_test(true)
                             .depth_write(false)
                             .depth_compare_op(engine::CompareOp::Less)
                             .cull_mode(engine::CullMode::NoCull);

    uint32_t postprocessing_spv_size;
    auto postprocessing_spv_data = engine::util::read_file("postprocessing.spv", &postprocessing_spv_size);
    auto postprocessing_module = engine::shader::create({postprocessing_spv_data, postprocessing_spv_size});
    free(postprocessing_spv_data);

    auto postprocessing_set_layout =
        engine::descriptor::create_layout(engine::DescriptorSet<engine::descriptor::Binding{
                                                                    .count = 1,
                                                                    .type = engine::descriptor::Binding::StorageImage,
                                                                    .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                                },
                                                                engine::descriptor::Binding{
                                                                    .count = 1,
                                                                    .type = engine::descriptor::Binding::StorageImage,
                                                                    .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                                }>{});
    auto postprocessing_pipeline = engine::compute::create(engine::ComputePipelineBuilder{}
                                                               .shader(postprocessing_module)
                                                               .descriptor_layout(0, postprocessing_set_layout)
                                                               .push_constant(PostprocessingPC::size));

    engine::GPUImage* depth_images = (engine::GPUImage*)malloc(sizeof(engine::GPUImage) * engine::frames_in_flight);
    VkImageView* depth_image_views = (VkImageView*)malloc(sizeof(VkImageView) * engine::frames_in_flight);
    update_depth(depth_images, depth_image_views, engine::frames_in_flight);

    engine::GPUImage* target_images = (engine::GPUImage*)malloc(sizeof(engine::GPUImage) * engine::frames_in_flight);
    VkImageView* target_image_views = (VkImageView*)malloc(sizeof(VkImageView) * engine::frames_in_flight);
    update_target(target_images, target_image_views, engine::frames_in_flight);

    constexpr uint32_t max_draw_size = 4096;
    engine::Buffer draw_id_buffers[engine::frames_in_flight];
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        draw_id_buffers[i] =
            engine::Buffer::create(std::format("draw id buffer #{}", i).c_str(),
                                   sizeof(glm::vec4) + max_draw_size * (sizeof(glm::mat4) + sizeof(glm::vec4)),
                                   VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT |
                                       VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                                   std::nullopt);
    }

    engine::Buffer indirect_draw_buffers[engine::frames_in_flight];
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        indirect_draw_buffers[i] = engine::Buffer::create(
            std::format("indirect draw buffer #{}", i).c_str(),
            sizeof(engine::culling::CulledDrawCommand) * max_draw_size,
            VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);
    }

    glfwSetInputMode(engine::window, GLFW_CURSOR, lock_cam ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    engine::imgui::enable(lock_cam);

    ui::init();

    double accum = 0;
    double last_time = glfwGetTime();
    static constexpr double dt = (1000.0 / 60.0) / 1000.0;

    bool done = false;
    while (!glfwWindowShouldClose(engine::window) && !done) {
        double time = glfwGetTime();
        double frame_time = time - last_time;
        last_time = time;
        accum += frame_time;
        if (game) {
            game->time_accum += frame_time;
        }

        auto state = engine::event::poll();
        if (state == engine::event::Minimized) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }

        auto cam_info = scene::camera();

        ImGuiWindow* scene_window = nullptr;
        {
            engine::imgui::begin();
            ui::begin();
            ImGui::DockSpaceOverViewport();

            if (ImGui::Begin("Editor Inspector", nullptr, ImGuiWindowFlags_HorizontalScrollbar)) {
                exvar_reg.imgui_ui();
            }
            ImGui::End();

            if ((ui::game_window() || !lock_cam) && ImGui::IsKeyDown(ImGuiKey_LeftShift) &&
                ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                lock_cam = !lock_cam;
                exvar_reg.modified();

                glfwSetInputMode(engine::window, GLFW_CURSOR, lock_cam ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                engine::imgui::enable(lock_cam);
            }

            if (game) {
                game->game.draw_game_imgui();

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
                ImGui::Begin("Game viewport", nullptr,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                if (ImGuiDockNode* node = ImGui::GetWindowDockNode()) {
                    node->LocalFlags |= ImGuiDockNodeFlags_NoDockingOverCentralNode;
                }

                game->focused = game_viewport.draw_pane();

                ImGui::End();
                ImGui::PopStyleVar();
            }

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Add model")) {
                        nfdu8filteritem_t filters[1] = {
                            {"Model files", "gltf,glb,gom"},
                        };
                        auto current_path = std::filesystem::current_path();
                        nfdopendialogu8args_t args{};
                        args.filterCount = 1;
                        args.filterList = filters;
                        args.defaultPath = (const char*)current_path.c_str();
                        NFD_GetNativeWindowFromGLFWWindow(engine::window, &args.parentWindow);

                        const nfdpathset_t* paths;
                        auto res = NFD_OpenDialogMultipleU8_With(&paths, &args);
                        if (res == NFD_OKAY) {
                            nfdpathsetenum_t enumerator;
                            NFD_PathSet_GetEnum(paths, &enumerator);

                            nfdchar_t* path;
                            std::size_t i = 0;

                            while (NFD_PathSet_EnumNext(&enumerator, &path) && path) {
                                engine::models::add(path, std::filesystem::path{path}.stem().string());
                                NFD_PathSet_FreePath(path);
                            }

                            NFD_PathSet_FreeEnum(&enumerator);
                            NFD_PathSet_Free(paths);
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMainMenuBar();

            if (ImGui::Begin("Instances")) {
                ui::instances_pane();
            }
            ImGui::End();

            if (ImGui::Begin("Assets")) {
                ui::assets_pane();
            }
            ImGui::End();

            if (ImGui::Begin("Transformation")) {
                ui::transform_pane();
            }
            ImGui::End();

            if (ImGui::Begin("Materials")) {
                ui::selected_model_materials_pane();
            }
            ImGui::End();

            ui::rename_popup();
            ui::scenes_settings_pane();

            engine::imgui::end();
        }

        while (accum >= dt) {
            accum -= dt;

            ui::tick(dt);

            // cam.set_projection(engine::camera::Perspective{
            //     .fov = glm::radians(fov),
            //     .aspect_ratio = 16.0f / 10.0f,
            // });

            glm::vec3 movement{0.0f};
            if (!lock_cam) {
                if (engine::event::is_held(ImGuiKey_W)) {
                    movement.z -= 1.0f;
                }
                if (engine::event::is_held(ImGuiKey_S)) {
                    movement.z += 1.0f;
                }
                if (engine::event::is_held(ImGuiKey_A)) {
                    movement.x -= 1.0f;
                }
                if (engine::event::is_held(ImGuiKey_D)) {
                    movement.x += 1.0f;
                }
            }
            bool moved = movement.x != 0.0f || movement.z != 0.0f;

            if (moved) {
                auto forward = cam_info.cam.forward();
                auto right = cam_info.cam.right();
                auto normalized_movement = glm::normalize(movement);

                cam_info.cam.position += cam_info.movement_speed * (-normalized_movement.z * forward + normalized_movement.x * right);
                exvar_reg.modified();
            }

            auto mouse_delta = engine::event::get_mouse_delta();
            if (!lock_cam && (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)) {
                cam_info.cam.rotate(cam_info.sensitivity * glm::radians(-mouse_delta.x), cam_info.sensitivity * glm::radians(-mouse_delta.y));
            }

            cam_info.cam.update_matrices();
            scene::update_camera(cam_info);

            if (state::want_to_save() || exvar_reg.want_to_save() || scene::want_to_save()) {
                std::ofstream o{project::editor_state};
                o << nlohmann::json{
                    {"state", state::save()},
                    {"exvars", exvar_reg.save()},
                    {"scenes", scene::save()},
                };
            }

            if (engine::models_to_save()) {
                std::ofstream o{project::models_registry};
                o << engine::models::save();
            }

            if (engine::materials_to_save()) {
                std::ofstream o{project::materials};
                o << engine::materials::save();
            }

            if (engine::textures_to_save()) {
                std::ofstream o{project::textures_registry};
                o << nlohmann::json{
                    {"textures", engine::textures::save()},
                    {"samplers", engine::samplers::save()},
                };
            }

            if (engine::scenes::want_to_save()) {
                std::ofstream o{project::scenes_file};
                o << engine::scenes::save();
            }

            engine::event::update_tick();
        }

        if (game) {
            const auto dt = (1000.0 / game->game.config.tps) / 1000.0;
            while (game->time_accum >= dt) {
                game->time_accum -= dt;
                game->game.tick(game->focused);
            }
        }

        if (engine::prepare_frame()) {
            rebuild(depth_images, depth_image_views, target_images, target_image_views, visbuffer_raster_pipeline,
                    visbuffer, grid_pipeline);

            if (game) game->game.resize();
        }

        engine::transport2::ticket transform_buffer_ticket{};
        {
            engine::prepare_draw();

            if (game && !game_viewport.skipped_window) {
                game->game.render(game_viewport.dimensions[engine::get_current_frame()]);
            }

            VkClearColorValue target_clear_color{};
            target_clear_color.float32[0] = 36.0f / 255.0f;
            target_clear_color.float32[1] = 36.0f / 255.0f;
            target_clear_color.float32[2] = 36.0f / 255.0f;
            target_clear_color.float32[3] = 1.0f;

            VkImageSubresourceRange clear_range{};
            clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clear_range.baseMipLevel = 0;
            clear_range.layerCount = 1;
            clear_range.baseArrayLayer = 0;
            clear_range.levelCount = 1;

            vkCmdClearColorImage(engine::get_cmd_buf(), target_images[engine::get_current_frame()].image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &target_clear_color, 1, &clear_range);

            engine::visbuffer::clear_buffers(visbuffer, engine::get_current_frame());

            engine::culling::bind_flatten();
            auto instance_transforms =
                engine::scenes::get_instance_transforms_buffer(scene::selected_scene(), transform_buffer_ticket);
            auto instance_models = engine::scenes::get_instance_models(scene::selected_scene());
            for (auto i = 0; i < instance_models.size(); i++) {
                auto mgid = instance_models[i];
                if (auto state = engine::models::is_loaded(mgid); !state || *state != engine::models::LoadState::OnGPU)
                    continue;

                auto res = engine::culling::flatten(mgid, instance_transforms.address(), i * sizeof(glm::mat4));
            }

            auto& draw_id_buffer = draw_id_buffers[engine::get_current_frame()];
            auto& indirect_draw_buffer = indirect_draw_buffers[engine::get_current_frame()];

            engine::culling::cull(max_draw_size, draw_id_buffer.address(), indirect_draw_buffer.address());

            engine::synchronization::begin_barriers();
            engine::culling::sync_for_draw(draw_id_buffer, indirect_draw_buffer);
            engine::synchronization::end_barriers();

            engine::visbuffer::prepare_for_draw(visbuffer, engine::get_current_frame());

            engine::rendering::begin(
                engine::RenderPass{}
                    .add_color_attachment(visbuffer.attach(engine::get_current_frame()))
                    .depth_attachment(engine::RenderingAttachement{}
                                          .set_image(depth_image_views[engine::get_current_frame()],
                                                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                          .set_clear_depth(1.0f)
                                          .set_load_op(engine::LoadOp::Clear)
                                          .set_store_op(engine::StoreOp::Store)));

            uint8_t visbuffer_raster_pc[VisbufferRasterPC::size];
            VisbufferRasterPC::write(visbuffer_raster_pc, draw_id_buffer.address(), indirect_draw_buffer.address(),
                                     cam_info.cam.view_projection());

            visbuffer_raster_pipeline.bind();
            visbuffer_raster_pipeline.draw_indirect_count(engine::GraphicsPipeline::DrawIndirectCountParams{
                .push_constant = visbuffer_raster_pc,
                .draw_buffer = indirect_draw_buffer.data(),
                .count_buffer = draw_id_buffer.data(),
                .max_draw_count = max_draw_size,
                .stride = sizeof(engine::culling::CulledDrawCommand),
            });
            engine::rendering::end();

            engine::visbuffer::count_materials(visbuffer, draw_id_buffer.address(), engine::get_current_frame());
            engine::visbuffer::get_offsets(visbuffer, engine::get_current_frame());
            engine::visbuffer::write_fragment_ids(visbuffer, draw_id_buffer.address(), engine::get_current_frame());

            VkImageMemoryBarrier2 target_barrier{};
            target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            target_barrier.pNext = nullptr;
            target_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            target_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            target_barrier.image = target_images[engine::get_current_frame()].image;
            target_barrier.subresourceRange = VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            target_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();

            auto shading = engine::visbuffer::shade(visbuffer, target_image_views[engine::get_current_frame()],
                                                    engine::get_current_frame());
            auto mats_buffer = engine::materials::get_buffer().address();
            if (mats_buffer != 0) {
                for (uint16_t mat_id = 0; mat_id < shading.material_id_count; mat_id++) {
                    uint8_t pbr_pc[PBRPC::size]{};
                    PBRPC::write(pbr_pc,
                                 glm::vec<2, uint32_t>{engine::swapchain_extent.width, engine::swapchain_extent.height},
                                 visbuffer.stages.address() + shading.indirect_buffer_offset,
                                 visbuffer.stages.address() + shading.fragment_id_buffer_offset,
                                 draw_id_buffer.address(), mat_id, mats_buffer);

                    PBRShadingSet shading_set_data{
                        .cam_pos = cam_info.cam.position,
                        .light_pos = light_position,
                        .light_intensity = light_intensity,
                        .view_proj_matrix = cam_info.cam.view_projection(),
                    };

                    auto pbr_shading_set = engine::descriptor::new_set(pbr_shading_set_layout);
                    engine::descriptor::begin_update(pbr_shading_set);
                    engine::descriptor::update_ubo(0, {(uint8_t*)&shading_set_data, sizeof(PBRShadingSet)});
                    engine::descriptor::end_update();

                    pbr_pipeline.bind();
                    pbr_pipeline.dispatch_indirect(engine::ComputePipeline::IndirectDispatchParams{
                        .push_constant = pbr_pc,
                        .descriptor_indexes =
                            {
                                shading.vis_and_target_set,
                                pbr_shading_set,
                                engine::textures::get_texture_pool().set,
                                engine::descriptor::null_set,
                            },
                        .indirect_buffer = visbuffer.stages,
                        .buffer_offset = shading.indirect_buffer_offset,
                    });
                }
            }

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();

            engine::rendering::begin(
                engine::RenderPass{}
                    .add_color_attachment(engine::RenderingAttachement{}
                                              .set_image(target_image_views[engine::get_current_frame()],
                                                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                                              .set_load_op(engine::LoadOp::Load)
                                              .set_store_op(engine::StoreOp::Store))
                    .depth_attachment(engine::RenderingAttachement{}
                                          .set_image(depth_image_views[engine::get_current_frame()],
                                                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                          .set_load_op(engine::LoadOp::Load)
                                          .set_store_op(engine::StoreOp::NoStore)));
            uint8_t grid_pc[GridPC::size]{};
            auto vp = cam_info.cam.view_projection();
            GridPC::write(grid_pc, glm::inverse(vp), vp, cam_info.cam.position,
                          glm::vec2{engine::swapchain_extent.width, engine::swapchain_extent.height});

            grid_pipeline.bind();
            grid_pipeline.draw(engine::GraphicsPipeline::DrawParams{
                .push_constant = grid_pc,
                .vertex_count = 3,
            });
            engine::rendering::end();

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();

            auto pp_set = engine::descriptor::new_set(postprocessing_set_layout);
            engine::descriptor::begin_update(pp_set);
            engine::descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL,
                                                     target_image_views[engine::get_current_frame()]);
            engine::descriptor::update_storage_image(1, VK_IMAGE_LAYOUT_GENERAL,
                                                     visbuffer.image_views[engine::get_current_frame()]);
            engine::descriptor::end_update();

            uint8_t postprocessing_push_constant[PostprocessingPC::size]{};
            PostprocessingPC::write(
                postprocessing_push_constant,
                glm::vec<2, uint32_t>{engine::swapchain_extent.width, engine::swapchain_extent.height});

            postprocessing_pipeline.bind();
            postprocessing_pipeline.dispatch(engine::ComputePipeline::DispatchParams{
                .push_constant = postprocessing_push_constant,
                .descriptor_indexes =
                    {
                        pp_set,
                        engine::descriptor::null_set,
                        engine::descriptor::null_set,
                        engine::descriptor::null_set,
                    },
                .group_count_x = (uint32_t)std::ceil(engine::swapchain_extent.width / 16.0f),
                .group_count_y = (uint32_t)std::ceil(engine::swapchain_extent.height / 16.0f),
                .group_count_z = 1,
            });

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();

            VkImageBlit2 blit_region{};
            blit_region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blit_region.srcOffsets[0] = VkOffset3D{
                .x = 0,
                .y = 0,
                .z = 0,
            };
            blit_region.srcOffsets[1] = VkOffset3D{
                .x = (int32_t)engine::swapchain_extent.width,
                .y = (int32_t)engine::swapchain_extent.height,
                .z = 1,
            };
            blit_region.srcSubresource = VkImageSubresourceLayers{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            VkBlitImageInfo2 blit_info{};
            blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blit_info.srcImage = target_images[engine::get_current_frame()].image;
            blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blit_info.filter = VK_FILTER_NEAREST;
            blit_info.regionCount = 1;
            blit_info.pRegions = &blit_region;
            auto game_window_barrier = ui::blit_game_window(blit_info);

            if (game && !game_viewport.skipped_window) game_viewport.blit(game->game);

            engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
                engine::RenderingAttachement{}
                    .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                    .set_load_op(engine::LoadOp::Load)
                    .set_store_op(engine::StoreOp::Store)));
            engine::imgui::render();
            engine::rendering::end();

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            engine::culling::clear_buffers(draw_id_buffer, indirect_draw_buffer);

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            if (game_window_barrier) engine::synchronization::apply_barrier(*game_window_barrier);
            engine::synchronization::end_barriers();
        }

        std::array<VkSemaphoreSubmitInfo, 2> waits{};
        waits[1] = engine::transport2::wait_on({&transform_buffer_ticket, 1});
        if (engine::next_frame(waits)) {
            rebuild(depth_images, depth_image_views, target_images, target_image_views, visbuffer_raster_pipeline,
                    visbuffer, grid_pipeline);

            if (game) game->game.resize();

            engine::increment_frame();
        }
    }

    vkDeviceWaitIdle(engine::device);

    NFD_Quit();

    engine::visbuffer::destroy(visbuffer);

    engine::rendering::destroy_pipeline(visbuffer_raster_pipeline);
    engine::shader::destroy(visbuffer_raster_fragment_module);
    engine::shader::destroy(visbuffer_raster_vertex_module);

    engine::descriptor::destroy_layout(pbr_shading_set_layout);
    engine::compute::destroy(pbr_pipeline);
    engine::shader::destroy(pbr_module);

    engine::shader::destroy(fullscreen_triangle_module);
    engine::shader::destroy(grid_module);
    engine::rendering::destroy_pipeline(grid_pipeline);

    engine::descriptor::destroy_layout(postprocessing_set_layout);
    engine::compute::destroy(postprocessing_pipeline);
    engine::shader::destroy(postprocessing_module);

    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
        engine::gpu_image_view::destroy(depth_image_views[i]);
        engine::gpu_image::destroy(depth_images[i]);

        engine::gpu_image_view::destroy(target_image_views[i]);
        engine::gpu_image::destroy(target_images[i]);

        draw_id_buffers[i].destroy();
        indirect_draw_buffers[i].destroy();
    }

    free(target_images);
    free(target_image_views);

    free(depth_image_views);
    free(depth_images);

    game_viewport.destroy();
    if (game) {
        game->game.unload();
    }

    ui::destroy();

    scene::destroy();

    engine::culling::destroy();
    engine::destroy();

    exvar_reg.destroy();

    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
