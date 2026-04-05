#include "init_menu.hpp"
#include "GLFW/glfw3.h"
#include "goliath/engine.hpp"
#include "goliath/game_interface2.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "nfd.h"
#include "nfd_glfw3.h"
#include "project.hpp"

#include <chrono>
#include <fstream>

using engine::game_interface2::EngineService;
using engine::game_interface2::GameConfig;
using engine::game_interface2::GameFunctions;
using engine::game_interface2::TickService;

std::optional<std::filesystem::path> selected_project{};

struct InitMenuState {
    std::vector<std::string> project_names;
    std::vector<std::filesystem::path> project_paths;
    std::vector<uint64_t> project_last_used;
    bool want_save;
    std::filesystem::path save_file;
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
                glfwSetWindowAttrib(engine::window(), GLFW_DECORATED, GLFW_TRUE);


                auto projects_file = project::global_editor_cache() / "projects.json";
                auto j = engine::util::read_json(projects_file);
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
                    .want_save = false,
                    .save_file = projects_file,
                };

                return state;
            },
            .destroy =
                [](void* _state, const EngineService* es) {
                    auto* state = (InitMenuState*)_state;

                    delete state;
                    NFD_Quit();
                },
            .tick = [](void* _state, const TickService* ts, const EngineService* es) -> bool {
                auto& state = *(InitMenuState*)_state;
                if (state.want_save) {
                    printf("saving to: %s %d\n", state.save_file.string().c_str(), selected_project.has_value());
                    std::ofstream o{state.save_file};
                    o << nlohmann::json{
                        {"names", state.project_names},
                        {"paths", state.project_paths},
                        {"last_used", state.project_last_used},
                    };
                }

                return selected_project.has_value();
            },
            .draw_imgui =
                [](void* _state, const EngineService* es) {
                    auto& state = *(InitMenuState*)_state;

                    ImGuiViewport* viewport = ImGui::GetMainViewport();

                    ImGui::SetNextWindowPos(viewport->Pos);
                    ImGui::SetNextWindowSize(viewport->Size);
                    ImGui::SetNextWindowViewport(viewport->ID);

                    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
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
                            selected_project = selected;

                            if (auto it = std::find(state.project_paths.begin(), state.project_paths.end(),
                                                    *selected_project);
                                it != state.project_paths.end()) {
                                auto ix = std::distance(state.project_paths.begin(), it);

                                state.project_last_used[ix] = std::chrono::duration_cast<std::chrono::seconds>(
                                                                  std::chrono::system_clock::now().time_since_epoch())
                                                                  .count();
                            } else {
                                state.project_names.emplace_back(selected_project->stem().string());
                                state.project_paths.emplace_back(*selected_project);
                                state.project_last_used.emplace_back(
                                    std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
                            }

                            state.want_save = true;

                            project::init(*selected_project);

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
                            selected_project = selected;
                            selected_project = selected_project->parent_path();

                            if (std::find(state.project_paths.begin(), state.project_paths.end(), *selected_project) ==
                                state.project_paths.end()) {
                                state.project_names.emplace_back(selected_project->stem().string());
                                state.project_paths.emplace_back(*selected_project);
                                state.project_last_used.emplace_back(
                                    std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
                                state.want_save = true;
                            }

                            NFD_FreePathU8(selected);
                        }
                    }

                    ImGui::SeparatorText("Last opened");
                    ImGui::BeginChild("last_opened_scroll");

                    auto frame_padding = ImGui::GetStyle().FramePadding;

                    static constexpr auto background_col = IM_COL32(40, 40, 40, 255);
                    static constexpr auto hover_col = IM_COL32(0, 0, 0, 255);

                    for (size_t i = 0; i < state.project_names.size(); i++) {
                        ImGui::PushID(i);

                        auto* dl = ImGui::GetWindowDrawList();
                        dl->ChannelsSplit(2);
                        dl->ChannelsSetCurrent(1);

                        auto min = ImGui::GetCursorPos();

                        ImGui::SetCursorPos(ImVec2{min.x + frame_padding.x, min.y + frame_padding.y});

                        ImGui::BeginGroup();
                        ImGui::Text("%s", state.project_names[i].c_str());
                        ImGui::Text("%s", state.project_paths[i].string().c_str());
                        ImGui::EndGroup();

                        auto max = ImGui::GetItemRectMax();
                        max.x += frame_padding.x;
                        max.y += frame_padding.y;

                        ImGui::SetCursorPos(min);
                        bool pressed = ImGui::InvisibleButton("##button", ImVec2{max.x - min.x, max.y - min.y});
                        bool hovered = ImGui::IsItemHovered();

                        dl->ChannelsSetCurrent(0);
                        dl->AddRectFilled(min, max, hovered ? hover_col : background_col);

                        dl->ChannelsMerge();

                        if (pressed) {
                            selected_project = state.project_paths[i];
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndChild();

                    ImGui::End();
                    ImGui::PopFont();
                },
            .render = [](void* _state, const engine::game_interface2::FrameService* fs, const EngineService* es,
                         VkSemaphoreSubmitInfo* waits) -> uint32_t { return 0; },
        }),
    };
}
