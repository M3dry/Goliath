#include "models.hpp"
#include "project.hpp"
#include "goliath/buffer.hpp"
#include "goliath/camera.hpp"
#include "goliath/compute.hpp"
#include "goliath/culling.hpp"
#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/gpu_group.hpp"
#include "goliath/imgui.hpp"
#include "goliath/model.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/rendering.hpp"
#include "goliath/scene.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/texture_registry.hpp"
#include "goliath/transport.hpp"
#include "goliath/util.hpp"
#include "goliath/visbuffer.hpp"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "scene.hpp"
#include <GLFW/glfw3.h>
#include <cstring>
#include <filesystem>
#include <format>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unistd.h>
#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <nfd.h>

#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#define GLFW_EXPOSE_NATIVE_EGL

#define GLFW_EXPOSE_NATIVE_WAYLAND
#define GLFW_EXPOSE_NATIVE_EGL
#elif _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#endif

#include <nfd_glfw3.h>

void update_depth(engine::GPUImage* images, VkImageView* image_views, VkImageMemoryBarrier2* barriers,
                  uint32_t frames_in_flight) {
    for (std::size_t i = 0; i < frames_in_flight; i++) {
        auto [depth_img, barrier] = engine::GPUImage::upload(engine::GPUImageInfo{}
                                                                 .format(VK_FORMAT_D16_UNORM)
                                                                 .width(engine::swapchain_extent.width)
                                                                 .height(engine::swapchain_extent.height)
                                                                 .aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT)
                                                                 .new_layout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                                                 .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

        barrier.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        images[i] = depth_img;
        barriers[i] = barrier;
        image_views[i] = engine::GPUImageView{images[i]}.aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT).create();
    }
}

void update_target(engine::GPUImage* images, VkImageView* image_views, VkImageMemoryBarrier2* barriers,
                   uint32_t frames_in_flight) {
    for (std::size_t i = 0; i < frames_in_flight; i++) {
        auto [target_img, barrier] =
            engine::GPUImage::upload(engine::GPUImageInfo{}
                                         .format(VK_FORMAT_R32G32B32A32_SFLOAT)
                                         .width(engine::swapchain_extent.width)
                                         .height(engine::swapchain_extent.height)
                                         .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                         .new_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
                                         .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));

        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        images[i] = target_img;
        barriers[i] = barrier;
        image_views[i] = engine::GPUImageView{images[i]}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).create();
    }
}

using VisbufferRasterPC = engine::PushConstant<uint64_t, uint64_t, glm::mat4>;
using PBRPC = engine::PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t, uint64_t, uint32_t>;
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

struct Model {
    enum DataType {
        GLTF,
        GLB,
        GOM,
    };

    uint64_t last_instance_id = 0;

    std::string name;
    std::filesystem::path filepath;
    bool embed_optimized = false;

    uint64_t timeline;
    engine::Model cpu_data;
    engine::GPUModel gpu_data;
    engine::Buffer indirect_draw_buffer;
    engine::GPUGroup gpu_group;

    std::vector<size_t> instances{};

    engine::Model::Err load(const std::string& cwd, DataType type, uint8_t* data, uint32_t size,
                            VkBufferMemoryBarrier2* barrier) {
        engine::Model::Err err;
        if (type == GOM) {
            engine::Model::load_optimized(&cpu_data, data);
            err = engine::Model::Ok;
        } else if (type == GLTF) {
            err = engine::Model::load_gltf(&cpu_data, {data, size}, cwd);
        } else if (type == GLB) {
            err = engine::Model::load_glb(&cpu_data, {data, size}, cwd);
        } else {
            err = engine::Model::InvalidFormat;
        }

        if (err != engine::Model::Ok) {
            return err;
        }

        engine::gpu_group::begin();
        auto [_gpu_data, _indirect_draw_buffer] = engine::model::upload(&cpu_data);
        gpu_data = _gpu_data;
        indirect_draw_buffer = _indirect_draw_buffer;
        gpu_group = engine::gpu_group::end(barrier, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);

        return engine::Model::Ok;
    }

    void destroy() {
        cpu_data.destroy();
        gpu_group.destroy();
        indirect_draw_buffer.destroy();
    }
};

struct Instance {
    size_t model_ix;
    std::string name;

    glm::vec3 translate{0.0f};
    glm::vec3 rotate{0.0f};
    glm::vec3 scale{1.0f};

    void update_transform(glm::mat4* transform) const {
        *transform = glm::translate(glm::identity<glm::mat4>(), translate) *
                     glm::rotate(glm::rotate(glm::rotate(glm::identity<glm::mat4>(), rotate.x, glm::vec3{0, 1, 0}),
                                             rotate.y, glm::vec3{1, 0, 0}),
                                 rotate.z, glm::vec3{0, 0, 1}) *
                     glm::scale(glm::identity<glm::mat4>(), scale);
    }
};

