#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/imgui.hpp"
#include "goliath/rendering.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/texture_pool.hpp"
#include "goliath/transport.hpp"
#include "imgui/imgui.h"
#include <GLFW/glfw3.h>
#include <volk.h>

int main(int argc, char** argv) {
    engine::init("Test window", 1000);

    auto vertex = engine::Shader{}.stage(VK_SHADER_STAGE_VERTEX_BIT).next_stage(VK_SHADER_STAGE_FRAGMENT_BIT);
    auto fragment = engine::Shader{}.stage(VK_SHADER_STAGE_FRAGMENT_BIT);
    auto pipeline = engine::Pipeline{}
                        .push_constant_size(sizeof(glm::vec4))
                        .vertex(vertex, "vertex.spv")
                        .fragment(fragment, "fragment.spv")
                        .update_layout();

    auto img = engine::Image::load8(argv[1], 4);

    engine::transport::begin();
    auto [gpu_img, barrier] = engine::GPUImage::upload(img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    auto img_timeline = engine::transport::end();
    bool barrier_applied = false;

    auto gpu_img_view = engine::GPUImageView{gpu_img}.create();
    auto img_sampler = engine::Sampler{}.create();

    engine::texture_pool::update(0, gpu_img_view, gpu_img.layout, img_sampler);

    glm::vec4 color{0.0f, 0.2f, 0.0f, 1.0f};

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

            update_count++;
            engine::event::update_tick();
        }

        engine::prepare_frame();

        engine::imgui::begin();
        ImGui::Begin("Window");
        ImGui::ColorPicker4("triangle color", glm::value_ptr(color));
        ImGui::End();
        engine::imgui::end();

        engine::prepare_draw();

        if (!barrier_applied) {
            engine::synchronization::begin_barriers();
            engine::synchronization::apply_barrier(barrier);
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

        if (engine::transport::timeline_value() >= img_timeline) {
            pipeline.draw(engine::Pipeline::DrawParams{
                .push_constant = &color,
                .vertex_count = 3,
            });
        }
        engine::rendering::end();

        engine::next_frame();
    }

    vkDeviceWaitIdle(engine::device);

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
