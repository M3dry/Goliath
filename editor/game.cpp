#include "game.hpp"
#include "goliath/dyn_module.hpp"
#include "goliath/engine.hpp"
#include "goliath/game_interface.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include <vulkan/vulkan_core.h>

void rebuild_target(engine::GPUImage* targets, VkImageView* target_views, glm::uvec2 target_dimensions,
                    engine::game_interface::GameConfig config) {
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
                                               config.target_finish_stage, config.target_finish_access);

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

    Game game = Game::make(((engine::game_interface::MainFn*)(*f))());
    engine::dyn_module::destroy(*mod);

    return game;
}

Game Game::make(engine::game_interface::GameConfig config) {
    Game game{};

    game.config = config;
    game.waits.resize(game.config.max_wait_count + 1);

    auto required_dims = game.config.target_dimensions == glm::uvec2{0, 0}
                             ? glm::uvec2{engine::swapchain_extent.width, engine::swapchain_extent.height}
                             : game.config.target_dimensions;
    if (required_dims != game.target_dimensions || game.target_format != game.config.target_format) {
        game.target_dimensions = required_dims;
        rebuild_target(game.targets.data(), game.target_views.data(), game.target_dimensions, game.config);
    }

    engine::game_interface::make_engine_service(&game.es);
    engine::game_interface::make_frame_service(&game.fs);
    engine::game_interface::make_tick_service(&game.ts);
    return game;
}

void Game::unload() {
    destroy();

    user_state = nullptr;

    for (int i = 0; i < engine::frames_in_flight; i++) {
        engine::gpu_image::destroy(targets[i]);
        engine::gpu_image_view::destroy(target_views[i]);
    }
}

void Game::state_tick(glm::uvec2 game_window_dims) {
    es.frame_ix = engine::get_current_frame();

    auto current_frame = engine::get_current_frame();
    fs.target = targets[current_frame].image;
    fs.target_view = target_views[current_frame];
    fs.target_dimensions = target_dimensions;
    engine::game_interface::update_frame_service(&fs, current_frame);

    if (config.target_dimensions == glm::uvec2{0, 0} && game_window_dims != target_dimensions) {
        target_dimensions = game_window_dims;
        rebuild_target(targets.data(), target_views.data(), target_dimensions, config);
    }
}

void Game::init(uint32_t argc, char** argv) {
    user_state = config.funcs.init(&es, argc, argv);
}

void Game::destroy() {
    config.funcs.destroy(user_state, &es);
}

void Game::resize() {
    auto resize = config.funcs.resize;
    if (resize == nullptr) return;

    resize(user_state, &es);
}

void Game::tick() {
    config.funcs.tick(user_state, &ts, &es);
}

void Game::draw_imgui() {
    config.funcs.draw_imgui(user_state, &es);
}

uint32_t Game::render() {
    return config.funcs.render(user_state, engine::get_cmd_buf(), &fs, &es, waits.data() + 1) + 1;
}

void Game::blit_game_target(engine::GPUImage out, glm::uvec2 out_dims) {
    engine::synchronization::begin_barriers();
    engine::synchronization::apply_barrier(out.transition(
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT));
    engine::synchronization::end_barriers();

    VkClearColorValue clear{
        .float32 = {0, 0, 0, 1},
    };

    VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    vkCmdClearColorImage(engine::get_cmd_buf(), out.image, out.current_layout, &clear, 1, &range);

    engine::synchronization::begin_barriers();
    engine::synchronization::apply_barrier(out.transition(
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT));
    engine::synchronization::end_barriers();

    auto current_frame = engine::get_current_frame();
    auto src = targets[current_frame];

    VkImageBlit2 region{};
    if (config.target_dimensions == glm::uvec2{0, 0} ||
        config.target_blit_strategy == engine::game_interface::GameConfig::Stretch) {
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
                        .y = (int32_t)out_dims.x,
                        .z = 1,
                    },
                },
        };
    } else if (config.target_blit_strategy == engine::game_interface::GameConfig::LetterBox) {
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
    engine::synchronization::apply_barrier(out.transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));
    engine::synchronization::end_barriers();
}
