#include "project.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

#include <fstream>

namespace project {
    std::filesystem::path global_editor_config{};

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
        o << nlohmann::json{
            {"materials", "./assets/materials.json"},
            {"models_directory", "./assets/models"},
            {"models_registry", "./assets/models.reg"},
            {"textures_directory", "./assets/textures"},
            {"textures_registry", "./assets/textures.reg"},
            {"scenes", "./scenes.json"},
            {"editor_state", "./editor_state.json"},
            {"asset_inputs", "./assets/inputs.json"},
            {"dependency_metadata_directory", "./assets/dependencies"}
        }.dump(4);

        std::filesystem::create_directory("./assets");
        std::filesystem::create_directory("./assets/models");
        std::filesystem::create_directory("./assets/textures");

        std::filesystem::create_directory("./assets/dependencies");
        std::filesystem::create_directory("./assets/dependencies/models");
        std::filesystem::create_directory("./assets/dependencies/textures");

        o.flush();

        std::filesystem::current_path(revert);
    }

    void find_global_editor_config() {

    }
}
