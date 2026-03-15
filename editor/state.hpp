#pragma once

#include "goliath/dependency_graph.hpp"
#include "goliath/materials.hpp"
#include <string>

#include <nlohmann/json.hpp>

namespace state {
    extern std::string models_query;
    extern int assets_scope;
    extern bool assets_scene_only_scope;

    extern engine::DependencyGraph* dependency_graph;
    extern engine::Materials* materials;

    nlohmann::json default_json();

    void load(const nlohmann::json& j);
    nlohmann::json save();

    void modified_value();
    bool want_to_save();
}
