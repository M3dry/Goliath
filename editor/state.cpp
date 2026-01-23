#include "state.hpp"

namespace state {
    bool want_to_save_ = false;

    std::string models_query{};
    int models_search_scope{};

    nlohmann::json default_json() {
        return nlohmann::json{
            {"models_query", ""},
            {"models_search_scope", 0},
        };
    }

    void load(const nlohmann::json& j) {
        j["models_query"].get_to(models_query);
        j["models_search_scope"].get_to(models_search_scope);
    }

    nlohmann::json save() {
        return nlohmann::json{
            {"models_query", models_query},
            {"models_search_scope", models_search_scope},
        };
    }

    void modified_value() {
        want_to_save_ = true;
    }

    bool want_to_save() {
        return want_to_save_;
    }
}
