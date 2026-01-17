#include "goliath/buffer.hpp"
#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/rendering.hpp"
#include "goliath/samplers.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/transport.hpp"
#include "goliath/visbuffer.hpp"
#include <GLFW/glfw3.h>
#include <cctype>
#include <cstring>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <volk.h>
#include <vulkan/vulkan_core.h>

struct Metadata {
    uint32_t width;
    uint32_t height;
    VkFormat format;
};

std::pair<std::span<uint8_t>, Metadata> read_goi(std::filesystem::path path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    auto size_signed = file.tellg();
    if (size_signed < 0) {
        assert(false && "texture somehow isn't on disk");
    }

    Metadata metadata{};
    size_t image_size = (uint32_t)size_signed - sizeof(Metadata);
    uint8_t* image_data = (uint8_t*)malloc(image_size);

    file.seekg(0, std::ios::beg);
    file.read((char*)&metadata, sizeof(Metadata));
    file.read((char*)image_data, image_size);

    return {{image_data, image_size}, metadata};
}

using ImagePC = engine::PushConstant<glm::uvec2, glm::uvec2>;

void rebuild(engine::GraphicsPipeline& image_pipeline) {
    image_pipeline.update_viewport_to_swapchain();
    image_pipeline.update_scissor_to_viewport();
}

int main(int argc, char** argv) {
    engine::init(engine::Init{
            .window_name = "GoiView",
            .texture_capacity = 0,
            .fullscreen = false,
    });
    glfwSetWindowAttrib(engine::window, GLFW_DECORATED, GLFW_TRUE);
    glfwSetWindowAttrib(engine::window, GLFW_RESIZABLE, GLFW_TRUE);
    glfwSetWindowAttrib(engine::window, GLFW_AUTO_ICONIFY, GLFW_TRUE);

    if (argc != 2) {
        printf("USAGE: %s [file].goi\n", argv[0]);
        return 0;
    }

    if (!std::filesystem::exists(argv[1])) {
        printf("File `%s` doesn't exist\n", argv[1]);
        return -1;
    }

    auto [image, metadata] = read_goi(argv[1]);
    engine::transport::begin();
    auto [gpu_image, barrier] = engine::GPUImage::upload("GOI Image", engine::GPUImageInfo{}
                                                             .new_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                                             .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                                             .data(image.data())
                                                             .size(image.size())
                                                             .width(metadata.width)
                                                             .height(metadata.height)
                                                             .format(metadata.format));
    auto image_timeline = engine::transport::end();
    auto gpu_image_view = engine::GPUImageView{gpu_image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).create();
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    bool barrier_applied = false;

    auto image_sampler = engine::Sampler{}.create();

    free(image.data());

    uint32_t image_vertex_spv_size;
    auto image_vertex_spv_data = engine::util::read_file("goiview_vertex.spv", &image_vertex_spv_size);
    auto image_vertex_module = engine::create_shader({image_vertex_spv_data, image_vertex_spv_size});
    free(image_vertex_spv_data);

    uint32_t image_fragment_spv_size;
    auto image_fragment_spv_data = engine::util::read_file("goiview_fragment.spv", &image_fragment_spv_size);
    auto image_fragment_module = engine::create_shader({image_fragment_spv_data, image_fragment_spv_size});
    free(image_fragment_spv_data);

    auto image_descriptor_layout = engine::DescriptorSet<engine::descriptor::Binding{
        .count = 1,
        .type = engine::descriptor::Binding::SampledImage,
        .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
    }>::create();

    auto image_pipeline = engine::GraphicsPipeline(engine::GraphicsPipelineBuilder{}
                                                       .vertex(image_vertex_module)
                                                       .fragment(image_fragment_module)
                                                       .push_constant_size(ImagePC::size)
                                                       .add_color_attachment(engine::swapchain_format)
                                                       .descriptor_layout(0, image_descriptor_layout))
                              .cull_mode(engine::CullMode::NoCull);

    double accum = 0;
    double last_time = glfwGetTime();

    bool done = false;
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

            engine::event::update_tick();
        }

        if (engine::prepare_frame()) {
            rebuild(image_pipeline);
        }

        {
            engine::prepare_draw();
            if (!barrier_applied) {
                engine::synchronization::begin_barriers();
                engine::synchronization::apply_barrier(barrier);
                engine::synchronization::end_barriers();
                barrier_applied = true;
            }

            engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
                engine::RenderingAttachement{}
                    .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                    .set_load_op(engine::LoadOp::Clear)
                    .set_store_op(engine::StoreOp::Store)));

            if (engine::transport::is_ready(image_timeline)) {
                auto image_descriptor = engine::descriptor::new_set(image_descriptor_layout);
                engine::descriptor::begin_update(image_descriptor);
                engine::descriptor::update_sampled_image(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, gpu_image_view,
                                                         image_sampler);
                engine::descriptor::end_update();

                uint8_t image_pc[ImagePC::size]{};
                ImagePC::write(image_pc, glm::uvec2{engine::swapchain_extent.width, engine::swapchain_extent.height},
                               glm::uvec2{metadata.width, metadata.height});

                image_pipeline.bind();
                image_pipeline.draw(engine::GraphicsPipeline::DrawParams{
                    .push_constant = image_pc,
                    .descriptor_indexes =
                        {
                            image_descriptor,
                            engine::descriptor::null_set,
                            engine::descriptor::null_set,
                            engine::descriptor::null_set,
                        },
                    .vertex_count = 3,
                });
            }

            engine::rendering::end();
        }

    end_of_frame:
        if (engine::next_frame()) {
            rebuild(image_pipeline);
            engine::increment_frame();
        }
    }

    vkDeviceWaitIdle(engine::device);

    gpu_image.destroy();
    engine::GPUImageView::destroy(gpu_image_view);
    engine::Sampler::destroy(image_sampler);

    engine::destroy_descriptor_set_layout(image_descriptor_layout);

    engine::destroy_shader(image_vertex_module);
    engine::destroy_shader(image_fragment_module);
    image_pipeline.destroy();

    engine::visbuffer::destroy();
    engine::destroy();
    return 0;
}
