#include "goliath/game_interface2.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/imgui.hpp"
#include "goliath/materials.hpp"
#include "goliath/models.hpp"
#include "goliath/rendering.hpp"
#include "goliath/scenes.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/textures.hpp"
#include "goliath/transport2.hpp"
#include "goliath/util.hpp"
#include "goliath/visbuffer.hpp"
#include "goliath/vma_ptrs.hpp"
#include "imgui.h"
#include <vulkan/vulkan_core.h>

namespace engine::game_interface2 {
    EngineService make_engine_service(Assets* assets) {
        return EngineService{.assets = EngineService::AssetsServicePtrs{
                                 .assets = assets,
                                 .acquire_scene = [](auto* a, auto handle) { a->acquire(handle); },
                                 .acquire_model = [](auto* a, auto handle) { a->acquire(handle); },
                                 .acquire_texture = [](auto* a, auto handle) { a->acquire(handle); },
                                 .release_scene = [](auto* a, auto handle) { a->release(handle); },
                                 .release_model = [](auto* a, auto handle) { a->release(handle); },
                                 .release_texture = [](auto* a, auto handle) { a->release(handle); },
                             },
                             .textures = EngineService::TexturesServicePtrs{
                                 .name = [](auto gid, auto* err, auto* erred) -> std::string* {
                                     auto res = textures::get_name(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return nullptr;
                                     }

                                     return *res;
                                 },
                                 .image = [](auto gid, auto* err, auto* erred) -> GPUImage {
                                     auto res = textures::get_image(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return GPUImage{};
                                     }

                                     return *res;
                                 },
                                 .image_view = [](auto gid, auto* err, auto* erred) -> VkImageView {
                                     auto res = textures::get_image_view(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return nullptr;
                                     }

                                     return *res;
                                 },
                                 .texture_pool = []() { return &textures::get_texture_pool(); },
                             },
                             .materials = EngineService::MaterialsServicePtrs{
                                 .instance_data =
                                     [](auto mat_id, auto instance_ix, auto* size) {
                                         auto ret = materials::get_instance_data(mat_id, instance_ix);

                                         *size = ret.size();
                                         return ret.data();
                                     },
                                 .buffer = []() { return materials::get_buffer(); },
                                 .set_instance_data =
                                     [](auto mat_id, auto instance_ix, auto* new_data) {
                                         materials::update_instance_data(mat_id, instance_ix, new_data);
                                     },
                             },
                             .models = EngineService::ModelsServicePtrs{
                                 .name = [](auto gid, auto* err, auto* erred) -> std::string* {
                                     auto res = models::get_name(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return nullptr;
                                     }

                                     return *res;
                                 },
                                 .cpu_model = [](auto gid, auto* err, auto* erred) -> Model* {
                                     auto res = models::get_cpu_model(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return nullptr;
                                     }

                                     return *res;
                                 },
                                 .ticket = [](auto gid, auto* err, auto* erred) -> transport2::ticket {
                                     auto res = models::get_ticket(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return transport2::ticket{};
                                     }

                                     return *res;
                                 },
                                 .draw_buffer = [](auto gid, auto* err, auto* erred) -> Buffer {
                                     auto res = models::get_draw_buffer(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return Buffer{};
                                     }

                                     return *res;
                                 },
                                 .gpu_model = [](auto gid, auto* err, auto* erred) -> GPUModel {
                                     auto res = models::get_gpu_model(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return GPUModel{};
                                     }

                                     return *res;
                                 },
                                 .gpu_group = [](auto gid, auto* err, auto* erred) -> GPUGroup {
                                     auto res = models::get_gpu_group(gid);
                                     *erred = !res.has_value();
                                     if (!res) {
                                         *err = res.error();
                                         return GPUGroup{};
                                     }

                                     return *res;
                                 },
                             },
                             .scenes = EngineService::ScenesServicePtrs{
                                 .instance_transforms_buffer =
                                     [](auto scene_ix, auto* ticket) {
                                         return scenes::get_instance_transforms_buffer(scene_ix, *ticket);
                                     },
                                 .used_models =
                                     [](auto scene_ix, auto* count) {
                                         auto ret = scenes::get_used_models(scene_ix);
                                         *count = ret.size();
                                         return ret.data();
                                     },
                             },
                             .fatal = [](const char* message) { throw GameFatalException(message); }};
    }

