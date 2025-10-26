#include "goliath/buffer.hpp"
#include "goliath/camera.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/imgui.hpp"
#include "goliath/model.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/rendering.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/transport.hpp"
#include "goliath/util.hpp"
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
    int width, height;
    glfwGetFramebufferSize(engine::window, &width, &height);

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        auto [depth_img, barrier] = engine::GPUImage::upload(engine::GPUImageInfo{}
                                                                 .image_type(VK_IMAGE_TYPE_2D)
                                                                 .format(VK_FORMAT_D16_UNORM)
                                                                 .width((uint32_t)width)
                                                                 .height((uint32_t)height)
                                                                 .layer_count(1)
                                                                 .extent(VkExtent3D{
                                                                     .width = (uint32_t)width,
                                                                     .height = (uint32_t)height,
                                                                     .depth = 1,
                                                                 })
                                                                 .aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT)
                                                                 .new_layout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                                                 .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                                                                 .size((uint32_t)(width * height * 2)));

        barrier.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        images[i] = depth_img;
        barriers[i] = barrier;
        image_views[i] = engine::GPUImageView{images[i]}
                             .view_type(VK_IMAGE_VIEW_TYPE_2D)
                             .aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT)
                             .create();
    }
}

using ModelPushConstant = engine::PushConstant<uint64_t, uint64_t, glm::mat4, glm::mat4>;

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

        engine::model::begin_gpu_upload();
        auto [_gpu_data, _indirect_draw_buffer] = engine::model::upload(&cpu_data);
        gpu_data = _gpu_data;
        indirect_draw_buffer = _indirect_draw_buffer;
        gpu_group = engine::model::end_gpu_upload(barrier);

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

    transform =
        glm::translate(glm::identity<glm::mat4>(), instance.translate) *
        glm::rotate(glm::rotate(glm::rotate(glm::identity<glm::mat4>(), instance.rotate.x, glm::vec3{0, 1, 0}),
                                instance.rotate.y, glm::vec3{1, 0, 0}),
                    instance.rotate.z, glm::vec3{0, 0, 1}) *
        glm::scale(glm::identity<glm::mat4>(), instance.scale);

    return false;
}

