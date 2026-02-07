#pragma once

#include "goliath/engine.hpp"
#include "goliath/game_interface.hpp"
#include "goliath/texture.hpp"
#include <array>
#include <cstdint>
#include <glm/ext/vector_uint2.hpp>

struct Game {
    enum Err {
        ModuleLoad,
        SymbolLookup
    };

    engine::game_interface::EngineService es;
    engine::game_interface::FrameService fs;
    engine::game_interface::TickService ts;

    std::array<engine::GPUImage, engine::frames_in_flight> targets{};
    std::array<VkImageView, engine::frames_in_flight> target_views{};
    glm::uvec2 target_dimensions{0, 0};
    VkFormat target_format{};

    engine::game_interface::GameConfig config;
    void* user_state = nullptr;
    std::vector<VkSemaphoreSubmitInfo> waits{};

    static std::expected<Game, Err> load(const char* plugin_file);
    static Game make(engine::game_interface::GameConfig config);
    void unload();
    void state_tick(glm::uvec2 game_window_dims);

    void init(uint32_t argc, char** argv);
    void destroy();
    void resize();
    void tick();
    void draw_imgui();
    uint32_t render();

    void blit_game_target(engine::GPUImage out, glm::uvec2 out_dims);
};