    FrameService make_frame_service(Assets* assets) {
        return FrameService{
            .assets = FrameService::AssetsServicePtrs{
                .assets = assets,
                .draw_model = [](auto* a, auto handle) { return a->draw_model(handle); },
                .end_model_draw = [](auto* a, auto handle) { a->end_model_draw(handle); },
                .draw_texture = [](auto* a, auto handle) { return a->draw_texture(handle); },
                .end_texture_draw = [](auto* a, auto handle) { a->end_texture_draw(handle); },
                .draw_scene = [](auto* a, auto handle, auto* t,
                                 auto* addr) { return a->draw_scene(handle, *t, *addr); },
                .scene_next_model = [](auto* a, auto* it) { return a->scene_next_model(it); },
                .end_scene_draw = [](auto* a, auto* it) { a->end_scene_draw(it); }},
        };
    }

    TickService make_tick_service() {
        return TickServicePtrs{
            .is_held = event::is_held,
            .was_released = event::was_released,
            .get_mouse_delta = event::get_mouse_delta,
            .get_mouse_absolute = event::get_mouse_absolute,
        };
    }

    GameFunctions GameFunctions::make(GameFunctionsPtrs ptrs) {
        return GameFunctions{
            .game = ptrs,
            .__engine_set_state = [](auto* state) { engine::set_state((State*)state); },
            .__engine_set_swapchain = engine::set_swapchain_state,
            .__engine_set_imgui_context = ImGui::SetCurrentContext,
            .__engine_set_vk_funcs = []() {
                VK_CHECK(volkInitialize());
                volkLoadInstance(state->instance);
                volkLoadDevice(device());
            },
            .__engine_set_transport_state = engine::transport2::set_internal_state,
            .__engine_set_visbuffer_state = engine::visbuffer::set_internal_state,
            .__engine_set_vma_ptrs = engine::vma_ptrs::set_internal_state,
        };
    }

