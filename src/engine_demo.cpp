#include "goliath/buffer.hpp"
#include "goliath/camera.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/imgui.hpp"
#include "goliath/model.hpp"
#include "goliath/rendering.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/texture_pool.hpp"
#include "goliath/transport.hpp"
#include "goliath/util.hpp"
#include "imgui/imgui.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <volk.h>
#include <vulkan/vulkan_core.h>

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

int main(int argc, char** argv) {
    engine::init("Test window", 1000);

    uint32_t vertex_spv_size;
    auto vertex_spv_data = engine::util::read_file("vertex.spv", &vertex_spv_size);
    auto vertex_module = engine::create_shader({vertex_spv_data, vertex_spv_size});

    uint32_t fragment_spv_size;
    auto fragment_spv_data = engine::util::read_file("fragment.spv", &fragment_spv_size);
    auto fragment_module = engine::create_shader({fragment_spv_data, fragment_spv_size});

    auto pipeline = engine::Pipeline2(engine::PipelineBuilder{}
                                          .vertex(vertex_module)
                                          .fragment(fragment_module)
                                          .push_constant_size(sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::mat4))
                                          .add_color_attachment(engine::swapchain_format)
                                          .depth_format(VK_FORMAT_D16_UNORM))
                        .depth_test(true)
                        .depth_write(true)
                        .depth_compare_op(engine::CompareOp::Less);

    uint32_t model_vertex_spv_size;
    auto model_vertex_spv_data = engine::util::read_file("model_vertex.spv", &model_vertex_spv_size);
    auto model_vertex_module = engine::create_shader({model_vertex_spv_data, model_vertex_spv_size});

    uint32_t model_fragment_spv_size;
    auto model_fragment_spv_data = engine::util::read_file("model_fragment.spv", &model_fragment_spv_size);
    auto model_fragment_module = engine::create_shader({model_fragment_spv_data, model_fragment_spv_size});

    auto model_pipeline =
        engine::Pipeline2(engine::PipelineBuilder{}
                              .vertex(model_vertex_module)
                              .fragment(model_fragment_module)
                              .push_constant_size(sizeof(engine::model::GPUOffset) + 2 * sizeof(uint64_t) + 2*sizeof(glm::mat4))
                              .add_color_attachment(engine::swapchain_format)
                              .depth_format(VK_FORMAT_D16_UNORM))
            .depth_test(true)
            .depth_write(true)
            .depth_compare_op(engine::CompareOp::Less);

    auto img = engine::Image::load8(argv[1], 4);

    uint32_t frames_in_flight = engine::get_frames_in_flight();
    engine::GPUImage* depth_images = new engine::GPUImage[frames_in_flight];
    VkImageView* depth_image_views = new VkImageView[frames_in_flight];
    bool depth_barriers_applied = false;
    VkImageMemoryBarrier2* depth_barriers = new VkImageMemoryBarrier2[frames_in_flight];

    update_depth(depth_images, depth_image_views, depth_barriers, frames_in_flight);

    glm::vec4 vertices[6] = {
        {5.0f, 0.0f, 0.0f, 1.0f}, {-5.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 5.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f},  {0.0f, 0.0f, 1.0f, 1.0f},
    };
    auto vertices_buffer = engine::Buffer::create(
        6 * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, std::nullopt);

    uint32_t model_glb_size;
    auto model_glb_data = engine::util::read_file(argv[2], &model_glb_size);
    assert(model_glb_data != nullptr);

    engine::Model model;
    auto err = engine::Model::load_glb(&model, {model_glb_data, model_glb_size});
    if (err != engine::Model::Err::Ok) {
        printf("err: %d", err);
        return 0;
    }
    VkBufferMemoryBarrier2 model_barrier{};
    model_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    model_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;

    engine::transport::begin();
    engine::model::begin_gpu_upload();
    auto gpu_model = engine::model::upload(&model);
    auto gpu_group = engine::model::end_gpu_upload(&model_barrier);

    auto [gpu_img, img_barrier] = engine::GPUImage::upload(img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    img_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    img_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    VkBufferMemoryBarrier2 buffer_barrier{};
    buffer_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    buffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    engine::transport::upload(&buffer_barrier, vertices, (uint32_t)vertices_buffer.size(), vertices_buffer);

    auto img_timeline = engine::transport::end();
    bool barrier_applied = false;

    auto gpu_img_view = engine::GPUImageView{gpu_img}.create();
    auto img_sampler = engine::Sampler{}.create();

    engine::texture_pool::update(0, gpu_img_view, gpu_img.layout, img_sampler);

    glm::vec4 color{0.0f, 0.2f, 0.0f, 1.0f};

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
                if (ImGui::MenuItem("Open")) {
                    printf("clicked open\n");
                }

                ImGui::EndMenu();
            }
        }
        ImGui::EndMainMenuBar();

        if (ImGui::Begin("Window")) {
            ImGui::ColorPicker4("triangle color", glm::value_ptr(color));

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

        if (!barrier_applied || !depth_barriers_applied) {
            engine::synchronization::begin_barriers();
            if (!barrier_applied) {
                engine::synchronization::apply_barrier(img_barrier);
                engine::synchronization::apply_barrier(buffer_barrier);
            }
            if (!depth_barriers_applied) {
                for (std::size_t i = 0; i < frames_in_flight; i++) {
                    engine::synchronization::apply_barrier(depth_barriers[i]);
                }
            }
            engine::synchronization::end_barriers();
            barrier_applied = false;
        }

        engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
            engine::RenderingAttachement{}
                .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .set_clear_color(glm::vec4{0.0f, 0.0f, 0.3f, 1.0f})
                .set_load_op(engine::LoadOp::Clear)
                .set_store_op(engine::StoreOp::Store)));
        engine::imgui::render();
        engine::rendering::end();

        engine::rendering::begin(engine::RenderPass{}
                                     .add_color_attachment(engine::RenderingAttachement{}
                                                               .set_image(engine::get_swapchain_view(),
                                                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                                                               .set_load_op(engine::LoadOp::Load)
                                                               .set_store_op(engine::StoreOp::Store))
                                     .depth_attachment(engine::RenderingAttachement{}
                                                           .set_image(depth_image_views[engine::get_current_frame()],
                                                                      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                                           .set_clear_depth(1.0f)
                                                           .set_load_op(engine::LoadOp::Clear)
                                                           .set_store_op(engine::StoreOp::Store)));
        if (engine::transport::timeline_value() >= img_timeline) {
            uint8_t push_constant[sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::mat4)];
            auto addr = vertices_buffer.address();

            std::memcpy(push_constant, &addr, sizeof(uint64_t));
            std::memcpy(push_constant + sizeof(glm::vec4), &color, sizeof(glm::vec4));
            auto cam_mat = cam.view_projection();
            std::memcpy(push_constant + 2 * sizeof(glm::vec4), &cam_mat, sizeof(glm::mat4));

            pipeline.bind();
            pipeline.draw(engine::Pipeline2::DrawParams{
                .push_constant = push_constant,
                .vertex_count = 3,
            });

            model_pipeline.bind();
            engine::model::draw(gpu_group, gpu_model,
                                [&](uint32_t vertex_count, engine::material_id id,
                                    const engine::model::GPUOffset& offset, glm::mat4 transform, engine::Buffer buf) {
                                    uint8_t push_constant[sizeof(engine::model::GPUOffset) + 2 * sizeof(uint64_t) + 2*sizeof(glm::mat4)]{};

                                    auto addr = buf.address();
                                    std::memcpy(push_constant, &addr, 2 * sizeof(uint64_t));

                                    std::memcpy(push_constant + sizeof(uint64_t), &transform, sizeof(glm::mat4));

                                    auto vp = cam.view_projection();
                                    std::memcpy(push_constant + sizeof(uint64_t) + sizeof(glm::mat4), &vp, sizeof(glm::mat4));

                                    std::memcpy(push_constant + sizeof(uint64_t) + 2*sizeof(glm::mat4), &offset, sizeof(engine::model::GPUOffset));

                                    model_pipeline.draw(engine::Pipeline2::DrawParams{
                                        .push_constant = push_constant,
                                        .vertex_count = vertex_count,
                                    });
                                });
        }
        engine::rendering::end();

    end_of_frame:
        if (engine::next_frame()) {
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                engine::GPUImageView::destroy(depth_image_views[i]);
                depth_images[i].destroy();
            }
            update_depth(depth_images, depth_image_views, depth_barriers, frames_in_flight);
            depth_barriers_applied = false;

            pipeline.update_viewport_to_swapchain();
            pipeline.update_scissor_to_viewport();
        }
    }

    gpu_group.destroy();
    model.destroy();

    vkDeviceWaitIdle(engine::device);

    pipeline.destroy();
    engine::destroy_shader(vertex_module);
    engine::destroy_shader(fragment_module);

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        engine::GPUImageView::destroy(depth_image_views[i]);
        depth_images[i].destroy();
    }
    delete[] depth_images;

    vertices_buffer.destroy();

    engine::GPUImageView::destroy(gpu_img_view);
    gpu_img.destroy();
    engine::Sampler::destroy(img_sampler);
    img.destroy();

    engine::destroy();
    return 0;
}
