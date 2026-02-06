
#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/game_interface.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/rendering.hpp"
#include <fstream>
#include <vulkan/vulkan_core.h>

using namespace engine::game_interface;

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

struct State {
    Metadata metadata;
    engine::transport2::ticket image_ticket;
    engine::GPUImage image;
    VkImageView image_view;
    VkSampler sampler;

    VkDescriptorSetLayout set_layout;
    engine::GraphicsPipeline pipeline;
};

extern "C" GameConfig GAME_INTERFACE_MAIN() {
    return GameConfig{
        .name = "GoiView",
        .tps = 1,
        .fullscreen = false,
        .target_format = engine::swapchain_format,
        .target_start_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .target_end_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .target_finish_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .target_finish_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .target_dimensions = {0, 0},
        .clear_color = {0, 0, 0, 255},

        .funcs = GameFunctions{
            .init = [](const AssetPaths* asset_paths, const EngineService* es, auto argc, auto** argv) -> void* {
                auto* state = (State*)malloc(sizeof(State));

                auto engine_state = es->get_engine_state();
                glfwSetWindowAttrib(*engine_state.window, GLFW_DECORATED, GLFW_TRUE);
                glfwSetWindowAttrib(*engine_state.window, GLFW_RESIZABLE, GLFW_TRUE);
                glfwSetWindowAttrib(*engine_state.window, GLFW_AUTO_ICONIFY, GLFW_TRUE);

                auto [image_data, metadata] = read_goi(argv[1]);
                state->metadata = metadata;

                state->image =
                    es->texture.upload("Viewed image",
                                       engine::GPUImageInfo{}
                                           .new_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                           .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                           .data(image_data.data(), free, state->image_ticket, true)
                                           .size(image_data.size())
                                           .width(metadata.width)
                                           .height(metadata.height)
                                           .format(metadata.format),
                                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                state->image_view =
                    es->texture.create_view(engine::GPUImageView{state->image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));

                state->sampler = engine::sampler::create(engine::Sampler{});

                uint32_t image_vertex_spv_size;
                auto image_vertex_spv_data = engine::util::read_file("goiview_vertex.spv", &image_vertex_spv_size);
                auto image_vertex_module = engine::shader::create({image_vertex_spv_data, image_vertex_spv_size});
                free(image_vertex_spv_data);

                uint32_t image_fragment_spv_size;
                auto image_fragment_spv_data =
                    engine::util::read_file("goiview_fragment.spv", &image_fragment_spv_size);
                auto image_fragment_module = engine::shader::create({image_fragment_spv_data, image_fragment_spv_size});
                free(image_fragment_spv_data);

                state->set_layout = engine::DescriptorSet<engine::descriptor::Binding{
                    .count = 1,
                    .type = engine::descriptor::Binding::SampledImage,
                    .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
                }>::create();

                state->pipeline = engine::rendering::create_pipeline(engine::GraphicsPipelineBuilder{}
                                                                         .vertex(image_vertex_module)
                                                                         .fragment(image_fragment_module)
                                                                         .push_constant_size(ImagePC::size)
                                                                         .add_color_attachment(engine::swapchain_format)
                                                                         .descriptor_layout(0, state->set_layout))
                                      .cull_mode(engine::CullMode::NoCull);
                engine::shader::destroy(image_vertex_module);
                engine::shader::destroy(image_fragment_module);

                return state;
            },
            .destroy =
                [](void* _state, const EngineService* es) {
                    State* state = (State*)_state;

                    engine::gpu_image::destroy(state->image);
                    engine::gpu_image_view::destroy(state->image_view);
                    engine::sampler::destroy(state->sampler);

                    engine::destroy_descriptor_set_layout(state->set_layout);
                    engine::rendering::destroy_pipeline(state->pipeline);
                },
            .tick = [](void*, const auto* ts, const auto* es) {},
            .draw_imgui = [](void*, const auto* es) {},
            .render = [](void* _state, VkCommandBuffer cmd_buf, const FrameService* fs, const EngineService* es,
                         auto* wait_count) -> VkSemaphoreSubmitInfo* {
                auto* state = (State*)_state;
                if (es->transport.is_uploaded(state->image_ticket)) {
                    fs->rendering.begin(engine::RenderPass{}.add_color_attachment(
                        engine::RenderingAttachement{}
                            .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                            .set_load_op(engine::LoadOp::Clear)
                            .set_store_op(engine::StoreOp::Store)));

                    auto image_descriptor = fs->descriptor.new_set(state->set_layout);
                    fs->descriptor.begin_update(image_descriptor);
                    fs->descriptor.update_sampled_image(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                             state->image_view, state->sampler);
                    fs->descriptor.end_update();

                    uint8_t image_pc[ImagePC::size]{};
                    ImagePC::write(image_pc, fs->target_dimensions,
                                   glm::uvec2{state->metadata.width, state->metadata.height});

                    fs->rendering.bind(state->pipeline);
                    fs->rendering.draw(state->pipeline, engine::GraphicsPipeline::DrawParams{
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

                    fs->rendering.end();
                }

                *wait_count = 0;
                return nullptr;
            },
        },
    };
}

int main(int argc, char** argv) {
    AssetPaths asset_paths{
        .scenes = nullptr,

        .materials = nullptr,
        .models_reg = nullptr,
        .models_dir = nullptr,

        .textures_reg = nullptr,
        .textures_dir = nullptr,
    };

    start(GAME_INTERFACE_MAIN(), asset_paths, argc, argv);
}

// #include "goliath/buffer.hpp"
// #include "goliath/descriptor_pool.hpp"
// #include "goliath/engine.hpp"
// #include "goliath/event.hpp"
// #include "goliath/push_constant.hpp"
// #include "goliath/rendering.hpp"
// #include "goliath/samplers.hpp"
// #include "goliath/texture.hpp"
// #include <GLFW/glfw3.h>
// #include <cctype>
// #include <cstring>
// #include <fstream>
// #include <glm/ext/matrix_transform.hpp>
// #include <glm/gtc/type_ptr.hpp>
// #include <volk.h>
// #include <vulkan/vulkan_core.h>
//
// struct Metadata {
//     uint32_t width;
//     uint32_t height;
//     VkFormat format;
// };
//
// std::pair<std::span<uint8_t>, Metadata> read_goi(std::filesystem::path path) {
//     std::ifstream file{path, std::ios::binary | std::ios::ate};
//     auto size_signed = file.tellg();
//     if (size_signed < 0) {
//         assert(false && "texture somehow isn't on disk");
//     }
//
//     Metadata metadata{};
//     size_t image_size = (uint32_t)size_signed - sizeof(Metadata);
//     uint8_t* image_data = (uint8_t*)malloc(image_size);
//
//     file.seekg(0, std::ios::beg);
//     file.read((char*)&metadata, sizeof(Metadata));
//     file.read((char*)image_data, image_size);
//
//     return {{image_data, image_size}, metadata};
// }
//
// using ImagePC = engine::PushConstant<glm::uvec2, glm::uvec2>;
//
// void rebuild(engine::GraphicsPipeline& image_pipeline) {
//     image_pipeline.update_viewport_to_swapchain();
//     image_pipeline.update_scissor_to_viewport();
// }
//
// int main(int argc, char** argv) {
//     engine::init(engine::Init{
//         .window_name = "GoiView",
//         .texture_capacity = 0,
//         .fullscreen = false,
//     });
//     glfwSetWindowAttrib(engine::window, GLFW_DECORATED, GLFW_TRUE);
//     glfwSetWindowAttrib(engine::window, GLFW_RESIZABLE, GLFW_TRUE);
//     glfwSetWindowAttrib(engine::window, GLFW_AUTO_ICONIFY, GLFW_TRUE);
//
//     if (argc != 2) {
//         printf("USAGE: %s [file].goi\n", argv[0]);
//         return 0;
//     }
//
//     if (!std::filesystem::exists(argv[1])) {
//         printf("File `%s` doesn't exist\n", argv[1]);
//         return -1;
//     }
//
//     auto [image, metadata] = read_goi(argv[1]);
//     engine::transport2::ticket gpu_image_ticket;
//     auto gpu_image =
//         engine::gpu_image::upload("GOI Image",
//                                   engine::GPUImageInfo{}
//                                       .new_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
//                                       .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
//                                       .data(image.data(), free, gpu_image_ticket, true)
//                                       .size(image.size())
//                                       .width(metadata.width)
//                                       .height(metadata.height)
//                                       .format(metadata.format),
//                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
//     auto gpu_image_view =
//         engine::gpu_image_view::create(engine::GPUImageView{gpu_image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
//
//     auto image_sampler = engine::sampler::create(engine::Sampler{});
//
//     uint32_t image_vertex_spv_size;
//     auto image_vertex_spv_data = engine::util::read_file("goiview_vertex.spv", &image_vertex_spv_size);
//     auto image_vertex_module = engine::shader::create({image_vertex_spv_data, image_vertex_spv_size});
//     free(image_vertex_spv_data);
//
//     uint32_t image_fragment_spv_size;
//     auto image_fragment_spv_data = engine::util::read_file("goiview_fragment.spv", &image_fragment_spv_size);
//     auto image_fragment_module = engine::shader::create({image_fragment_spv_data, image_fragment_spv_size});
//     free(image_fragment_spv_data);
//
//     auto image_descriptor_layout = engine::DescriptorSet<engine::descriptor::Binding{
//         .count = 1,
//         .type = engine::descriptor::Binding::SampledImage,
//         .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
//     }>::create();
//
//     auto image_pipeline = engine::rendering::create_pipeline(engine::GraphicsPipelineBuilder{}
//                                                                  .vertex(image_vertex_module)
//                                                                  .fragment(image_fragment_module)
//                                                                  .push_constant_size(ImagePC::size)
//                                                                  .add_color_attachment(engine::swapchain_format)
//                                                                  .descriptor_layout(0, image_descriptor_layout))
//                               .cull_mode(engine::CullMode::NoCull);
//
//     double accum = 0;
//     double last_time = glfwGetTime();
//
//     bool done = false;
//     while (!glfwWindowShouldClose(engine::window)) {
//         double time = glfwGetTime();
//         double frame_time = time - last_time;
//         last_time = time;
//         accum += frame_time;
//
//         auto state = engine::event::poll();
//         if (state == engine::event::Minimized) {
//             glfwWaitEventsTimeout(0.05);
//             continue;
//         }
//
//         while (accum >= (1000.0 / 60.0) / 1000.0) {
//             accum -= (1000.0 / 60.0) / 1000.0;
//
//             engine::event::update_tick();
//         }
//
//         if (engine::prepare_frame()) {
//             rebuild(image_pipeline);
//         }
//
//         {
//             engine::prepare_draw();
//
//             engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
//                 engine::RenderingAttachement{}
//                     .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
//                     .set_load_op(engine::LoadOp::Clear)
//                     .set_store_op(engine::StoreOp::Store)));
//
//             if (engine::transport2::is_ready(gpu_image_ticket)) {
//                 auto image_descriptor = engine::descriptor::new_set(image_descriptor_layout);
//                 engine::descriptor::begin_update(image_descriptor);
//                 engine::descriptor::update_sampled_image(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, gpu_image_view,
//                                                          image_sampler);
//                 engine::descriptor::end_update();
//
//                 uint8_t image_pc[ImagePC::size]{};
//                 ImagePC::write(image_pc, glm::uvec2{engine::swapchain_extent.width, engine::swapchain_extent.height},
//                                glm::uvec2{metadata.width, metadata.height});
//
//                 image_pipeline.bind();
//                 image_pipeline.draw(engine::GraphicsPipeline::DrawParams{
//                     .push_constant = image_pc,
//                     .descriptor_indexes =
//                         {
//                             image_descriptor,
//                             engine::descriptor::null_set,
//                             engine::descriptor::null_set,
//                             engine::descriptor::null_set,
//                         },
//                     .vertex_count = 3,
//                 });
//             }
//
//             engine::rendering::end();
//         }
//
//         VkSemaphoreSubmitInfo wait{};
//         if (engine::next_frame({&wait, 1})) {
//             rebuild(image_pipeline);
//             engine::increment_frame();
//         }
//     }
//
//     vkDeviceWaitIdle(engine::device);
//
//     engine::gpu_image::destroy(gpu_image);
//     engine::gpu_image_view::destroy(gpu_image_view);
//     engine::sampler::destroy(image_sampler);
//
//     engine::destroy_descriptor_set_layout(image_descriptor_layout);
//
//     engine::shader::destroy(image_vertex_module);
//     engine::shader::destroy(image_fragment_module);
//     engine::rendering::destroy_pipeline(image_pipeline);
//
//     engine::destroy();
//     return 0;
// }
