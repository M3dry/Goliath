#include "state.hpp"

namespace state {
    bool want_to_save_ = false;

    std::string models_query{};
    int assets_scope{};
    bool assets_scene_only_scope;

    nlohmann::json default_json() {
        return nlohmann::json{
            {"models_query", ""},
            {"assets_scope", 0},
            {"assets_scene_only_scope", false},
        };
    }

    void load(const nlohmann::json& j) {
#define TRY_GET(key, def) do { auto x = j.find(#key); if (x != j.end()) x->get_to(key); else key = def; } while (false)
        TRY_GET(models_query, "");
        TRY_GET(assets_scope, 0);
        TRY_GET(assets_scene_only_scope, false);
#undef TRY_GET
    }

    nlohmann::json save() {
        return nlohmann::json{
            {"models_query", models_query},
            {"assets_scene_only_scope", assets_scene_only_scope},
        };
    }

    void modified_value() {
        want_to_save_ = true;
    }

    bool want_to_save() {
        return want_to_save_;
    }
}