struct Scene {
    std::string name;

    std::vector<Model> models{};
    std::array<std::vector<Model>, engine::frames_in_flight> models_to_destroy{};
    std::vector<Instance> instances{};
    size_t selected_instance = -1;

    static std::vector<Scene> init(size_t& current_scene) {
        return {};
    }

    void save() {}

    void load_models() {}

    void unload_models() {}

    void add_model(std::filesystem::path path, VkBufferMemoryBarrier2* barrier, uint64_t timeline_value) {
        uint32_t size;
        auto* file = engine::util::read_file(path, &size);

        auto extension = path.extension();
        barrier->dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        barrier->dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        Model::DataType type;
        if (extension == ".glb") {
            type = Model::GLB;
        } else if (extension == ".gltf") {
            type = Model::GLTF;
        } else if (extension == ".gom") {
            type = Model::GOM;
        } else {
            assert(false && "Wrong filetype");
        }

        models.emplace_back();
        models.back().name = path.stem();
        models.back().filepath = std::move(path);
        models.back().timeline = timeline_value;

        auto& model = models.back();
        auto err = model.load(models.back().filepath.parent_path(), type, file, size, barrier);
        if (err != engine::Model::Ok) {
            printf("error: %d @%d in %s\n", err, __LINE__, __FILE__);
            assert(false);
        }

        free(file);
    }

