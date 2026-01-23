#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace state {
    extern std::string models_query;
    extern int models_search_scope;

    nlohmann::json default_json();

    void load(const nlohmann::json& j);
    nlohmann::json save();

    void modified_value();
    bool want_to_save();
}
