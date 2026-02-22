#include "game.hpp"
#include "goliath/dyn_module.hpp"
#include "goliath/engine.hpp"
#include "goliath/game_interface2.hpp"
#include "goliath/samplers.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/transport2.hpp"
#include "goliath/visbuffer.hpp"
#include "goliath/vma_ptrs.hpp"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "project.hpp"
#include <vulkan/vulkan_core.h>

void rebuild_target(engine::GPUImage* targets, VkImageView* target_views, glm::uvec2 target_dimensions,
                    engine::game_interface2::GameConfig config) {
    vkDeviceWaitIdle(engine::device());

    for (int i = 0; i < engine::frames_in_flight; i++) {
        engine::gpu_image::destroy(targets[i]);
        engine::gpu_image_view::destroy(target_views[i]);

        targets[i] = engine::gpu_image::upload(std::format("Game Target #{}", i).c_str(),
                                               engine::GPUImageInfo{}
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
            engine::gpu_image_view::create(engine::GPUImageView{targets[i]}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
    }
}

std::expected<Game, Game::Err> Game::load(const char* plugin_file) {
    auto mod = engine::dyn_module::load(plugin_file);
    if (!mod) {
        printf("MODULE error: %s\n", mod.error().c_str());
        return std::unexpected(Game::ModuleLoad);
    }

    auto f = engine::dyn_module::find_sym(GAME_INTERFACE_MAIN_SYM, *mod);
    if (!f) {
        printf("SYMBOL error: %s\n", f.error().c_str());
        return std::unexpected(Game::SymbolLookup);
    }

    Game game = Game::make(((engine::game_interface2::MainFn*)(*f))());
    game.mod = *mod;

    return game;
}

Game Game::make(engine::game_interface2::GameConfig config) {
    Game game{};

    game.targets = new engine::GPUImage[engine::frames_in_flight]{};
    game.target_views = new VkImageView[engine::frames_in_flight]{};
    game.sstate = new engine::ForeignSwapchainState{
        .format = config.target_format,
        .images = game.targets,
        .views = game.target_views,
    };

    game.config = config;
    game.config.funcs.set_swapchain(game.sstate);
    game.config.funcs.set_state(engine::get_internal_state());
    game.config.funcs.set_imgui_context(ImGui::GetCurrentContext());
    game.config.funcs.__engine_set_vk_funcs();
    game.config.funcs.__engine_set_transport_state(engine::transport2::get_internal_state());
    game.config.funcs.__engine_set_visbuffer_state(engine::visbuffer::get_internal_state());
    game.config.funcs.__engine_set_vma_ptrs(engine::vma_ptrs::get_internal_state());

    game.assets = engine::Assets::init(game.config.asset_inputs);
    auto asset_inputs_json = engine::util::read_json(project::asset_inputs);
    if (!asset_inputs_json.has_value() && asset_inputs_json.error() == engine::util::ReadJsonErr::FileErr && !std::filesystem::exists(project::asset_inputs)) {
        asset_inputs_json = engine::Assets::default_json();
    } else if (!asset_inputs_json.has_value()) {
        printf("Asset inputs file is corrupted\n");
        exit(-1);
    }
    game.assets.load(*asset_inputs_json);

    game.waits.resize(game.config.max_wait_count + 1);

    auto required_dims = game.config.target_dimensions == glm::uvec2{0, 0}
                             ? glm::uvec2{engine::get_swapchain_extent().width, engine::get_swapchain_extent().height}
                             : game.config.target_dimensions;
    if (required_dims != game.target_dimensions || game.target_format != game.config.target_format) {
        game.target_dimensions = required_dims;
        rebuild_target(game.targets, game.target_views, game.target_dimensions, game.config);
    }

    game.es = engine::game_interface2::make_engine_service(&game.assets);
    game.fs = engine::game_interface2::make_frame_service(&game.assets);
    game.ts = engine::game_interface2::make_tick_service();
    return game;
}

void Game::unload() {
    destroy();
    config.asset_inputs.destroy();
    assets.destroy();

    user_state = nullptr;
    delete sstate;

    for (int i = 0; i < engine::frames_in_flight; i++) {
        engine::gpu_image::destroy(targets[i]);
        engine::gpu_image_view::destroy(target_views[i]);
    }
    delete[] targets;
    delete[] target_views;
    if (mod) engine::dyn_module::destroy(*mod);
}

void Game::init(uint32_t argc, char** argv) {
    if (!destroyed_state) assert(false);
    destroyed_state = false;

    sstate->extent = VkExtent2D{
        .width = target_dimensions.x,
        .height = target_dimensions.y,
    };

    try {
        user_state = config.funcs.game.init(&es, argc, argv);
    } catch (const engine::game_interface2::GameFatalException& e) {
        printf("GAME EXCEPTION: %s\n", e.what());
    }
}

void Game::destroy() {
    if (destroyed_state) return;

    try {
        config.funcs.game.destroy(user_state, &es);
        destroyed_state = true;
    } catch (const engine::game_interface2::GameFatalException& e) {
        printf("GAME EXCEPTION: %s\n", e.what());
    }
}

void Game::tick(bool focused) {
    if (focused) {
        engine::game_interface2::TickServicePtrs ptrs{
            .is_held = [](auto x) { return false; },
            .was_released = [](auto x) { return false; },

            .get_mouse_delta = []() { return glm::vec2{}; },
            .get_mouse_absolute = []() { return glm::vec2{}; },
        };

        ts = ptrs;
    } else {
        ts = engine::game_interface2::make_tick_service();
    }

    try {
        config.funcs.game.tick(user_state, &ts, &es);
    } catch (const engine::game_interface2::GameFatalException& e) {
        printf("GAME EXCEPTION: %s\n", e.what());
    }
}

void Game::draw_game_imgui() {
    try {
        config.funcs.game.draw_imgui(user_state, &es);
    } catch (const engine::game_interface2::GameFatalException& e) {
        printf("GAME EXCEPTION: %s\n", e.what());
    }
}

uint32_t Game::render(glm::uvec2 game_window_dims) {
    if (config.target_dimensions == glm::uvec2{0, 0} && game_window_dims != target_dimensions) {
        target_dimensions = game_window_dims;
        rebuild_target(targets, target_views, target_dimensions, config);
        if (auto resize = config.funcs.game.resize; resize != nullptr) resize(user_state, &es);
    }

    sstate->extent = VkExtent2D{
        .width = target_dimensions.x,
        .height = target_dimensions.y,
    };

    try {
        return config.funcs.game.render(user_state, &fs, &es, waits.data() + 1) + 1;
    } catch (const engine::game_interface2::GameFatalException& e) {
        printf("GAME EXCEPTION: %s\n", e.what());
        return 0;
    }
}

void Game::blit_game_target(engine::GPUImage out, glm::uvec2 out_dims) {
    auto final_stage = out.current_stage;
    auto final_access = out.current_access;
    auto final_layout = out.current_layout;

    engine::synchronization::begin_barriers();
    engine::synchronization::apply_barrier(out.transition(
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT));
    engine::synchronization::end_barriers();

    VkClearColorValue clear{};
    clear.float32[0] = config.clear_color.x / 255.0f;
    clear.float32[1] = config.clear_color.y / 255.0f;
    clear.float32[2] = config.clear_color.z / 255.0f;
    clear.float32[3] = config.clear_color.w / 255.0f;

    VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    vkCmdClearColorImage(engine::get_cmd_buf(), out.image, out.current_layout, &clear, 1, &range);

    auto current_frame = engine::get_current_frame();
    auto src = targets[current_frame];

    engine::synchronization::begin_barriers();
    engine::synchronization::apply_barrier(out.transition(
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT));
    engine::synchronization::apply_barrier(src.transition(
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT));
    engine::synchronization::end_barriers();

    VkImageBlit2 region{};
    if (config.target_dimensions == glm::uvec2{0, 0} ||
        config.target_blit_strategy == engine::game_interface2::GameConfig::Stretch) {
        region = {
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
                        .x = (int32_t)target_dimensions.x,
                        .y = (int32_t)target_dimensions.y,
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
                        .x = (int32_t)out_dims.x,
                        .y = (int32_t)out_dims.y,
                        .z = 1,
                    },
                },
        };
    } else if (config.target_blit_strategy == engine::game_interface2::GameConfig::LetterBox) {
        glm::vec2 src_dims = target_dimensions;
        glm::vec2 dst_dims = out_dims;
        float scale = std::min(dst_dims.x / src_dims.x, dst_dims.y / src_dims.y);
        if (scale > 1.0f) {
            scale = floor(scale);
        }

        src_dims *= scale;
        glm::vec2 offset = (dst_dims - src_dims) * 0.5f;

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
                        .x = (int32_t)target_dimensions.x,
                        .y = (int32_t)target_dimensions.y,
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
                        .x = (int32_t)(offset.x + src_dims.x),
                        .y = (int32_t)(offset.y + src_dims.y),
                        .z = 1,
                    },
                },
        };
    }

    VkBlitImageInfo2 blit_info{
        .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
        .pNext = nullptr,
        .srcImage = src.image,
        .srcImageLayout = src.current_layout,
        .dstImage = out.image,
        .dstImageLayout = out.current_layout,
        .regionCount = 1,
        .pRegions = &region,
        .filter = VK_FILTER_NEAREST,
    };
    vkCmdBlitImage2(engine::get_cmd_buf(), &blit_info);

    engine::synchronization::begin_barriers();
    engine::synchronization::apply_barrier(out.transition(final_layout, final_stage, final_access));
    engine::synchronization::apply_barrier(
        src.transition(config.target_start_layout, config.target_start_stage, config.target_start_access));
    engine::synchronization::end_barriers();
}

