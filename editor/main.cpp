#include "goliath/buffer.hpp"
#include "goliath/camera.hpp"
#include "goliath/compute.hpp"
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
#include "goliath/transport.hpp"
#include "goliath/util.hpp"
#include "goliath/visbuffer.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <filesystem>
#include <format>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
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
                                                                 .extent(VkExtent3D{
                                                                     .width = engine::swapchain_extent.width,
                                                                     .height = engine::swapchain_extent.height,
                                                                     .depth = 1,
                                                                 })
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
                                         .extent(VkExtent3D{
                                             .width = engine::swapchain_extent.width,
                                             .height = engine::swapchain_extent.height,
                                             .depth = 1,
                                         })
                                         .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                         .new_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                                         .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        images[i] = target_img;
        barriers[i] = barrier;
        image_views[i] = engine::GPUImageView{images[i]}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).create();
    }
}

using ModelPushConstant = engine::PushConstant<uint64_t, uint64_t, glm::mat4, glm::mat4>;
using ScenePushConstant = engine::PushConstant<uint64_t, uint64_t, glm::mat4, glm::mat4>;

struct Model {
    enum DataType {
        GLTF,
        GLB,
        GOM,
    };

    struct Instance {
        std::string name;

        glm::vec3 translate{0.0f};
        glm::vec3 rotate{0.0f};
        glm::vec3 scale{1.0f};
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

    std::vector<Instance> instances{};
    std::vector<glm::mat4> instance_transforms{};