    void start(GameConfig config, const AssetPaths& asset_paths, uint32_t argc, char** argv) {
        std::vector<VkSemaphoreSubmitInfo> waits{};
        waits.resize(config.max_wait_count + 1);

        init(Init{
            .window_name = config.name,
            .fullscreen = config.fullscreen,
            .textures_directory =
                asset_paths.textures_dir == nullptr ? std::nullopt : std::make_optional(asset_paths.textures_dir),
            .models_directory =
                asset_paths.models_dir == nullptr ? std::nullopt : std::make_optional(asset_paths.models_dir),
        });

        auto assets = Assets::init(config.asset_inputs);
        if (asset_paths.asset_inputs != nullptr) {
            auto asset_inputs_json = util::read_json(asset_paths.asset_inputs);
            if (!asset_inputs_json.has_value() && asset_inputs_json.error() == util::ReadJsonErr::FileErr &&
                !std::filesystem::exists(asset_paths.asset_inputs)) {
                asset_inputs_json = Assets::default_json();
            } else if (!asset_inputs_json.has_value()) {
                printf("Assets inputs file is corrupted\n");
            }

            assets.load(*asset_inputs_json);
        }

        if (asset_paths.textures_reg != nullptr) {
            auto tex_reg_json = util::read_json(asset_paths.textures_reg);
            if (!tex_reg_json.has_value() && tex_reg_json.error() == util::ReadJsonErr::FileErr &&
                !std::filesystem::exists(asset_paths.textures_reg)) {
                tex_reg_json = nlohmann::json{
                    {"textures", nlohmann::json::array()},
                    {"samplers", nlohmann::json::array()},
                };
            } else if (!tex_reg_json.has_value()) {
                printf("Texture registry file is corrupted\n");
                exit(-1);
            }

            samplers::load((*tex_reg_json)["samplers"]);
            textures::load((*tex_reg_json)["textures"]);
        }

        if (asset_paths.materials != nullptr) {
            auto mats_json = util::read_json(asset_paths.materials);
            if (!mats_json.has_value() && mats_json.error() == util::ReadJsonErr::FileErr &&
                !std::filesystem::exists(asset_paths.materials)) {
                mats_json = materials::default_json();
            } else if (!mats_json.has_value()) {
                printf("materials.json file is corrupted\n");
                exit(-1);
            }

            materials::load(*mats_json);
        }

        if (asset_paths.models_reg != nullptr) {
            auto models_registry_json = util::read_json(asset_paths.models_reg);
            if (!models_registry_json.has_value() && models_registry_json.error() == util::ReadJsonErr::FileErr &&
                !std::filesystem::exists(asset_paths.models_reg)) {
                models_registry_json = nlohmann::json::array();
            } else if (!models_registry_json.has_value()) {
                printf("Models registry file is corrupted\n");
                exit(-1);
            }

            models::load(*models_registry_json);
        }

        if (asset_paths.scenes != nullptr) {
            auto scenes_json = util::read_json(asset_paths.scenes);
            if (!scenes_json.has_value() && scenes_json.error() == util::ReadJsonErr::FileErr &&
                !std::filesystem::exists(asset_paths.scenes)) {
                scenes_json = scenes::default_json();
            } else if (!scenes_json.has_value()) {
                printf("Models registry file is corrupted\n");
                exit(-1);
            }

            scenes::load(*scenes_json);
        }

        auto es = make_engine_service(&assets);
        auto fs = make_frame_service(&assets);
        auto ts = make_tick_service();

        glm::ivec2 target_dimension{0};
        std::array<GPUImage, frames_in_flight> targets{};
        std::array<VkImageView, frames_in_flight> target_views{};

        auto update_targets = [&config](GPUImage* targets, VkImageView* target_views, glm::ivec2& target_dimensions) {
            target_dimensions.x =
                config.target_dimensions.x == 0 ? get_swapchain_extent().width : config.target_dimensions.x;
            target_dimensions.y =
                config.target_dimensions.y == 0 ? get_swapchain_extent().height : config.target_dimensions.y;
            for (size_t i = 0; i < frames_in_flight; i++) {
                gpu_image_view::destroy(target_views[i]);
                gpu_image::destroy(targets[i]);

                targets[i] = gpu_image::upload(std::format("Target image #{}", i).c_str(),
                                               GPUImageInfo{}
                                                   .format(config.target_format)
                                                   .width(target_dimensions.x)
                                                   .height(target_dimensions.y)
                                                   .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                                   .new_layout(config.target_start_layout)
                                                   .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | config.target_usage),
                                               config.target_start_stage, config.target_start_access);

                target_views[i] =
                    gpu_image_view::create(GPUImageView{targets[i]}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
            }
        };

        update_targets(targets.data(), target_views.data(), target_dimension);

        void* user_data = nullptr;
        try {
            user_data = config.funcs.game.init(&es, argc - 1, argv + 1);

            double accum = 0;
            double last_time = glfwGetTime();
            double dt = (1000.0 / config.tps) / 1000.0;

            bool done = false;
            while (!glfwWindowShouldClose(window()) && !done) {
                double time = glfwGetTime();
                double frame_time = time - last_time;
                last_time = time;
                accum += frame_time;

                auto state = event::poll();
                if (state == event::Minimized) {
                    glfwWaitEventsTimeout(0.05);
                    continue;
                }

                while (accum >= dt) {
                    accum -= dt;

                    config.funcs.game.tick(user_data, &ts, &es);

                    event::update_tick();
                }

                if (prepare_frame()) {
                    if (config.target_dimensions == glm::uvec2{0, 0})
                        update_targets(targets.data(), target_views.data(), target_dimension);
                    if (config.funcs.game.resize != nullptr) config.funcs.game.resize(user_data, &es);
                }

                imgui::begin();
                config.funcs.game.draw_imgui(user_data, &es);
                imgui::end();

                prepare_draw();

                auto* sstate = (SwapchainState*)get_swapchain_state();
                auto foreign_state = ForeignSwapchainState{
                    .format = config.target_format,
                    .extent =
                        VkExtent2D{
                            .width = (uint32_t)target_dimension.x,
                            .height = (uint32_t)target_dimension.y,
                        },
                    .images = targets.data(),
                    .views = target_views.data(),
                };
                set_swapchain_state(&foreign_state);
                auto wait_count = config.funcs.game.render(user_data, &fs, &es, waits.data() + 1) + 1;
                set_swapchain_state(sstate);

                VkImageMemoryBarrier2 swapchain_barrier{};
                swapchain_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                swapchain_barrier.pNext = nullptr;
                swapchain_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                swapchain_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                swapchain_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                swapchain_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                swapchain_barrier.image = get_swapchain();
                swapchain_barrier.subresourceRange = VkImageSubresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };

                auto& target = targets[get_current_frame()];

                synchronization::begin_barriers();
                synchronization::apply_barrier(swapchain_barrier);
                synchronization::end_barriers();

                VkClearColorValue clear{};
                clear.float32[0] = config.clear_color.x / 255.0f;
                clear.float32[1] = config.clear_color.y / 255.0f;
                clear.float32[2] = config.clear_color.z / 255.0f;
                clear.float32[3] = config.clear_color.w / 255.0f;

                VkImageSubresourceRange range{};
                range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                range.baseArrayLayer = 0;
                range.layerCount = 1;
                range.levelCount = 1;
                range.baseMipLevel = 0;

                vkCmdClearColorImage(get_cmd_buf(), get_swapchain(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                                     &range);

                swapchain_barrier.oldLayout = swapchain_barrier.newLayout;
                swapchain_barrier.srcStageMask = swapchain_barrier.dstStageMask;
                swapchain_barrier.srcAccessMask = swapchain_barrier.dstAccessMask;
                swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                swapchain_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

                synchronization::begin_barriers();
                synchronization::apply_barrier(swapchain_barrier);
                synchronization::apply_barrier(target.transition(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                 VK_ACCESS_2_TRANSFER_WRITE_BIT));
                synchronization::end_barriers();

                if (config.target_dimensions == glm::uvec2{0, 0} ||
                    config.target_blit_strategy == GameConfig::Stretch) {
                    VkImageBlit2 blit_region{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                        .pNext = nullptr,
                        .srcSubresource =
                            VkImageSubresourceLayers{
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .mipLevel = 0,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                        .srcOffsets =
                            {
                                VkOffset3D{
                                    .x = 0,
                                    .y = 0,
                                    .z = 0,
                                },
                                VkOffset3D{
                                    .x = target_dimension.x,
                                    .y = target_dimension.y,
                                    .z = 1,
                                },
                            },
                        .dstSubresource =
                            VkImageSubresourceLayers{
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .mipLevel = 0,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                        .dstOffsets =
                            {
                                VkOffset3D{
                                    .x = 0,
                                    .y = 0,
                                    .z = 0,
                                },
                                VkOffset3D{
                                    .x = (int32_t)get_swapchain_extent().width,
                                    .y = (int32_t)get_swapchain_extent().height,
                                    .z = 1,
                                },
                            },
                    };

                    VkBlitImageInfo2 blit_info{
                        .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                        .pNext = nullptr,
                        .srcImage = target.image,
                        .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        .dstImage = swapchain_barrier.image,
                        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .regionCount = 1,
                        .pRegions = &blit_region,
                        .filter = VK_FILTER_NEAREST,
                    };
                    vkCmdBlitImage2(get_cmd_buf(), &blit_info);
                } else if (config.target_blit_strategy == GameConfig::LetterBox) {
                    glm::vec2 dims = target_dimension;
                    glm::vec2 dims2 = {get_swapchain_extent().width, get_swapchain_extent().height};
                    float scale = std::min(dims2.x / dims.x, dims2.y / dims.y);
                    if (scale > 1.0f) {
                        scale = floor(scale);
                    }

                    dims *= scale;
                    glm::vec2 offset = (dims2 - dims) * 0.5f;

                    VkImageBlit2 blit_region{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                        .pNext = nullptr,
                        .srcSubresource =
                            VkImageSubresourceLayers{
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .mipLevel = 0,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                        .srcOffsets =
                            {
                                VkOffset3D{
                                    .x = 0,
                                    .y = 0,
                                    .z = 0,
                                },
                                VkOffset3D{
                                    .x = target_dimension.x,
                                    .y = target_dimension.y,
                                    .z = 1,
                                },
                            },
                        .dstSubresource =
                            VkImageSubresourceLayers{
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .mipLevel = 0,
                                .baseArrayLayer = 0,
                                .layerCount = 1,
                            },
                        .dstOffsets =
                            {
                                VkOffset3D{
                                    .x = (int32_t)offset.x,
                                    .y = (int32_t)offset.y,
                                    .z = 0,
                                },
                                VkOffset3D{
                                    .x = (int32_t)(offset.x + dims.x),
                                    .y = (int32_t)(offset.y + dims.y),
                                    .z = 1,
                                },
                            },
                    };

                    VkBlitImageInfo2 blit_info{
                        .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                        .pNext = nullptr,
                        .srcImage = target.image,
                        .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        .dstImage = swapchain_barrier.image,
                        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .regionCount = 1,
                        .pRegions = &blit_region,
                        .filter = VK_FILTER_NEAREST,
                    };
                    vkCmdBlitImage2(get_cmd_buf(), &blit_info);
                }

                swapchain_barrier.srcStageMask = swapchain_barrier.dstStageMask;
                swapchain_barrier.srcAccessMask = swapchain_barrier.dstAccessMask;
                swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                swapchain_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                swapchain_barrier.oldLayout = swapchain_barrier.newLayout;
                swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                synchronization::begin_barriers();
                synchronization::apply_barrier(swapchain_barrier);
                synchronization::apply_barrier(target.transition(config.target_start_layout, config.target_start_stage,
                                                                 config.target_start_access));
                synchronization::end_barriers();

                engine::rendering::begin(engine::RenderPass{}.add_color_attachment(
                    engine::RenderingAttachement{}
                        .set_image(engine::get_swapchain_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                        .set_load_op(engine::LoadOp::Load)
                        .set_store_op(engine::StoreOp::Store)));
                engine::imgui::render();
                engine::rendering::end();

                if (next_frame({waits.data(), wait_count})) {
                    if (config.target_dimensions == glm::uvec2{0, 0})
                        update_targets(targets.data(), target_views.data(), target_dimension);
                    if (config.funcs.game.resize != nullptr) config.funcs.game.resize(user_data, &es);
                    increment_frame();
                }
            }
        } catch (const GameFatalException& e) {
            fprintf(stderr, "%s\n", e.what());
        }

        vkDeviceWaitIdle(device());

        if (user_data != nullptr) config.funcs.game.destroy(user_data, &es);

        for (size_t i = 0; i < frames_in_flight; i++) {
            gpu_image_view::destroy(target_views[i]);
            gpu_image::destroy(targets[i]);
        }

        destroy();
    }
}
