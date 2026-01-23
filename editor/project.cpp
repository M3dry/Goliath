#include "project.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

#include <fstream>

namespace project {
    std::filesystem::path project_root{};
    std::filesystem::path materials{};
    std::filesystem::path models_directory{};
    std::filesystem::path models_registry{};
    std::filesystem::path textures_directory{};
    std::filesystem::path textures_registry{};
    std::filesystem::path scenes_file{};
    std::filesystem::path editor_state{};

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

    void init() {
        std::ofstream o{"./goliath.json"};
        o << nlohmann::json{
            {"materials", "./assets/materials.json"},
            {"models_directory", "./assets/models"},
            {"models_registry", "./assets/models.reg"},
            {"textures_directory", "./assets/textures"},
            {"textures_registry", "./assets/textures.reg"},
            {"scenes", "./scenes.json"},
            {"editor_state", "./editor_state.json"},
        }.dump(4);

        std::filesystem::create_directory("./assets");
        std::filesystem::create_directory("./assets/models");
        std::filesystem::create_directory("./assets/textures");

        o.flush();
    }
}
