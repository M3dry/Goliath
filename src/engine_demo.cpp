#include "goliath/buffer.hpp"
#include "goliath/camera.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/imgui.hpp"
#include "goliath/rendering.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/texture_pool.hpp"
#include "goliath/transport.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <volk.h>
#include <vulkan/vulkan_core.h>

int main(int argc, char** argv) {
    engine::init("Test window", 1000);

    auto vertex = engine::Shader{}.stage(VK_SHADER_STAGE_VERTEX_BIT).next_stage(VK_SHADER_STAGE_FRAGMENT_BIT);
    auto fragment = engine::Shader{}.stage(VK_SHADER_STAGE_FRAGMENT_BIT);
    auto pipeline = engine::Pipeline{}
                        .push_constant_size(sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::mat4))
                        .vertex(vertex, "vertex.spv")
                        .fragment(fragment, "fragment.spv")
                        .update_layout();

    auto img = engine::Image::load8(argv[1], 4);

    glm::vec4 vertices[6] = {
        {5.0f, 0.0f, 0.0f, 1.0f},
        {-5.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 5.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    };
    auto vertices_buffer = engine::Buffer::create(
        6 * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, std::nullopt);

    engine::transport::begin();
    auto [gpu_img, barrier] = engine::GPUImage::upload(img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

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

    glm::vec3 res_movement{ 0.0f };

    {
    auto f = cam.forward();
    auto r = cam.right();
    auto u = cam.up();
    auto det = glm::dot(glm::cross(r, u), f);
    printf("hahdedness determinant: %f\n", det);
    }

    bool lock_cam = false;
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

                res_movement = -normalized_movement.z * forward + normalized_movement.x * right;
                cam.position.x += res_movement.x;
                cam.position.y += res_movement.y;
                cam.position.z += res_movement.z;
            }
            // auto mouse_delta = engine::event::get_mouse_delta();
            // if (!lock_cam && (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)) {
            //     cam.rotate(-mouse_delta.x, -mouse_delta.y);
            // }

            cam.update_matrices();

            // engine::imgui::enable(lock_cam);
            // glfwSetInputMode(engine::window, GLFW_CURSOR, lock_cam ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);

            update_count++;
            engine::event::update_tick();
        }

        engine::prepare_frame();

        engine::imgui::begin();
        ImGui::Begin("Window");
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
        ImGui::End();
        engine::imgui::end();

        engine::prepare_draw();

        if (!barrier_applied) {
            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(barrier);
            engine::synchronization::apply_barrier(buffer_barrier);
            engine::synchronization::end_barriers();
            barrier_applied = false;
        }

        engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
            engine::RenderingAttachement{}
                .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .set_clear_color(glm::vec4{0.0f, 0.0f, 0.3f, 1.0f})
                .set_load_op(engine::LoadOp::Clear)
                .set_store_op(engine::StoreOp::Store)));

        if (engine::transport::timeline_value() >= img_timeline) {
            uint8_t push_constant[sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::mat4)];
            auto addr = vertices_buffer.address();

            std::memcpy(push_constant, &addr, sizeof(uint64_t));
            std::memcpy(push_constant + sizeof(glm::vec4), &color, sizeof(glm::vec4));
            auto cam_mat = cam.view_projection();
            std::memcpy(push_constant + 2 * sizeof(glm::vec4), &cam_mat, sizeof(glm::mat4));

            pipeline.draw(engine::Pipeline::DrawParams{
                .push_constant = push_constant,
                .vertex_count = 3,
            });
        }

        engine::imgui::render();
        engine::rendering::end();

        engine::next_frame();
    }

    vkDeviceWaitIdle(engine::device);

    vertices_buffer.destroy();

    engine::GPUImageView::destroy(gpu_img_view);
    gpu_img.destroy();
    engine::Sampler::destroy(img_sampler);
    img.destroy();

    engine::destroy_shader(pipeline._fragment);
    engine::destroy_shader(pipeline._vertex);
    pipeline.destroy({false, false, false});
    engine::destroy();
    return 0;
}