    void destroy() {
        for (auto& model : models) {
            model.destroy();
        }
        for (auto& models : models_to_destroy) {
            for (auto& model : models) {
                model.destroy();
            }
        }
    }
};

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "init") == 0) {
        project::init();
        return 0;
    }

    if (!project::find_project()) {
        printf("Project root couldn't be find. To initialize a project use `%s init`\n", argv[0]);
        return 0;
    }
    std::filesystem::current_path(project::project_root);

    engine::init("Goliath editor", 1000, false);

    bool model_parse_error = false;
    models::init(project::models_registry, &model_parse_error);
    if (model_parse_error) {
        printf("Models weren't loaded");
        return 0;
    }

    auto mgid = models::add("./DamagedHelmet.glb", "Damanged Helmet");

    bool scene_parse_error = false;
    scene::load(project::scenes_file, &scene_parse_error);
    if (scene_parse_error) {
        printf("Scenes weren't loaded");
        return 0;
    }

    size_t scene_count = 0;
    size_t current_scene = -1;
    std::vector<Scene> scenes = Scene::init(current_scene);
    if (scenes.size() == 0 || current_scene == -1) {
        scenes.emplace_back("Default");
        scene_count++;
        current_scene = scenes.size() - 1;
    }
    Scene* scene = &scenes[current_scene];

    NFD_Init();
    engine::culling::init(8192);

    VkImageMemoryBarrier2* visbuffer_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * engine::frames_in_flight * 2);
    engine::visbuffer::init(visbuffer_barriers);
    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
        visbuffer_barriers[i].srcAccessMask = 0;
        visbuffer_barriers[i].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        visbuffer_barriers[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        visbuffer_barriers[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    bool visbuffer_barriers_applied = false;

    uint32_t mesh_fragment_spv_size;
    auto mesh_fragment_spv_data = engine::util::read_file("mesh_fragment.spv", &mesh_fragment_spv_size);
    auto mesh_fragment_module = engine::create_shader({mesh_fragment_spv_data, mesh_fragment_spv_size});
    free(mesh_fragment_spv_data);

    uint32_t visbuffer_raster_vertex_spv_size;
    auto visbuffer_raster_vertex_spv_data =
        engine::util::read_file("visbuffer_raster_vertex.spv", &visbuffer_raster_vertex_spv_size);
    auto visbuffer_raster_vertex_module =
        engine::create_shader({visbuffer_raster_vertex_spv_data, visbuffer_raster_vertex_spv_size});
    free(visbuffer_raster_vertex_spv_data);

    uint32_t visbuffer_raster_fragment_spv_size;
    auto visbuffer_raster_fragment_spv_data =
        engine::util::read_file("visbuffer_raster_fragment.spv", &visbuffer_raster_fragment_spv_size);
    auto visbuffer_raster_fragment_module =
        engine::create_shader({visbuffer_raster_fragment_spv_data, visbuffer_raster_fragment_spv_size});
    free(visbuffer_raster_fragment_spv_data);

    auto visbuffer_raster_pipeline = engine::GraphicsPipeline(engine::GraphicsPipelineBuilder{}
                                                                  .vertex(visbuffer_raster_vertex_module)
                                                                  .fragment(visbuffer_raster_fragment_module)
                                                                  .push_constant_size(VisbufferRasterPC::size)
                                                                  .add_color_attachment(engine::visbuffer::format)
                                                                  .depth_format(VK_FORMAT_D16_UNORM))
                                         .depth_test(true)
                                         .depth_write(true)
                                         .depth_compare_op(engine::CompareOp::Less)
                                         .cull_mode(engine::CullMode::NoCull);

    uint32_t pbr_spv_size;
    auto pbr_spv_data = engine::util::read_file("pbr.spv", &pbr_spv_size);
    auto pbr_module = engine::create_shader({pbr_spv_data, pbr_spv_size});
    free(pbr_spv_data);

    auto pbr_shading_set_layout = engine::DescriptorSet<engine::descriptor::Binding{
        .count = 1,
        .type = engine::descriptor::Binding::UBO,
        .stages = VK_SHADER_STAGE_COMPUTE_BIT,
    }>::create();
    auto pbr_pipeline =
        engine::ComputePipeline(engine::ComputePipelineBuilder{}
                                    .shader(pbr_module)
                                    .descriptor_layout(0, engine::visbuffer::shading_set_layout)
                                    .descriptor_layout(1, pbr_shading_set_layout)
                                    .descriptor_layout(2, engine::texture_registry::get_texture_pool().set_layout)
                                    .push_constant(PBRPC::size));

    uint32_t postprocessing_spv_size;
    auto postprocessing_spv_data = engine::util::read_file("postprocessing.spv", &postprocessing_spv_size);
    auto postprocessing_module = engine::create_shader({postprocessing_spv_data, postprocessing_spv_size});
    free(postprocessing_spv_data);

    auto postprocessing_set_layout = engine::DescriptorSet<engine::descriptor::Binding{
                                                               .count = 1,
                                                               .type = engine::descriptor::Binding::StorageImage,
                                                               .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                           },
                                                           engine::descriptor::Binding{
                                                               .count = 1,
                                                               .type = engine::descriptor::Binding::StorageImage,
                                                               .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                           }>::create();
    auto postprocessing_pipeline = engine::ComputePipeline(engine::ComputePipelineBuilder{}
                                                               .shader(postprocessing_module)
                                                               .descriptor_layout(0, postprocessing_set_layout)
                                                               .push_constant(PostprocessingPC::size));

    engine::GPUImage* depth_images = (engine::GPUImage*)malloc(sizeof(engine::GPUImage) * engine::frames_in_flight);
    VkImageView* depth_image_views = (VkImageView*)malloc(sizeof(VkImageView) * engine::frames_in_flight);
    bool depth_barriers_applied = false;
    VkImageMemoryBarrier2* depth_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * engine::frames_in_flight);

    update_depth(depth_images, depth_image_views, depth_barriers, engine::frames_in_flight);

    engine::GPUImage* target_images = (engine::GPUImage*)malloc(sizeof(engine::GPUImage) * engine::frames_in_flight);
    VkImageView* target_image_views = (VkImageView*)malloc(sizeof(VkImageView) * engine::frames_in_flight);
    bool target_barriers_applied = false;
    VkImageMemoryBarrier2* target_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * engine::frames_in_flight);

    update_target(target_images, target_image_views, target_barriers, engine::frames_in_flight);

    constexpr uint32_t max_draw_size = 4096;
    engine::Buffer draw_id_buffers[engine::frames_in_flight];
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        draw_id_buffers[i] =
            engine::Buffer::create(sizeof(glm::vec4) + max_draw_size * (sizeof(glm::mat4) + sizeof(glm::vec4)),
                                   VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT |
                                       VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
                                   std::nullopt);
    }

    engine::Buffer indirect_draw_buffers[engine::frames_in_flight];
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        indirect_draw_buffers[i] = engine::Buffer::create(
            sizeof(engine::culling::CulledDrawCommand) * max_draw_size,
            VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, std::nullopt);
    }

    constexpr uint32_t max_transform_size = 1024;
    uint32_t transform_buffer_sizes[2]{};
    engine::Buffer transform_buffers[engine::frames_in_flight];
    glm::mat4* transforms[2] = {
        (glm::mat4*)malloc(sizeof(glm::mat4) * max_transform_size),
        (glm::mat4*)malloc(sizeof(glm::mat4) * max_transform_size),
    };
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        transform_buffers[i] = engine::Buffer::create(
            sizeof(glm::mat4) * 1024, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
            std::nullopt);
    }

    VkBufferMemoryBarrier2 model_barrier{};
    // VkBufferMemoryBarrier2 scene_indirect_buffer_barrier{};
    bool model_barrier_applied = true;
    // bool scene_indirect_buffer_barrier_applied = true;

    float fov = 90.0f;
    glm::vec3 look_at{0.0f};
    engine::Camera cam{};
    cam.set_projection(engine::camera::Perspective{
        .fov = glm::radians(fov),
        .aspect_ratio = 16.0f / 10.0f,
    });
    cam.position = glm::vec3{10.0f};
    cam.look_at(look_at);
    cam.update_matrices();

    glm::vec3 res_movement{0.0f};

    bool lock_cam = false;
    glfwSetInputMode(engine::window, GLFW_CURSOR, lock_cam ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    engine::imgui::enable(false);

    float sensitivity = 1.0f;

    glm::vec3 light_intensity{1.0f};
    glm::vec3 light_position{5.0f};

    bool test_x = false;

    ImVec2 game_windows[engine::frames_in_flight]{};
    engine::GPUImage game_window_images[engine::frames_in_flight]{};
    VkImageView game_window_image_views[engine::frames_in_flight]{};
    VkDescriptorSet game_window_textures[engine::frames_in_flight]{};
    std::pair<bool, VkImageMemoryBarrier2> game_window_barriers[engine::frames_in_flight]{};
    VkSampler game_window_sampler = engine::Sampler{}.create();

    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        game_windows[i].x = -1.0f;
        game_windows[i].y = -1.0f;
    }

    double accum = 0;
    double last_time = glfwGetTime();

    bool done = false;
    uint64_t update_count = 0;
    while (!glfwWindowShouldClose(engine::window)) {
        double time = glfwGetTime();
        double frame_time = time - last_time;
        last_time = time;
        accum += frame_time;

        auto state = engine::event::poll();
        if (state == engine::event::Minimized) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }

        while (accum >= (1000.0 / 60.0) / 1000.0) {
            accum -= (1000.0 / 60.0) / 1000.0;

            cam.set_projection(engine::camera::Perspective{
                .fov = glm::radians(fov),
                .aspect_ratio = 16.0f / 10.0f,
            });

            if (engine::event::was_released(GLFW_KEY_L)) {
                lock_cam = !lock_cam;
                glfwSetInputMode(engine::window, GLFW_CURSOR, lock_cam ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                engine::imgui::enable(lock_cam);
            }

            glm::vec3 movement{0.0f};
            if (engine::event::is_held(GLFW_KEY_W)) {
                movement.z -= 1.0f;
            }
            if (engine::event::is_held(GLFW_KEY_S)) {
                movement.z += 1.0f;
            }
            if (engine::event::is_held(GLFW_KEY_A)) {
                movement.x -= 1.0f;
            }
            if (engine::event::is_held(GLFW_KEY_D)) {
                movement.x += 1.0f;
            }
            bool moved = movement.x != 0.0f || movement.z != 0.0f;

            res_movement = glm::vec3{0.0f};
            if (moved) {
                auto forward = cam.forward();
                auto right = cam.right();
                auto normalized_movement = glm::normalize(movement);

                cam.position += -normalized_movement.z * forward + normalized_movement.x * right;
            }

            auto mouse_delta = engine::event::get_mouse_delta();
            if (!lock_cam && (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)) {
                cam.rotate(glm::radians(-mouse_delta.x), glm::radians(-mouse_delta.y));
            }

            cam.update_matrices();

            update_count++;
            engine::event::update_tick();
        }

        if (engine::prepare_frame()) {
            goto end_of_frame;
        }

        {
            auto& game_window = game_windows[engine::get_current_frame()];
            auto& game_window_image = game_window_images[engine::get_current_frame()];
            auto& game_window_image_view = game_window_image_views[engine::get_current_frame()];
            auto& game_window_texture = game_window_textures[engine::get_current_frame()];
            auto& [game_window_barrier_applied, game_window_barrier] =
                game_window_barriers[engine::get_current_frame()];
            bool skip_game_window = false;

            engine::imgui::begin();
            ImGui::DockSpaceOverViewport();

            ImGui::Begin("Game");
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x <= 0 || avail.y <= 0) skip_game_window = true;
            if ((avail.x != game_window.x || avail.y != game_window.y) && !skip_game_window) {
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

                game_window = avail;
            }

            if (!skip_game_window) ImGui::Image(game_window_texture, game_window);
            ImGui::End();

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
                        args.defaultPath = current_path.c_str();
                        NFD_GetNativeWindowFromGLFWWindow(engine::window, &args.parentWindow);

                        const nfdpathset_t* paths;
                        auto res = NFD_OpenDialogMultipleU8_With(&paths, &args);
                        if (res == NFD_OKAY) {
                            nfdpathsetenum_t enumerator;
                            NFD_PathSet_GetEnum(paths, &enumerator);

                            nfdchar_t* path;
                            std::size_t i = 0;

                            auto timeline_value = engine::transport::begin();
                            while (NFD_PathSet_EnumNext(&enumerator, &path) && path) {
                                scene->add_model(path, &model_barrier, timeline_value);
                                NFD_PathSet_FreePath(path);
                            }
                            engine::transport::end();

                            NFD_PathSet_FreeEnum(&enumerator);
                            NFD_PathSet_Free(paths);
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMainMenuBar();

            uint32_t transforms_count = 0;
            if (ImGui::Begin("Instances")) {
                for (size_t i = 0; i < scene->instances.size(); i++) {
                    ImGui::PushID(i);

                    if (ImGui::Selectable("", scene->selected_instance == i,
                                          ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                                          ImVec2(0.0, ImGui::GetFrameHeight()))) {
                        scene->selected_instance = i;
                    }

                    ImGui::SameLine();
                    ImGui::InputText("##name", &scene->instances[i].name);
                    if (ImGui::IsItemClicked()) {
                        scene->selected_instance = i;
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("X")) {
                        if (scene->selected_instance == i) scene->selected_instance = -1;
                        scene->instances.erase(scene->instances.begin() + i);
                        for (auto& m : scene->models) {
                            std::erase_if(m.instances, [&](auto& instance) -> bool {
                                if (instance > i) {
                                    instance--;
                                    return false;
                                } else {
                                    return instance == i;
                                }
                            });
                        }
                        i--;
                        ImGui::PopID();
                        continue;
                    }

                    scene->instances[i].update_transform(transforms[engine::get_current_frame()] + transforms_count);
                    transforms_count++;

                    ImGui::PopID();
                }
            }
            ImGui::End();

            if (ImGui::Begin("Models")) {
                for (size_t i = 0; i < scene->models.size(); i++) {
                    auto& model = scene->models[i];
                    ImGui::PushID(model.gpu_group.data.address());

                    ImGuiID id = ImGui::GetID("##node");
                    auto* storage = ImGui::GetCurrentWindow()->DC.StateStorage;
                    bool open = storage->GetBool(id, false);
                    bool pressed = ImGui::Selectable(
                        "##node",
                        scene->selected_instance != -1 && scene->instances[scene->selected_instance].model_ix == i,
                        ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                        ImVec2(0.0, ImGui::GetFrameHeight()));
                    if (pressed) {
                        open = !open;
                        storage->SetBool(id, open);
                    }

                    ImGui::SameLine();
                    ImGui::InputText("##name", &model.name);

                    ImGui::SameLine();
                    if (ImGui::Button("+")) {
                        scene->instances.emplace_back(i, std::format("{} #{}", model.name, model.last_instance_id++));
                        model.instances.emplace_back(scene->instances.size() - 1);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("X")) {
                        scene->models_to_destroy[(engine::get_current_frame() - 1) % engine::frames_in_flight]
                            .emplace_back(model);

                        if (scene->selected_instance != -1 && scene->instances[scene->selected_instance].model_ix == i)
                            scene->selected_instance = -1;
                        std::erase_if(scene->instances, [&](auto& instance) -> bool {
                            if (instance.model_ix > i) {
                                instance.model_ix--;
                                return false;
                            } else {
                                return instance.model_ix == i;
                            }
                        });
                        scene->models.erase(scene->models.begin() + i);
                        for (auto& m : scene->models) {
                            m.instances.clear();
                        }
                        for (size_t j = 0; j < scene->instances.size(); j++) {
                            scene->models[scene->instances[j].model_ix].instances.emplace_back(j);
                        }
                        i--;
                        ImGui::PopID();
                        continue;
                    }

                    if (open) {
                        ImGui::Indent();
                        ImGui::TextWrapped("file path: %s", model.filepath.c_str());
                        ImGui::Checkbox("Embed", &model.embed_optimized);

                        ImGui::Unindent();
                    }

                    ImGui::PopID();
                }
            }
            ImGui::End();

            if (ImGui::Begin("Scenes", nullptr)) {
                if (ImGui::Button("+")) {
                    scene_count++;
                    scenes.emplace_back(std::format("Scene {}", scene_count));
                    scene = &scenes[current_scene];
                }

                for (size_t i = 0; i < scenes.size(); i++) {
                    ImGui::PushID(i);

                    if (ImGui::Selectable("", &scenes[i] == scene,
                                          ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                                          ImVec2(0.0, ImGui::GetFrameHeight()))) {
                        scene = &scenes[i];
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("X") && scenes.size() != 1) {
                        if (current_scene == i) current_scene = i - 1;
                        else if (current_scene > i) current_scene--;
                        current_scene = std::min<size_t>(0, current_scene);

                        scene->destroy();
                        scenes.erase(scenes.begin() + i);
                        i--;

                        scene = &scenes[current_scene];
                        ImGui::PopID();
                        continue;
                    }

                    ImGui::SameLine();
                    ImGui::InputText("##name", &scenes[i].name);

                    ImGui::PopID();
                }
            }
            ImGui::End();

            if (ImGui::Begin("Transformation") && scene->selected_instance != -1) {
                auto& instance = scene->instances[scene->selected_instance];

                ImGui::InputText("name: ", &instance.name);

                ImGui::DragFloat3("XYZ", glm::value_ptr(instance.translate), 0.1f, 0.0f, 0.0f, "%.2f");

                ImGui::DragFloat("yaw", &instance.rotate.x, 0.1f, 0.0, 0.0, "%.2f");
                ImGui::DragFloat("pitch", &instance.rotate.y, 0.1f, 0.0, 0.0, "%.2f");
                ImGui::DragFloat("roll", &instance.rotate.z, 0.1f, 0.0, 0.0, "%.2f");

                ImGui::DragFloat3("scale", glm::value_ptr(instance.scale), 0.1f, 0.0f, 0.0f, "%.2f");

                instance.update_transform(transforms[engine::get_current_frame()] + scene->selected_instance);
            }
            ImGui::End();

            if (ImGui::Begin("Config")) {
                ImGui::SeparatorText("Camera");
                ImGui::SliderFloat("sensitivity", &sensitivity, 0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("fov", &fov, 0.0f, 360.0f, "%.0f");
                ImGui::DragFloat3("position", glm::value_ptr(cam.position), 0.1f);
                ImGui::DragFloat3("look at", glm::value_ptr(look_at), 0.1f);
                if (ImGui::Button("update look at")) {
                    cam.look_at(look_at);
                    cam.update_matrices();
                }
                ImGui::Text("lock cam: %b", lock_cam);

                ImGui::SeparatorText("Lighting");
                ImGui::DragFloat3("intensity", glm::value_ptr(light_intensity), 0.1f);
                ImGui::DragFloat3("position##sybau", glm::value_ptr(light_position), 0.1f);
            }

            ImGui::End();
            engine::imgui::end();

            engine::prepare_draw();

            if (!visbuffer_barriers_applied || !depth_barriers_applied || !target_barriers_applied ||
                !model_barrier_applied /* || !scene_indirect_buffer_barrier_applied */ ||
                !game_window_barrier_applied) {
                engine::synchronization::begin_barriers();
                if (!visbuffer_barriers_applied) {
                    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(visbuffer_barriers[i]);
                    }

                    visbuffer_barriers_applied = true;
                }
                if (!depth_barriers_applied) {
                    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(depth_barriers[i]);
                    }

                    depth_barriers_applied = true;
                }
                if (!target_barriers_applied) {
                    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(target_barriers[i]);
                    }

                    depth_barriers_applied = true;
                }
                if (!model_barrier_applied) {
                    engine::synchronization::apply_barrier(model_barrier);

                    model_barrier_applied = true;
                }
                // if (!scene_indirect_buffer_barrier_applied) {
                //     engine::synchronization::apply_barrier(scene_indirect_buffer_barrier);
                //
                //     scene_indirect_buffer_barrier_applied = true;
                // }
                if (!game_window_barrier_applied && !skip_game_window) {
                    engine::synchronization::apply_barrier(game_window_barrier);

                    game_window_barrier_applied = true;
                }
                engine::synchronization::end_barriers();
            }

            auto& transform_buffer = transform_buffers[engine::get_current_frame()];
            VkBufferMemoryBarrier2 transform_barrier{};
            transform_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            transform_barrier.pNext = nullptr;
            transform_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            transform_barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;

            if (transforms_count != 0) {
                engine::transport::begin();
                engine::transport::upload(&transform_barrier, transforms[engine::get_current_frame()],
                                          transforms_count * sizeof(glm::mat4), transform_buffer.data(), 0);
                auto timeline_wait = engine::transport::end();

                engine::synchronization::begin_barriers();
                engine::synchronization::apply_barrier(transform_barrier);
                engine::synchronization::end_barriers();

                while (!engine::transport::is_ready(timeline_wait)) {}
            } else {
                transform_barrier.buffer = transform_buffer;
            }
            transform_barrier.offset = 0;
            transform_barrier.size = transform_buffer.size();
            transform_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            transform_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            VkClearColorValue target_clear_color{};
            target_clear_color.float32[0] = 0.0f;
            target_clear_color.float32[1] = 0.0f;
            target_clear_color.float32[2] = 0.3f;
            target_clear_color.float32[3] = 1.0f;

            VkClearColorValue game_window_clear_color{};
            target_clear_color.float32[0] = 0.0f;
            target_clear_color.float32[1] = 0.0f;
            target_clear_color.float32[2] = 0.3f;
            target_clear_color.float32[3] = 1.0f;

            VkImageSubresourceRange clear_range{};
            clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clear_range.baseMipLevel = 0;
            clear_range.layerCount = 1;
            clear_range.baseArrayLayer = 0;
            clear_range.levelCount = 1;

            vkCmdClearColorImage(engine::get_cmd_buf(), target_images[engine::get_current_frame()].image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &target_clear_color, 1, &clear_range);
            if (!skip_game_window) {
                vkCmdClearColorImage(engine::get_cmd_buf(), game_window_image.image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &game_window_clear_color, 1, &clear_range);
            }

            engine::culling::bind_flatten();
            uint transform_ix = 0;
            for (auto& model : scene->models) {
                if (!engine::transport::is_ready(model.timeline)) continue;

                for (const auto& instance_ix : model.instances) {
                    engine::culling::flatten(model.gpu_group.data.address(), model.gpu_data.mesh_count,
                                             model.indirect_draw_buffer.address(), transform_buffer.address(),
                                             instance_ix * sizeof(glm::mat4));
                    transform_ix++;
                }
            }

            // for (auto& scene : scenes) {
            //     if (!engine::transport::is_ready(scene.timeline)) continue;
            //
            //     engine::culling::flatten(scene.gpu_group.data.address(), scene.gpu_data.draw_count,
            //                              scene.gpu_data.draw_indirect.address(), scene.gpu_group.data.address(), 0);
            // }

            auto& draw_id_buffer = draw_id_buffers[engine::get_current_frame()];
            auto& indirect_draw_buffer = indirect_draw_buffers[engine::get_current_frame()];

            engine::culling::cull(max_draw_size, draw_id_buffer.address(), indirect_draw_buffer.address());

            engine::synchronization::begin_barriers();
            engine::culling::sync_for_draw(draw_id_buffer, indirect_draw_buffer);
            engine::synchronization::end_barriers();

            engine::visbuffer::prepare_for_draw();

            engine::rendering::begin(
                engine::RenderPass{}
                    .add_color_attachment(engine::visbuffer::attach())
                    .depth_attachment(engine::RenderingAttachement{}
                                          .set_image(depth_image_views[engine::get_current_frame()],
                                                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                          .set_clear_depth(1.0f)
                                          .set_load_op(engine::LoadOp::Clear)
                                          .set_store_op(engine::StoreOp::Store)));

            uint8_t visbuffer_raster_pc[VisbufferRasterPC::size];
            VisbufferRasterPC::write(visbuffer_raster_pc, draw_id_buffer.address(), indirect_draw_buffer.address(),
                                     cam.view_projection());

            visbuffer_raster_pipeline.bind();
            visbuffer_raster_pipeline.draw_indirect_count(engine::GraphicsPipeline::DrawIndirectCountParams{
                .push_constant = visbuffer_raster_pc,
                .draw_buffer = indirect_draw_buffer.data(),
                .count_buffer = draw_id_buffer.data(),
                .max_draw_count = max_draw_size,
                .stride = sizeof(engine::culling::CulledDrawCommand),
            });
            engine::rendering::end();

            engine::visbuffer::count_materials(draw_id_buffer.address());
            engine::visbuffer::get_offsets();
            engine::visbuffer::write_fragment_ids(draw_id_buffer.address());

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

            auto shading = engine::visbuffer::shade(target_image_views[engine::get_current_frame()]);
            for (uint16_t mat_id = 0; mat_id < shading.material_id_count; mat_id++) {
                uint8_t pbr_pc[PBRPC::size]{};
                PBRPC::write(pbr_pc,
                             glm::vec<2, uint32_t>{engine::swapchain_extent.width, engine::swapchain_extent.height},
                             engine::visbuffer::stages.address() + shading.indirect_buffer_offset,
                             engine::visbuffer::stages.address() + shading.fragment_id_buffer_offset,
                             draw_id_buffer.address(), mat_id);

                PBRShadingSet shading_set_data{
                    .cam_pos = cam.position,
                    .light_pos = light_position,
                    .light_intensity = light_intensity,
                    .view_proj_matrix = cam.view_projection(),
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
                            engine::texture_registry::get_texture_pool().set,
                            engine::descriptor::null_set,
                        },
                    .indirect_buffer = engine::visbuffer::stages,
                    .buffer_offset = shading.indirect_buffer_offset,
                });
            }

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();

            auto pp_set = engine::descriptor::new_set(postprocessing_set_layout);
            engine::descriptor::begin_update(pp_set);
            engine::descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL,
                                                     target_image_views[engine::get_current_frame()]);
            engine::descriptor::update_storage_image(1, VK_IMAGE_LAYOUT_GENERAL, engine::visbuffer::get_view());
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

            VkImageMemoryBarrier2 game_window_texture_barrier{};
            game_window_texture_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            game_window_texture_barrier.pNext = nullptr;
            game_window_texture_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            game_window_texture_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            game_window_texture_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            game_window_texture_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            game_window_texture_barrier.image = game_window_image.image;
            game_window_texture_barrier.subresourceRange = VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            game_window_texture_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            game_window_texture_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            game_window_texture_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            game_window_texture_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            if (!skip_game_window) engine::synchronization::apply_barrier(game_window_texture_barrier);
            engine::synchronization::end_barriers();

            if (!skip_game_window) {
                VkImageBlit2 blit_region{};
                blit_region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
                blit_region.srcOffsets[0] = VkOffset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                };
                blit_region.srcOffsets[1] = VkOffset3D{
                    .x = std::min((int32_t)game_window.x, (int32_t)engine::swapchain_extent.width),
                    .y = std::min((int32_t)game_window.y, (int32_t)engine::swapchain_extent.height),
                    .z = 1,
                };
                blit_region.srcSubresource = VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };
                blit_region.dstOffsets[0] = VkOffset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                };
                blit_region.dstOffsets[1] = VkOffset3D{
                    .x = std::min((int32_t)game_window.x, (int32_t)engine::swapchain_extent.width),
                    .y = std::min((int32_t)game_window.y, (int32_t)engine::swapchain_extent.height),
                    .z = 1,
                };
                blit_region.dstSubresource = VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };
                VkBlitImageInfo2 blit_info{};
                blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
                blit_info.srcImage = target_images[engine::get_current_frame()].image;
                blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                blit_info.dstImage = game_window_image.image;
                blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                blit_info.filter = VK_FILTER_NEAREST;
                blit_info.regionCount = 1;
                blit_info.pRegions = &blit_region;
                vkCmdBlitImage2(engine::get_cmd_buf(), &blit_info);
            }

            game_window_texture_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            game_window_texture_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            game_window_texture_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            game_window_texture_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            game_window_texture_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            game_window_texture_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

            engine::synchronization::begin_barriers();
            if (!skip_game_window) engine::synchronization::apply_barrier(game_window_texture_barrier);
            engine::synchronization::end_barriers();

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

            game_window_texture_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            game_window_texture_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            game_window_texture_barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            game_window_texture_barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            game_window_texture_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            game_window_texture_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            transform_barrier.srcAccessMask = transform_barrier.dstAccessMask;
            transform_barrier.srcStageMask = transform_barrier.dstStageMask;
            transform_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            transform_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(transform_barrier);
            engine::synchronization::end_barriers();

            engine::visbuffer::clear_buffers();
            engine::culling::clear_buffers(draw_id_buffer, indirect_draw_buffer);
            vkCmdFillBuffer(engine::get_cmd_buf(), transform_buffer, 0, transform_buffer.size(), 0);

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            if (!skip_game_window) engine::synchronization::apply_barrier(game_window_texture_barrier);
            engine::synchronization::end_barriers();
        }

    end_of_frame:
        for (auto& model : scene->models_to_destroy[engine::get_current_frame()]) {
            model.destroy();
        }
        scene->models_to_destroy[engine::get_current_frame()].clear();

        if (engine::next_frame()) {
            for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                engine::GPUImageView::destroy(depth_image_views[i]);
                depth_images[i].destroy();

                engine::GPUImageView::destroy(target_image_views[i]);
                target_images[i].destroy();
            }

            update_depth(depth_images, depth_image_views, depth_barriers, engine::frames_in_flight);
            depth_barriers_applied = false;

            update_target(target_images, target_image_views, target_barriers, engine::frames_in_flight);
            target_barriers_applied = false;

            visbuffer_raster_pipeline.update_viewport_to_swapchain();
            visbuffer_raster_pipeline.update_scissor_to_viewport();

            engine::visbuffer::resize(visbuffer_barriers, true, false);
            for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
                visbuffer_barriers[i].srcAccessMask = 0;
                visbuffer_barriers[i].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                visbuffer_barriers[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                visbuffer_barriers[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            }
            visbuffer_barriers_applied = false;
        }
    }

    vkDeviceWaitIdle(engine::device);

    NFD_Quit();

    for (auto& s : scenes) {
        s.destroy();
    }

    visbuffer_raster_pipeline.destroy();
    engine::destroy_shader(visbuffer_raster_fragment_module);
    engine::destroy_shader(visbuffer_raster_vertex_module);

    engine::destroy_shader(mesh_fragment_module);

    engine::destroy_descriptor_set_layout(pbr_shading_set_layout);
    pbr_pipeline.destroy();
    engine::destroy_shader(pbr_module);

    engine::destroy_descriptor_set_layout(postprocessing_set_layout);
    postprocessing_pipeline.destroy();
    engine::destroy_shader(postprocessing_module);

    for (std::size_t i = 0; i < engine::frames_in_flight; i++) {
        engine::GPUImageView::destroy(depth_image_views[i]);
        depth_images[i].destroy();

        engine::GPUImageView::destroy(target_image_views[i]);

        target_images[i].destroy();

        draw_id_buffers[i].destroy();
        indirect_draw_buffers[i].destroy();
        transform_buffers[i].destroy();
        free(transforms[i]);

        game_window_images[i].destroy();
        engine::GPUImageView::destroy(game_window_image_views[i]);
        ImGui_ImplVulkan_RemoveTexture(game_window_textures[i]);
    }
    engine::Sampler::destroy(game_window_sampler);

    free(target_images);
    free(target_image_views);
    free(target_barriers);

    free(depth_image_views);
    free(depth_images);

    engine::culling::destroy();
    engine::visbuffer::destroy();
    engine::destroy();
    return 0;
}
