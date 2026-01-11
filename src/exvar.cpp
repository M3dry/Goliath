#include "goliath/exvar.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace engine::exvar {
    void imgui_input_method(ComponentType ct, void* value) {
        Type{ct, 1}.visit(
            [&](auto&& v) {
                switch (ct) {
                    case Int8:
                        ImGui::InputScalar("##input", ImGuiDataType_S8, value);
                        break;
                    case Int16:
                        ImGui::InputScalar("##input", ImGuiDataType_S16, value);
                        break;
                    case Int32:
                        ImGui::InputScalar("##input", ImGuiDataType_S32, value);
                        break;
                    case Int64:
                        ImGui::InputScalar("##input", ImGuiDataType_S64, value);
                        break;
                    case UInt8:
                        ImGui::InputScalar("##input", ImGuiDataType_U8, value);
                        break;
                    case UInt16:
                        ImGui::InputScalar("##input", ImGuiDataType_U16, value);
                        break;
                    case UInt32:
                        ImGui::InputScalar("##input", ImGuiDataType_U32, value);
                        break;
                    case UInt64:
                        ImGui::InputScalar("##input", ImGuiDataType_U64, value);
                        break;
                    case Float:
                        ImGui::InputScalar("##input", ImGuiDataType_Float, value);
                        break;
                    case Double:
                        ImGui::InputScalar("##input", ImGuiDataType_Double, value);
                        break;
                    case Bool:
                        ImGui::Checkbox("##input", (bool*)value);
                        break;
                    case String:
                        ImGui::InputText("##input", (std::string*)value);
                        break;
                }
            },
            value);
    }

    path::path(std::string str) : path_str(str) {
        size_t pos = 0;
        while (true) {
            size_t next = path_str.find('/', pos);
            segments.emplace_back(path_str.substr(pos, next - pos));
            if (next == std::string::npos) break;
            pos = next + 1;
        }
    }

    path::path(const char* str) : path_str(str) {
        size_t pos = 0;
        while (true) {
            size_t next = path_str.find('/', pos);
            segments.emplace_back(path_str.substr(pos, next - pos));
            if (next == std::string::npos) break;
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

        auto it = std::lower_bound(variables.begin(), variables.end(), var,
                                   [](const Var& a, const Var& b) { return a.path.segments < b.path.segments; });

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
            auto it = std::find_if(values.begin() + find_start, values.end(),
                                   [&](const auto& other) -> bool { return other.path.path_str == path.path_str; });

            if (it == values.end()) continue;

            find_start = std::distance(values.begin(), it);
            std::memcpy(variables[find_start].address, it->data.data(), it->data.size());
        }
    }

    nlohmann::json Registry::save() const {
        return variables;
    }

    void Registry::imgui_ui() {
        std::span<const std::string> prev{};
        size_t imgui_depth = 0;
        size_t blocked_depth = -1;

        for (auto& var : variables) {
            const auto& path = var.path;

            size_t lcp = 0;
            while (lcp < path.segments.size() && lcp < (prev.size() == 0 ? 0 : prev.size() - 1) && path.segments[lcp] == prev[lcp]) {
                lcp++;
            }

            if (blocked_depth != -1 && lcp <= blocked_depth) {
                blocked_depth = -1;
            }

            while (imgui_depth > lcp) {
                ImGui::TreePop();
                imgui_depth--;
            }

            if (blocked_depth != -1) {
                prev = path.segments;
                continue;
            }

            for (size_t i = lcp; i + 1 < path.segments.size(); i++) {
                bool opened = ImGui::TreeNodeEx(path.segments[i].c_str(),
                                                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesToNodes);

                if (opened) {
                    ++imgui_depth;
                } else {
                    blocked_depth = i;
                    break;
                }
            }

            if (blocked_depth == -1) {
                ImGui::PushID(var.address);
                ImGui::Text("%s:", var.path.segments.back().c_str());

                if (var.type.count == 1) ImGui::SameLine();
                else ImGui::Indent();

                var.type.visit(
                    [&](auto&& value) {
                        for (size_t i = 0; i < var.type.count; i++) {
                            ImGui::PushID(i);
                            imgui_input_method(var.type.type, value + i);
                            ImGui::PopID();
                        }
                    },
                    var.address);

                if (var.type.count != 1) ImGui::Unindent();
                ImGui::PopID();
            }

            prev = path.segments;
        }

        while (imgui_depth > 0) {
            ImGui::TreePop();
            imgui_depth--;
        }
    }

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