    engine::Model::Err load(DataType type, uint8_t* data, uint32_t size, VkBufferMemoryBarrier2* barrier) {
        engine::Model::Err err;
        if (type == GOM) {
            err = engine::Model::load_optimized(&cpu_data, data) ? engine::Model::Ok : engine::Model::InvalidFormat;
        } else if (type == GLTF) {
            err = engine::Model::load_gltf(&cpu_data, {data, size});
        } else if (type == GLB) {
            err = engine::Model::load_glb(&cpu_data, {data, size}, nullptr);
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

bool imgui_model_instance(Model::Instance& instance, glm::mat4& transform) {
    bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanLabelWidth);

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
    ImGui::InputText("##name", &instance.name);

    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
    if (ImGui::Button("Remove")) {
        if (open) ImGui::TreePop();
        return true;
    }

    if (open) {
        ImGui::DragFloat3("XYZ", glm::value_ptr(instance.translate), 0.1f, 0.0f, 0.0f, "%.2f");

        ImGui::DragFloat("yaw", &instance.rotate.x, 0.1f, 0.0, 0.0, "%.2f");
        ImGui::DragFloat("pitch", &instance.rotate.y, 0.1f, 0.0, 0.0, "%.2f");
        ImGui::DragFloat("roll", &instance.rotate.z, 0.1f, 0.0, 0.0, "%.2f");

        ImGui::DragFloat3("scale", glm::value_ptr(instance.scale), 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::TreePop();
    }

    transform = glm::translate(glm::identity<glm::mat4>(), instance.translate) *
                glm::rotate(glm::rotate(glm::rotate(glm::identity<glm::mat4>(), instance.rotate.x, glm::vec3{0, 1, 0}),
                                        instance.rotate.y, glm::vec3{1, 0, 0}),
                            instance.rotate.z, glm::vec3{0, 0, 1}) *
                glm::scale(glm::identity<glm::mat4>(), instance.scale);

    return false;
}

// <model count><total instance count><external count><names size>{<is external><name metadata><name data|if <name
// metadata> valid><instance count><instance transforms><model data size><model data> |*model count}
// <model data> = <embedded||GLB||GLTF>(<binary embeded data> || <file path>)
uint8_t* serialize_scene(const Model* models, uint32_t models_size, uint32_t* out_size) {
    uint32_t total_size = sizeof(uint32_t) /* <model count> */ + sizeof(uint32_t) /* <total instance count> */ +
                          sizeof(uint32_t) /* <external count> */ + sizeof(uint32_t) /* <names size> */ +
                          sizeof(uint32_t) * models_size /* <name metadata>s */ +
                          sizeof(uint32_t) * models_size /* <instance count>s */;
    uint32_t total_instance_count = 0;
    uint32_t names_size = 0;

    for (uint32_t i = 0; i < models_size; i++) {
        total_size += sizeof(uint8_t); // <is external>

        auto& model = models[i];
        names_size += model.name.size() + 1;

        total_size += sizeof(uint8_t); // <embedded||GLB||GLTF>
        if (model.embed_optimized) {
            // total_size += model.cpu_data.; // <model data size>
        } else {
            total_size += sizeof(uint32_t);                   // <filepath size>
            total_size += model.filepath.string().size() + 1; // <model data size>
        }

        total_instance_count += model.instances.size();
    }
    total_size += total_instance_count * sizeof(glm::mat4); // <instance transforms>s
    total_size += names_size;                               // <names size>

    uint8_t* out = (uint8_t*)malloc(total_size);

    uint32_t offset = 0;
    std::memcpy(out + offset, &models_size, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    std::memcpy(out + offset, &total_instance_count, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    std::memset(out + offset, 0, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    std::memcpy(out + offset, &names_size, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    for (std::size_t i = 0; i < models_size; i++) {
        auto& model = models[i];

        std::memset(out + offset, 0, sizeof(uint8_t));
        offset += sizeof(uint8_t);

        uint32_t name_size = model.name.size() + 1;
        std::memcpy(out + offset, &name_size, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(out + offset, model.name.c_str(), name_size);
        offset += name_size;

        uint32_t instance_count = model.instances.size();
        std::memcpy(out + offset, &instance_count, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(out + offset, model.instance_transforms.data(),
                    model.instance_transforms.size() * sizeof(glm::mat4));
        offset += model.instance_transforms.size() * sizeof(glm::mat4);

        if (model.embed_optimized) {
            std::memset(out + offset, 0, sizeof(uint8_t));
            offset += sizeof(uint8_t);

        } else {
            uint8_t model_type = model.filepath.extension() == ".glb" ? 1 : 2;
            std::memcpy(out + offset, &model_type, sizeof(uint8_t));
            offset += sizeof(uint8_t);

            uint32_t filepath_size = model.filepath.string().size() + 1;
            std::memcpy(out + offset, &filepath_size, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            std::memcpy(out + offset, model.filepath.c_str(), filepath_size);
            offset += filepath_size;
        }
    }

    printf("total size: %d\n", total_size);
    printf("offset: %d\n", offset);
    *out_size = total_size;
    return out;
}

struct Scene {
    std::string name;
    std::filesystem::path filepath;

    engine::Scene cpu_data;

    uint64_t timeline;
    engine::GPUGroup gpu_group;
    engine::GPUScene gpu_data;

    glm::vec3 translate{0.0f};
    glm::vec3 rotate{0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 transform = glm::identity<glm::mat4>();

    void load(const char* path, VkBufferMemoryBarrier2* indirect_buffer_barrier,
              VkBufferMemoryBarrier2* model_barrier) {
        uint32_t file_size;
        auto file = engine::util::read_file(path, &file_size);

        engine::Scene::load(&cpu_data, file, file_size);

        timeline = engine::transport::begin();
        engine::gpu_group::begin();
        engine::GPUScene::upload(&gpu_data, &cpu_data, indirect_buffer_barrier);
        gpu_group = engine::gpu_group::end(model_barrier, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
        engine::transport::end();

        free(file);
    }

    void destroy() {
        cpu_data.destroy();
        gpu_group.destroy();
        gpu_data.destroy();
    }
};

int main(int argc, char** argv) {
    uint32_t frames_in_flight = engine::get_frames_in_flight();

    std::vector<Model> models{};
    std::vector<std::vector<Model>> models_to_destroy{};
    models_to_destroy.resize(frames_in_flight);

    std::vector<Scene> scenes{};

    engine::init("Goliath editor", 1000, false);
    NFD_Init();

    VkImageMemoryBarrier2* visbuffer_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * frames_in_flight);
    engine::visbuffer::init(visbuffer_barriers);
    for (std::size_t i = 0; i < frames_in_flight; i++) {
        visbuffer_barriers[i].srcAccessMask = 0;
        visbuffer_barriers[i].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        visbuffer_barriers[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        visbuffer_barriers[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    bool visbuffer_barriers_applied = false;

    uint32_t model_vertex_spv_size;
    auto model_vertex_spv_data = engine::util::read_file("model_vertex.spv", &model_vertex_spv_size);
    auto model_vertex_module = engine::create_shader({model_vertex_spv_data, model_vertex_spv_size});
    free(model_vertex_spv_data);

    uint32_t model_fragment_spv_size;
    auto model_fragment_spv_data = engine::util::read_file("model_fragment.spv", &model_fragment_spv_size);
    auto model_fragment_module = engine::create_shader({model_fragment_spv_data, model_fragment_spv_size});
    free(model_fragment_spv_data);

    auto model_pipeline = engine::GraphicsPipeline(engine::GraphicsPipelineBuilder{}
                                                       .vertex(model_vertex_module)
                                                       .fragment(model_fragment_module)
                                                       .push_constant_size(ModelPushConstant::size)
                                                       .add_color_attachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                                                       .add_color_attachment(VK_FORMAT_R32G32B32A32_UINT)
                                                       .depth_format(VK_FORMAT_D16_UNORM))
                              .depth_test(true)
                              .depth_write(true)
                              .depth_compare_op(engine::CompareOp::Less)
                              .cull_mode(engine::CullMode::NoCull);

    uint32_t scene_vertex_spv_size;
    auto scene_vertex_spv_data = engine::util::read_file("scene_vertex.spv", &scene_vertex_spv_size);
    auto scene_vertex_module = engine::create_shader({scene_vertex_spv_data, scene_vertex_spv_size});
    free(scene_vertex_spv_data);

    uint32_t scene_fragment_spv_size;
    auto scene_fragment_spv_data = engine::util::read_file("scene_fragment.spv", &scene_fragment_spv_size);
    auto scene_fragment_module = engine::create_shader({scene_fragment_spv_data, scene_fragment_spv_size});
    free(scene_fragment_spv_data);

    auto scene_pipeline = engine::GraphicsPipeline(engine::GraphicsPipelineBuilder{}
                                                       .vertex(scene_vertex_module)
                                                       .fragment(scene_fragment_module)
                                                       .push_constant_size(ScenePushConstant::size)
                                                       .add_color_attachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                                                       .add_color_attachment(VK_FORMAT_R32G32B32A32_UINT)
                                                       .depth_format(VK_FORMAT_D16_UNORM))
                              .depth_test(true)
                              .depth_write(true)
                              .depth_compare_op(engine::CompareOp::Less)
                              .cull_mode(engine::CullMode::NoCull);

    VkDescriptorSetLayoutBinding pp_bindings[2]{};
    pp_bindings[0].binding = 0;
    pp_bindings[0].descriptorCount = 1;
    pp_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pp_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pp_bindings[1].binding = 1;
    pp_bindings[1].descriptorCount = 1;
    pp_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pp_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo pp_set_layout_info{};
    pp_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    pp_set_layout_info.bindingCount = 2;
    pp_set_layout_info.pBindings = pp_bindings;

    VkDescriptorSetLayout postprocessing_set_layout;
    vkCreateDescriptorSetLayout(engine::device, &pp_set_layout_info, nullptr, &postprocessing_set_layout);

    uint32_t postprocessing_spv_size;
    auto postprocessing_spv_data = engine::util::read_file("postprocessing.spv", &postprocessing_spv_size);
    auto postprocessing_module = engine::create_shader({postprocessing_spv_data, postprocessing_spv_size});
    free(postprocessing_spv_data);

    auto postprocessing_pipeline = engine::ComputePipeline(
        engine::ComputePipelineBuilder{}.shader(postprocessing_module).descriptor_layout(0, postprocessing_set_layout));

    // uint32_t debug_visbuffer_spv_size;
    // auto debug_visbuffer_spv_data = engine::util::read_file("debug_visbuffer.spv", &debug_visbuffer_spv_size);
    // auto debug_visbuffer_module = engine::create_shader({debug_visbuffer_spv_data, debug_visbuffer_spv_size});
    // free(debug_visbuffer_spv_data);
    //
    // auto debug_visbuffer_pipeline = engine::ComputePipeline(engine::ComputePipelineBuilder{});

    engine::GPUImage* depth_images = (engine::GPUImage*)malloc(sizeof(engine::GPUImage) * frames_in_flight);
    VkImageView* depth_image_views = (VkImageView*)malloc(sizeof(VkImageView) * frames_in_flight);
    bool depth_barriers_applied = false;
    VkImageMemoryBarrier2* depth_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * frames_in_flight);

    update_depth(depth_images, depth_image_views, depth_barriers, frames_in_flight);

    engine::GPUImage* target_images = (engine::GPUImage*)malloc(sizeof(engine::GPUImage) * frames_in_flight);
    VkImageView* target_image_views = (VkImageView*)malloc(sizeof(VkImageView) * frames_in_flight);
    bool target_barriers_applied = false;
    VkImageMemoryBarrier2* target_barriers =
        (VkImageMemoryBarrier2*)malloc(sizeof(VkImageMemoryBarrier2) * frames_in_flight);

    update_target(target_images, target_image_views, target_barriers, frames_in_flight);

    VkBufferMemoryBarrier2 model_barrier{};
    VkBufferMemoryBarrier2 scene_indirect_buffer_barrier{};
    bool model_barrier_applied = true;
    bool scene_indirect_buffer_barrier_applied = true;

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
            engine::imgui::begin();
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Load model")) {
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
                                uint32_t size;
                                auto* file = engine::util::read_file(path, &size);
                                std::filesystem::path file_path{path};

                                auto extension = file_path.extension();
                                model_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
                                model_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
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
                                models.back().name = file_path.stem();
                                models.back().filepath = std::move(file_path);
                                models.back().timeline = timeline_value;

                                auto& model = models.back();
                                auto err = model.load(type, file, size, &model_barrier);
                                if (err != engine::Model::Ok) {
                                    printf("error: %d @%d in %s\n", err, __LINE__, __FILE__);
                                    assert(false);
                                }

                                free(file);
                                NFD_PathSet_FreePath(path);
                            }
                            engine::transport::end();

                            NFD_PathSet_FreeEnum(&enumerator);
                            NFD_PathSet_Free(paths);
                        }
                    }

                    if (ImGui::MenuItem("Open scene")) {
                        nfdu8filteritem_t filters[1] = {
                            {"Scene file", "gos"},
                        };

                        auto current_path = std::filesystem::current_path();
                        nfdopendialogu8args_t args{};
                        args.filterCount = 1;
                        args.filterList = filters;
                        args.defaultPath = current_path.c_str();
                        NFD_GetNativeWindowFromGLFWWindow(engine::window, &args.parentWindow);

                        nfdu8char_t* path;
                        auto res = NFD_OpenDialogU8_With(&path, &args);
                        if (res == NFD_OKAY) {
                            scenes.emplace_back();

                            model_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
                            model_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                            scene_indirect_buffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                            scene_indirect_buffer_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                            scenes.back().load(path, &scene_indirect_buffer_barrier, &model_barrier);
                            std::filesystem::path path_fs = path;
                            scenes.back().name = path_fs.stem();
                            scenes.back().filepath = std::move(path_fs);

                            model_barrier_applied = false;
                            scene_indirect_buffer_barrier_applied = false;

                            NFD_FreePath(path);
                        }
                    }

                    if (ImGui::MenuItem("Save scene")) {
                        nfdu8filteritem_t filters[1] = {
                            {"Scene file", "gos"},
                        };

                        auto current_path = std::filesystem::current_path();
                        nfdsavedialogu8args_t args{};
                        args.defaultName = "scene.gos";
                        args.filterCount = 1;
                        args.filterList = filters;
                        args.defaultPath = current_path.c_str();
                        NFD_GetNativeWindowFromGLFWWindow(engine::window, &args.parentWindow);

                        nfdu8char_t* path;
                        auto res = NFD_SaveDialogU8_With(&path, &args);
                        if (res == NFD_OKAY) {
                            uint32_t data_size;
                            auto data = serialize_scene(models.data(), models.size(), &data_size);
                            engine::util::save_file(path, (uint8_t*)data, data_size);

                            NFD_FreePath(path);
                            free(data);
                        }
                    }

                    ImGui::EndMenu();
                }
            }
            ImGui::EndMainMenuBar();

            if (ImGui::Begin("Models")) {
                std::erase_if(models, [&](auto& model) {
                    ImGui::PushID(model.gpu_group.data.address());

                    bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_AllowItemOverlap |
                                                                ImGuiTreeNodeFlags_SpanLabelWidth);

                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    ImGui::InputText("##name", &model.name);

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    if (ImGui::Button("Unload")) {
                        models_to_destroy[(engine::get_current_frame() - 1) % frames_in_flight].emplace_back(model);

                        if (open) ImGui::TreePop();
                        ImGui::PopID();

                        return true;
                    }

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    ImGui::Checkbox("Embed", &model.embed_optimized);

                    if (open) {
                        ImGui::Text("file path: %s", model.filepath.c_str());

                        if (ImGui::Button("add instance")) {
                            model.instances.emplace_back();
                            model.instances.back().name = std::format("Instance #{}", model.last_instance_id++);
                        }

                        model.instance_transforms.clear();
                        model.instance_transforms.resize(model.instances.size());
                        std::size_t i = 0;
                        std::size_t instance_counter = 0;
                        std::erase_if(model.instances, [&](auto& instance) {
                            ImGui::PushID(i);
                            auto remove =
                                imgui_model_instance(model.instances[i], model.instance_transforms[instance_counter]);
                            ImGui::PopID();

                            if (!remove) instance_counter++;

                            i++;
                            return remove;
                        });

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                    return false;
                });
            }
            ImGui::End();

            if (ImGui::Begin("Scenes")) {
                std::erase_if(scenes, [&](auto& scene) {
                    ImGui::PushID(scene.gpu_group.data.address());

                    bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_AllowItemOverlap |
                                                                ImGuiTreeNodeFlags_SpanLabelWidth);

                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    ImGui::InputText("##name", &scene.name);

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                    if (ImGui::Button("Unload")) {
                        scene.destroy();

                        if (open) ImGui::TreePop();
                        ImGui::PopID();
                        return true;
                    }

                    if (open) {
                        ImGui::DragFloat3("XYZ", glm::value_ptr(scene.translate), 0.1f, 0.0f, 0.0f, "%.2f");

                        ImGui::DragFloat("yaw", &scene.rotate.x, 0.1f, 0.0, 0.0, "%.2f");
                        ImGui::DragFloat("pitch", &scene.rotate.y, 0.1f, 0.0, 0.0, "%.2f");
                        ImGui::DragFloat("roll", &scene.rotate.z, 0.1f, 0.0, 0.0, "%.2f");

                        ImGui::DragFloat3("scale", glm::value_ptr(scene.scale), 0.1f, 0.0f, 0.0f, "%.2f");

                        scene.transform = glm::translate(glm::identity<glm::mat4>(), scene.translate) *
                                          glm::rotate(glm::rotate(glm::rotate(glm::identity<glm::mat4>(),
                                                                              scene.rotate.x, glm::vec3{0, 1, 0}),
                                                                  scene.rotate.y, glm::vec3{1, 0, 0}),
                                                      scene.rotate.z, glm::vec3{0, 0, 1}) *
                                          glm::scale(glm::identity<glm::mat4>(), scene.scale);

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                    return false;
                });
            }
            ImGui::End();

            if (ImGui::Begin("Window")) {
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
            }

            ImGui::End();
            engine::imgui::end();

            engine::prepare_draw();

            if (!visbuffer_barriers_applied || !depth_barriers_applied || !target_barriers_applied ||
                !model_barrier_applied || !scene_indirect_buffer_barrier_applied) {
                engine::synchronization::begin_barriers();
                if (!visbuffer_barriers_applied) {
                    for (std::size_t i = 0; i < frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(visbuffer_barriers[i]);
                    }

                    visbuffer_barriers_applied = true;
                }
                if (!depth_barriers_applied) {
                    for (std::size_t i = 0; i < frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(depth_barriers[i]);
                    }

                    depth_barriers_applied = true;
                }
                if (!target_barriers_applied) {
                    for (std::size_t i = 0; i < frames_in_flight; i++) {
                        engine::synchronization::apply_barrier(target_barriers[i]);
                    }

                    depth_barriers_applied = true;
                }
                if (!model_barrier_applied) {
                    engine::synchronization::apply_barrier(model_barrier);

                    model_barrier_applied = true;
                }
                if (!scene_indirect_buffer_barrier_applied) {
                    engine::synchronization::apply_barrier(scene_indirect_buffer_barrier);

                    scene_indirect_buffer_barrier_applied = true;
                }
                engine::synchronization::end_barriers();
            }

            engine::rendering::begin(
                engine::RenderPass{}
                    .add_color_attachment(engine::RenderingAttachement{}
                                              .set_image(target_image_views[engine::get_current_frame()],
                                                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                                              .set_clear_color(glm::vec4{0.0f, 0.0f, 0.3f, 1.0f})
                                              .set_load_op(engine::LoadOp::Clear)
                                              .set_store_op(engine::StoreOp::Store))
                    .add_color_attachment(
                        engine::visbuffer::attach().set_load_op(engine::LoadOp::Clear).set_clear_color(glm::vec4{0.0f}))
                    .depth_attachment(engine::RenderingAttachement{}
                                          .set_image(depth_image_views[engine::get_current_frame()],
                                                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                          .set_clear_depth(1.0f)
                                          .set_load_op(engine::LoadOp::Clear)
                                          .set_store_op(engine::StoreOp::Store)));
            model_pipeline.bind();
            uint8_t model_push_constant[ModelPushConstant::size]{};

            for (auto& model : models) {
                if (!engine::transport::is_ready(model.timeline)) continue;

                for (const auto& transform : model.instance_transforms) {
                    ModelPushConstant::write(model_push_constant, model.gpu_group.data.address(),
                                             model.indirect_draw_buffer.address(), cam.view_projection(), transform);

                    model_pipeline.draw_indirect(engine::GraphicsPipeline::DrawIndirectParams{
                        .push_constant = model_push_constant,
                        .draw_buffer = model.indirect_draw_buffer.data(),
                        .draw_count = model.gpu_data.mesh_count,
                        .stride = sizeof(VkDrawIndirectCommand) + sizeof(uint32_t),
                    });
                }
            }

            uint8_t scene_push_constant[ScenePushConstant::size];
            scene_pipeline.bind();
            for (auto& scene : scenes) {
                if (!engine::transport::is_ready(scene.timeline)) continue;

                ScenePushConstant::write(scene_push_constant, scene.gpu_group.data.address(),
                                         scene.gpu_data.draw_indirect.address(), cam.view_projection(),
                                         scene.transform);

                scene_pipeline.draw_indirect(engine::GraphicsPipeline::DrawIndirectParams{
                    .push_constant = scene_push_constant,
                    .draw_buffer = scene.gpu_data.draw_indirect.data(),
                    .draw_count = scene.gpu_data.draw_count,
                    .stride = sizeof(engine::GPUScene::DrawCommand),
                });
            }
            engine::rendering::end();

            VkImageMemoryBarrier2 target_barrier{};
            target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            target_barrier.pNext = nullptr;
            target_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
            target_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            VkImageMemoryBarrier2 visbuffer_barrier{};
            engine::visbuffer::barrier(&visbuffer_barrier);
            visbuffer_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            visbuffer_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            visbuffer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            visbuffer_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            visbuffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(visbuffer_barrier);
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::end_barriers();

            auto pp_set = engine::descriptor::new_set(postprocessing_set_layout);
            engine::descriptor::begin_update(pp_set);
            engine::descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL,
                                                     target_image_views[engine::get_current_frame()]);
            engine::descriptor::update_storage_image(1, VK_IMAGE_LAYOUT_GENERAL,
                                                     engine::visbuffer::get_view());
            engine::descriptor::end_update();

            postprocessing_pipeline.bind();
            postprocessing_pipeline.dispatch(engine::ComputePipeline::DispatchParams{
                .descriptor_indexes =
                    {
                        pp_set,
                        engine::descriptor::null_set,
                        engine::descriptor::null_set,
                        engine::descriptor::null_set,
                    },
                .group_count_x = (uint32_t)std::ceil(engine::swapchain_extent.width / 16.0f),
                .group_count_y = (uint32_t)std::ceil(engine::swapchain_extent.width / 16.0f),
                .group_count_z = 1,
            });

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_NONE;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            VkImageMemoryBarrier2 swapchain_barrier{};
            swapchain_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            swapchain_barrier.pNext = nullptr;
            swapchain_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            swapchain_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            swapchain_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            swapchain_barrier.image = engine::get_swapchain();
            swapchain_barrier.subresourceRange = VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            swapchain_barrier.srcAccessMask = VK_ACCESS_2_NONE;
            swapchain_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            swapchain_barrier.dstAccessMask = VK_ACCESS_2_NONE;
            swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::apply_barrier(swapchain_barrier);
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
            blit_region.dstOffsets[0] = VkOffset3D{
                .x = 0,
                .y = 0,
                .z = 0,
            };
            blit_region.dstOffsets[1] = VkOffset3D{
                .x = (int32_t)engine::swapchain_extent.width,
                .y = (int32_t)engine::swapchain_extent.height,
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
            blit_info.dstImage = engine::get_swapchain();
            blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blit_info.filter = VK_FILTER_NEAREST;
            blit_info.regionCount = 1;
            blit_info.pRegions = &blit_region;
            vkCmdBlitImage2(engine::get_cmd_buf(), &blit_info);

            swapchain_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            swapchain_barrier.srcAccessMask = VK_ACCESS_2_NONE;
            swapchain_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            swapchain_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(swapchain_barrier);
            engine::synchronization::end_barriers();

            engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
                engine::RenderingAttachement{}
                    .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                    .set_load_op(engine::LoadOp::Load)
                    .set_store_op(engine::StoreOp::Store)));
            engine::imgui::render();
            engine::rendering::end();

            target_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            target_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            target_barrier.srcAccessMask = VK_ACCESS_2_NONE;
            target_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            target_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            visbuffer_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            visbuffer_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            visbuffer_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            visbuffer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            visbuffer_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            visbuffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(target_barrier);
            engine::synchronization::apply_barrier(visbuffer_barrier);
            engine::synchronization::end_barriers();
        }

    end_of_frame:
        for (auto& model : models_to_destroy[engine::get_current_frame()]) {
            model.destroy();
        }
        models_to_destroy[engine::get_current_frame()].clear();

        if (engine::next_frame()) {
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                engine::GPUImageView::destroy(depth_image_views[i]);
                depth_images[i].destroy();

                engine::GPUImageView::destroy(target_image_views[i]);
                target_images[i].destroy();
            }

            update_depth(depth_images, depth_image_views, depth_barriers, frames_in_flight);
            depth_barriers_applied = false;

            update_target(target_images, target_image_views, target_barriers, frames_in_flight);
            target_barriers_applied = false;

            model_pipeline.update_viewport_to_swapchain();
            model_pipeline.update_scissor_to_viewport();

            engine::visbuffer::destroy();
            engine::visbuffer::init(visbuffer_barriers);
            for (std::size_t i = 0; i < frames_in_flight; i++) {
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

    for (auto& model : models) {
        model.destroy();
    }
    for (auto& models : models_to_destroy) {
        for (auto& model : models) {
            model.destroy();
        }
    }

    for (auto& scene : scenes) {
        scene.destroy();
    }

    model_pipeline.destroy();
    engine::destroy_shader(model_vertex_module);
    engine::destroy_shader(model_fragment_module);

    scene_pipeline.destroy();
    engine::destroy_shader(scene_vertex_module);
    engine::destroy_shader(scene_fragment_module);

    vkDestroyDescriptorSetLayout(engine::device, postprocessing_set_layout, nullptr);
    postprocessing_pipeline.destroy();
    engine::destroy_shader(postprocessing_module);

    // debug_visbuffer_pipeline.destroy();
    // engine::destroy_shader(debug_visbuffer_module);

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        engine::GPUImageView::destroy(depth_image_views[i]);
        depth_images[i].destroy();

        engine::GPUImageView::destroy(target_image_views[i]);
        target_images[i].destroy();
    }

    free(target_images);
    free(target_image_views);
    free(target_barriers);

    engine::visbuffer::destroy();
    engine::destroy();
    return 0;
}