VkSampler GameView::sampler = nullptr;

GameView::GameView() {
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        dimensions[i].x = -1;
        dimensions[i].y = -1;
    }
}

bool GameView::draw_pane() {
    auto curr_frame = engine::get_current_frame();

    bool focused = ImGui::IsWindowFocused();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 avail = ImGui::GetWindowSize();
    skipped_window = avail.x <= 0 || avail.y <= 0;

    ImGui_ImplVulkan_RemoveTexture(textures_freeup[engine::get_current_frame()]);
    textures_freeup[engine::get_current_frame()] = nullptr;

    auto& dims = dimensions[curr_frame];
    if ((avail.x != dims.x || avail.y != dims.y) && !skipped_window) {
        auto& image = images[curr_frame];
        auto& view = views[curr_frame];
        auto& texture = textures[curr_frame];

        engine::gpu_image::destroy(image);
        engine::gpu_image_view::destroy(view);
        textures_freeup[(engine::get_current_frame() + 1) % engine::frames_in_flight] = texture;

        image = engine::gpu_image::upload(std::format("GameView window texture #{}", curr_frame).c_str(),
                                          engine::GPUImageInfo{}
                                              .new_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                              .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                              .width(avail.x)
                                              .height(avail.y)
                                              .format(engine::get_swapchain_format())
                                              .usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
                                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        view = engine::gpu_image_view::create(engine::GPUImageView{image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
        texture = ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        dims.x = avail.x;
        dims.y = avail.y;
    }

    if (!skipped_window) {
        auto dim = dimensions[engine::get_current_frame()];
        ImGui::Image(textures[engine::get_current_frame()], ImVec2{(float)dim.x, (float)dim.y});
    }

    return focused;
}

void GameView::blit(Game& game) {
    auto curr_frame = engine::get_current_frame();
    game.blit_game_target(images[curr_frame], dimensions[curr_frame]);
}

void GameView::init() {
    sampler = engine::samplers::get(engine::samplers::add(engine::Sampler{}));
}

void GameView::destroy() {
    for (size_t i = 0; i < engine::frames_in_flight; i++) {
        engine::gpu_image::destroy(images[i]);
        engine::gpu_image_view::destroy(views[i]);
        ImGui_ImplVulkan_RemoveTexture(textures[i]);
        ImGui_ImplVulkan_RemoveTexture(textures_freeup[i]);
    }
}
