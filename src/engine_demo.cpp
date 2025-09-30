#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/imgui.hpp"
#include "goliath/rendering.hpp"
#include "imgui/imgui.h"
#include <GLFW/glfw3.h>
#include <volk.h>

int main() {
    engine::init("Test window", 1000);

    auto vertex = engine::Shader{}.stage(VK_SHADER_STAGE_VERTEX_BIT).next_stage(VK_SHADER_STAGE_FRAGMENT_BIT);
    auto fragment = engine::Shader{}.stage(VK_SHADER_STAGE_FRAGMENT_BIT);
    auto pipeline = engine::Pipeline{}
                        .vertex(vertex, "vertex.spv")
                        .fragment(fragment, "fragment.spv")
                        .push_constant_size(0)
                        .update_layout();

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
        ImGui::End();
        engine::imgui::end();

        engine::prepare_draw();

        engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
            engine::RenderingAttachement{}
                .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .set_clear_color(glm::vec4{0.0f, 0.0f, 0.3f, 1.0f})
                .set_load_op(engine::LoadOp::Clear)
                .set_store_op(engine::StoreOp::Store)));

        engine::imgui::render();

        pipeline.draw(nullptr, 3);
        engine::rendering::end();

        engine::next_frame();
    }

    engine::destroy_shader(pipeline._fragment);
    engine::destroy_shader(pipeline._vertex);
    pipeline.destroy({false, false, false});
    engine::destroy();
    return 0;
}
