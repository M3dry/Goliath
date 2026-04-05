#include "project.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

#include <fstream>

#ifdef _WIN32
#include <filesystem>
#include <shlobj.h> // SHGetKnownFolderPath
#include <stdexcept>
#include <windows.h>
#endif

namespace project {
    std::filesystem::path project_root{};
    std::filesystem::path materials{};
    std::filesystem::path models_directory{};
    std::filesystem::path models_registry{};
    std::filesystem::path textures_directory{};
    std::filesystem::path textures_registry{};
    std::filesystem::path scenes_file{};
    std::filesystem::path editor_state{};
    std::filesystem::path asset_inputs{};
    std::filesystem::path dependency_graph_metadata_directory{};

    void parse(const std::filesystem::path& json_file) {
        std::ifstream i{json_file};
        nlohmann::json j = nlohmann::json::parse(i);

        materials = std::filesystem::path{std::string{j["materials"]}};
        models_directory = std::filesystem::path{std::string{j["models_directory"]}};
        models_registry = std::filesystem::path{std::string{j["models_registry"]}};
        textures_directory = std::filesystem::path{std::string{j["textures_directory"]}};
        textures_registry = std::filesystem::path{std::string{j["textures_registry"]}};
        scenes_file = std::filesystem::path{std::string{j["scenes"]}};
        editor_state = std::filesystem::path{std::string{j["editor_state"]}};
        asset_inputs = std::filesystem::path{std::string{j["asset_inputs"]}};
        dependency_graph_metadata_directory = std::filesystem::path{std::string{j["dependency_metadata_directory"]}};
    }

    bool find_project() {
        std::filesystem::path current = std::filesystem::current_path();

        while (true) {
            std::filesystem::path project_file = current / "goliath.json";

            if (std::filesystem::exists(project_file)) {
                project_root = current;
                parse(project_file);
                return true;
            }

            std::filesystem::path parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }

        return false;
    }

    void init(std::filesystem::path root) {
        auto revert = std::filesystem::current_path();
        std::filesystem::current_path(root);

        std::ofstream o{"./goliath.json"};
        o << nlohmann::json{{"materials", "./assets/materials.json"},
                            {"models_directory", "./assets/models"},
                            {"models_registry", "./assets/models.reg"},
                            {"textures_directory", "./assets/textures"},
                            {"textures_registry", "./assets/textures.reg"},
                            {"scenes", "./scenes.json"},
                            {"editor_state", "./editor_state.json"},
                            {"asset_inputs", "./assets/inputs.json"},
                            {"dependency_metadata_directory", "./assets/dependencies"}}
                 .dump(4);

        std::filesystem::create_directory("./assets");
        std::filesystem::create_directory("./assets/models");
        std::filesystem::create_directory("./assets/textures");

        std::filesystem::create_directory("./assets/dependencies");
        std::filesystem::create_directory("./assets/dependencies/models");
        std::filesystem::create_directory("./assets/dependencies/textures");

        o.flush();

        std::filesystem::current_path(revert);
    }

    static std::filesystem::path xdg_dir(const char* xdg_type, const char* default_dir) {
        const char* xdg_config_home = std::getenv(xdg_type);
        std::filesystem::path path;

        if (xdg_config_home && *xdg_config_home) {
            path = xdg_config_home;
        } else {
            const char* home = std::getenv("HOME");
            if (!home || !*home) {
                throw std::runtime_error(std::format("couldn't find xdg dir for {}", xdg_type));
            }
            path = std::filesystem::path(home) / default_dir;
        }

        return path;
    }

#ifdef _WIN32
    std::filesystem::path windows_known_folder(REFKNOWNFOLDERID folder_id) {
        PWSTR wide_path = nullptr;

        HRESULT hr = SHGetKnownFolderPath(folder_id, 0, nullptr, &wide_path);
        if (FAILED(hr)) {
            throw std::runtime_error("SHGetKnownFolderPath failed");
        }

        std::filesystem::path result = wide_path;
        CoTaskMemFree(wide_path);
        return result;
    }
#endif

    const std::filesystem::path& global_editor_config() {
        static auto path = []() -> std::filesystem::path {
#ifdef __linux__
            auto path = xdg_dir("XDG_CONFIG_HOME", ".config");
#elif _WIN32
            auto path = windows_known_folder(FOLDERID_RoamingAppData);
#endif
            path /= "goliath";

            std::error_code ec;
            if (!std::filesystem::exists(path)) {
                if (!std::filesystem::create_directories(path, ec)) {
                    if (ec) {
                        throw std::runtime_error("Failed to create config directory: " + ec.message());
                    }
                }
            }

            return path;
        }();

        return path;
    }

    const std::filesystem::path& global_editor_cache() {
        static auto path = []() -> std::filesystem::path {
#ifdef __linux__
            auto path = xdg_dir("XDG_CACHE_HOME", ".cache");
#elif _WIN32
            auto path = windows_known_folder(FOLDERID_LocalAppData);
#endif
            path /= "goliath";

            std::error_code ec;
            if (!std::filesystem::exists(path)) {
                if (!std::filesystem::create_directories(path, ec)) {
                    if (ec) {
                        throw std::runtime_error("Failed to create config directory: " + ec.message());
                    }
                }
            }

            return path;
        }();

        return path;
    }
}
