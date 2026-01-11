#include "goliath/exvar.hpp"

namespace engine::exvar {
    path::path(std::string str) : path_str(str) {
        size_t pos = 0;
        while (true) {
            size_t next = path_str.find('/', pos);
            segments.emplace_back(path_str.substr(pos, next - pos));
            if (next == std::string_view::npos) break;
            pos = next + 1;
        }
    }

    path::path(const char* str) : path_str(str) {
        size_t pos = 0;
        while (true) {
            size_t next = path_str.find('/', pos);
            segments.emplace_back(path_str.substr(pos, next - pos));
            if (next == std::string_view::npos) break;
            pos = next + 1;
        }
    }

    void to_json(nlohmann::json& j, const path& p) {
        j = p.path_str;
    }

    void from_json(const nlohmann::json& j, path& p) {
        p = path{std::string{j}};
    }

    struct RefValue {
        path path;
        Type type;
        std::vector<uint8_t> data;
    };

    void from_json(const nlohmann::json& j, RefValue& ref) {
        ref.path = j["path"];
        ref.type.type = j["component_type"];
        ref.type.count = j["value"].size();

        ref.type.visit(
            [&](auto&& t) {
                using T = std::remove_pointer_t<std::decay_t<decltype(t)>>;
                std::vector<T> data{};
                if (ref.type.count == 1) {
                    data.emplace_back(j["value"]);
                } else {
                    j["value"].get_to(data);
                }

                ref.data.resize(data.size() * sizeof(T));
                if constexpr (!std::is_same_v<T, bool>) {
                    std::memcpy(ref.data.data(), data.data(), ref.data.size());
                } else {
                    for (int i = 0; i < data.size(); i++) {
                        ref.data[i] = data[i];
                    }
                }
            },
            nullptr);
    }

    Registry::Registry() {}

    void Registry::add_reference(path path, Type type, void* address, uint32_t flags) {
        Var var{std::move(path), type, flags, address};

        auto it = std::lower_bound(variables.begin(), variables.end(), var, [](const Var& a, const Var& b) {
            return a.path.segments < b.path.segments;
        });
        
        if (it != variables.end() && it->path.path_str == var.path.path_str) {
            assert(false && "Duplicate exvar path");
            return;
        }

        variables.insert(it, std::move(var));
    }

    void Registry::override(nlohmann::json j) {
        std::vector<RefValue> values = j;

        size_t find_start = 0;
        for (const auto& [path, type, data] : values) {
            auto it = std::find_if(values.begin() + find_start, values.end(), [&](const auto& other) -> bool {
                return other.path.path_str == path.path_str;
            });

            if (it == values.end()) continue;

            find_start = std::distance(values.begin(), it);
            std::memcpy(variables[find_start].address, it->data.data(), it->data.size());
        }
    }

    nlohmann::json Registry::save() const {
        return variables;
    }

    void Registry::imgui_ui() {}

    std::span<Registry::Var> Registry::get_variables() {
        return variables;
    }

    void to_json(nlohmann::json& j, const Registry::Var& var) {
        j = nlohmann::json{};
        var.type.visit(
            [&](auto&& val) {
                if (var.type.count == 1) {
                    j["value"] = *val;
                } else {
                    j["value"] = std::span{val, var.type.count};
                }
            },
            var.address);

        j["path"] = var.path;
        j["component_type"] = var.type.type;
    }
}