int main(int argc, char** argv) {
    uint32_t frames_in_flight = engine::get_frames_in_flight();

    std::vector<Model> models{};
    std::vector<std::vector<Model>> models_to_destroy{};
    models_to_destroy.resize(frames_in_flight);

    engine::init("Goliath editor", 1000, false);
    NFD_Init();

    uint32_t model_vertex_spv_size;
    auto model_vertex_spv_data = engine::util::read_file("model_vertex.spv", &model_vertex_spv_size);
    auto model_vertex_module = engine::create_shader({model_vertex_spv_data, model_vertex_spv_size});
    free(model_vertex_spv_data);

    uint32_t model_fragment_spv_size;
    auto model_fragment_spv_data = engine::util::read_file("model_fragment.spv", &model_fragment_spv_size);
    auto model_fragment_module = engine::create_shader({model_fragment_spv_data, model_fragment_spv_size});
    free(model_fragment_spv_data);

    auto model_pipeline = engine::Pipeline(engine::PipelineBuilder{}
                                               .vertex(model_vertex_module)
                                               .fragment(model_fragment_module)
                                               .push_constant_size(ModelPushConstant::size)
                                               .add_color_attachment(engine::swapchain_format)
                                               .depth_format(VK_FORMAT_D16_UNORM))
                              .depth_test(true)
                              .depth_write(true)
                              .depth_compare_op(engine::CompareOp::Less)
                              .cull_mode(engine::CullMode::NoCull);
    engine::GPUImage* depth_images = new engine::GPUImage[frames_in_flight];
    VkImageView* depth_image_views = new VkImageView[frames_in_flight];
    bool depth_barriers_applied = false;
    VkImageMemoryBarrier2* depth_barriers = new VkImageMemoryBarrier2[frames_in_flight];

    update_depth(depth_images, depth_image_views, depth_barriers, frames_in_flight);

    VkBufferMemoryBarrier2 model_barrier{};
    bool model_barrier_applied = true;

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

        engine::imgui::begin();
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Load model")) {
                    nfdu8filteritem_t filters[1] = {
                        {"Model files", "gltf,glb,gom"},
                    };
                    nfdopendialogu8args_t args{};
                    args.filterCount = 1;
                    args.filterList = filters;
                    NFD_GetNativeWindowFromGLFWWindow(engine::window, &args.parentWindow);

                    // nfdu8char_t* path;
                    // auto res = NFD_OpenDialogU8_With(&path, &args);

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
                    } else if (res != NFD_CANCEL) {
                        printf("NFD error: %s\n", NFD_GetError());
                        assert(false);
                    }
                }

                if (ImGui::MenuItem("Open scene")) {
                    printf("clicked open\n");
                }

                if (ImGui::MenuItem("Save scene")) {
                    printf("clicked save\n");
                }

                ImGui::EndMenu();
            }
        }
        ImGui::EndMainMenuBar();

        if (ImGui::Begin("Models")) {
            std::erase_if(models, [&](auto& model) {
                ImGui::PushID(model.gpu_group.vertex_data.address());

                bool open = ImGui::TreeNodeEx("##node",
                                              ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanLabelWidth);

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

        if (!depth_barriers_applied || !model_barrier_applied) {
            engine::synchronization::begin_barriers();
            if (!depth_barriers_applied) {
                for (std::size_t i = 0; i < frames_in_flight; i++) {
                    engine::synchronization::apply_barrier(depth_barriers[i]);
                }

                depth_barriers_applied = true;
            }
            if (!model_barrier_applied) {
                engine::synchronization::apply_barrier(model_barrier);

                model_barrier_applied = true;
            }
            engine::synchronization::end_barriers();
        }

        {
            engine::rendering::begin(
                engine::RenderPass{}
                    .add_color_attachment(
                        engine::RenderingAttachement{}
                            .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                            .set_clear_color(glm::vec4{0.0f, 0.0f, 0.3f, 1.0f})
                            .set_load_op(engine::LoadOp::Clear)
                            .set_store_op(engine::StoreOp::Store))
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
                    ModelPushConstant::write(model_push_constant, model.gpu_group.vertex_data.address(),
                                             model.indirect_draw_buffer.address(), cam.view_projection(), transform);

                    model_pipeline.draw_indirect(engine::Pipeline::DrawIndirectParams{
                        .push_constant = model_push_constant,
                        .draw_buffer = model.indirect_draw_buffer.data(),
                        .draw_count = model.gpu_data.second - model.gpu_data.first,
                        .stride = sizeof(VkDrawIndirectCommand) + sizeof(uint32_t),
                    });
                }
            }
            engine::rendering::end();
        }

        engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
            engine::RenderingAttachement{}
                .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .set_load_op(engine::LoadOp::Load)
                .set_store_op(engine::StoreOp::Store)));
        engine::imgui::render();
        engine::rendering::end();

    end_of_frame:
        for (auto& model : models_to_destroy[engine::get_current_frame()]) {
            model.destroy();
        }
        models_to_destroy[engine::get_current_frame()].clear();

        if (engine::next_frame()) {
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                engine::GPUImageView::destroy(depth_image_views[i]);
                depth_images[i].destroy();
            }
            update_depth(depth_images, depth_image_views, depth_barriers, frames_in_flight);
            depth_barriers_applied = false;

            model_pipeline.update_viewport_to_swapchain();
            model_pipeline.update_scissor_to_viewport();
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

    model_pipeline.destroy();
    engine::destroy_shader(model_vertex_module);
    engine::destroy_shader(model_fragment_module);

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        engine::GPUImageView::destroy(depth_image_views[i]);
        depth_images[i].destroy();
    }
    delete[] depth_images;

    engine::destroy();
    return 0;
}
