#include "init_menu.hpp"
#include "GLFW/glfw3.h"
#include "goliath/engine.hpp"
#include "goliath/game_interface2.hpp"
#include "imgui.h"
#include "nfd.h"
#include "nfd_glfw3.h"
#include "project.hpp"

using engine::game_interface2::EngineService;
using engine::game_interface2::GameConfig;
using engine::game_interface2::GameFunctions;
using engine::game_interface2::TickService;

struct InitMenuState {
    std::vector<std::string> project_names;
    std::vector<std::filesystem::path> project_paths;
    std::vector<uint64_t> project_last_used;
};

GameConfig init_menu() {
    return GameConfig{
        .name = "Goliath Editor",
        .tps = 100,
        .fullscreen = false,
        .target_format = engine::swapchain_format,
        .target_start_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .target_start_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .target_start_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .target_dimensions = {0, 0},
        .clear_color = {0, 0, 0, 255},
        .funcs = GameFunctions::make({
            .init = [](const EngineService* es, auto argc, auto** argv) -> void* {

                glfwSetWindowSize(engine::window(), 1000, 700);
                glfwSetWindowAttrib(engine::window(), GLFW_RESIZABLE, GLFW_TRUE);
                glfwSetWindowAttrib(engine::window(), GLFW_FLOATING, GLFW_TRUE);

                auto j = engine::util::read_json(project::global_editor_config);
                if (!j) {
                    j = nlohmann::json{
                        {"names", nlohmann::json::array()},
                        {"paths", nlohmann::json::array()},
                        {"last_used", nlohmann::json::array()},
                    };
                }

                NFD_Init();
                auto state = new InitMenuState{
                    .project_names = (*j)["names"],
                    .project_paths = (*j)["paths"],
                    .project_last_used = (*j)["last_used"],
                };

                return state;
            },
            .destroy =
                [](void* _state, const EngineService* es) {
                    auto* state = (InitMenuState*)_state;

                    delete state;
                    NFD_Quit();
                },
            .tick =
                [](void* _state, const TickService* ts, const EngineService* es) -> bool {
                    return false;
                },
            .draw_imgui =
                [](void* _state, const EngineService* es) {
                    auto& state = *(InitMenuState*)_state;

                    ImGuiViewport* viewport = ImGui::GetMainViewport();

                    ImGui::SetNextWindowPos(viewport->Pos);
                    ImGui::SetNextWindowSize(viewport->Size);
                    ImGui::SetNextWindowViewport(viewport->ID);

                    ImGuiWindowFlags flags =
                        ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoBringToFrontOnFocus;

                    float xscale, yscale;
                    glfwGetWindowContentScale(engine::window(), &xscale, &yscale);

                    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * xscale);
                    ImGui::Begin("FullscreenWindow", nullptr, flags);

                    if (ImGui::Button("Create new project")) {
                        nfdpickfolderu8args_t args{};
                        NFD_GetNativeWindowFromGLFWWindow(engine::window(), &args.parentWindow);

                        nfdu8char_t* selected;
                        auto res = NFD_PickFolderU8_With(&selected, &args);
                        if (res == NFD_OKAY) {
                            const char* str = selected;

                            NFD_FreePathU8(selected);
                        }
                    }

                    if (ImGui::Button("Open Project")) {
                        nfdu8filteritem_t filters[1] = {
                            {"Project file", "json"},
                        };

                        nfdopendialogu8args_t args{};
                        args.filterList = filters;
                        args.filterCount = 1;
                        NFD_GetNativeWindowFromGLFWWindow(engine::window(), &args.parentWindow);

                        nfdu8char_t* selected;
                        auto res = NFD_OpenDialogU8_With(&selected, &args);
                        if (res == NFD_OKAY) {
                            const char* str = selected;

                            NFD_FreePathU8(selected);
                        }
                    }

                    ImGui::SeparatorText("Last opened");
                    for (size_t i = 0; i < state.project_names.size(); i++) {
                        ImGui::Text("%s", state.project_names[i].c_str());
                    }

                    ImGui::End();
                    ImGui::PopFont();
                },
            .render = [](void* _state, const engine::game_interface2::FrameService* fs, const EngineService* es,
                         VkSemaphoreSubmitInfo* waits) -> uint32_t { return 0; },
        }),
    };
}
