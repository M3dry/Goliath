#pragma once

#include "goliath/dyn_module.hpp"
#include "goliath/engine.hpp"
#include "goliath/game_interface.hpp"
#include "goliath/texture.hpp"
#include <array>
#include <cstdint>
#include <glm/ext/vector_uint2.hpp>

class Game {
  public:
    enum Err {
        ModuleLoad,
        SymbolLookup
    };

    engine::game_interface::EngineService es;
    engine::game_interface::FrameService fs;
    engine::game_interface::TickService ts;

    std::array<engine::GPUImage, engine::frames_in_flight> targets{};
    std::array<VkImageView, engine::frames_in_flight> target_views{};
    glm::uvec2 target_dimensions{-1, -1};
    VkFormat target_format{};

    engine::game_interface::GameConfig config;
    void* user_state = nullptr;
    std::vector<VkSemaphoreSubmitInfo> waits{};

    std::optional<engine::dyn_module::DynModule> mod;

    static std::expected<Game, Err> load(const char* plugin_file);
    static Game make(engine::game_interface::GameConfig config);
    void unload();

    void init(uint32_t argc, char** argv);
    void destroy();
    void resize();
    void tick(bool focused);
    void draw_game_imgui();
    uint32_t render(glm::uvec2 game_window_dims);

    void blit_game_target(engine::GPUImage out, glm::uvec2 out_dims);

  private:
    bool destroyed_state = true;
    bool focused = false;
};

struct GameView {
    static VkSampler sampler;

    bool skipped_window = false;
    std::array<engine::GPUImage, engine::frames_in_flight> images{};
    std::array<VkImageView, engine::frames_in_flight> views{};
    std::array<glm::uvec2, engine::frames_in_flight> dimensions{};
    std::array<VkDescriptorSet, engine::frames_in_flight> textures{};
    std::array<VkDescriptorSet, engine::frames_in_flight> textures_freeup{};

    GameView();

    bool draw_pane();
    void blit(Game& game);

    static void init();
    void destroy();
};
