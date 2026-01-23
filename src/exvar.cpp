#include "goliath/exvar.hpp"
#include "imgui.h"
#include <type_traits>
#include <variant>

namespace engine::exvar {
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

    void Registry::Var::destroy() {
        std::visit(
            [&](auto&& arg) {
                if constexpr (std::same_as<imgui_reflection::Input, std::decay_t<decltype(arg)>>) {
                    return;
                } else {
                    free(arg.min);
                }
            },
            input_method);
    }

    Registry::Registry() {}

    void Registry::add_input_reference(path path, Type type, void* address, uint64_t flags) {
        add_reference(path, type, address, imgui_reflection::Input{flags});
    }

    void Registry::add_slider_reference(path path, Type type, void* address, void* min, void* max, const char* format,
                                        uint64_t flags) {
        assert(type.type != String || type.type != Bool);

        type.visit(
            [&](auto&& t) {
                using T = std::remove_pointer_t<std::decay_t<decltype(t)>>;

                auto* minmax = (uint8_t*)malloc(2 * sizeof(T));
                std::memcpy(minmax, min, sizeof(T));
                std::memcpy(minmax + sizeof(T), max, sizeof(T));

                add_reference(path, type, address,
                              imgui_reflection::Slider{
                                  .min = minmax,
                                  .max = minmax + sizeof(T),
                                  .format = format,
                                  .flags = flags,
                              });
            },
            nullptr);
    }

    void Registry::add_drag_reference(path path, Type type, void* address, void* min, void* max, float speed,
                                      const char* format, uint64_t flags) {
        assert(type.type != String || type.type != Bool);
        assert((min == nullptr && max == nullptr) || (min != nullptr && max != nullptr));

        auto type_size = type.size();

        type.visit(
            [&](auto&& t) {
                using T = std::remove_pointer_t<std::decay_t<decltype(t)>>;

                imgui_reflection::Drag drag{
                    .speed = speed,
                    .min = nullptr,
                    .max = nullptr,
                    .format = format,
                    .flags = flags,
                };
                if (min != nullptr && max != nullptr) {
                    auto* minmax = (uint8_t*)malloc(2 * sizeof(T));
                    std::memcpy(minmax, min, sizeof(T));
                    std::memcpy(minmax + sizeof(T), max, sizeof(T));

                    drag.min = minmax;
                    drag.max = minmax + sizeof(T);
                }

                add_reference(path, type, address, drag);
            },
            nullptr);
    }

    void Registry::add_reference(path path, Type type, void* address, imgui_reflection::InputMethod&& input_method) {
        Var var{std::move(path), type, address, input_method};

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

    void Registry::modified() {
        want_save = true;
    }

    bool Registry::want_to_save() {
        auto res = want_save;
        want_save = false;
        return res;
    }

    void Registry::imgui_ui() {
        std::span<const std::string> prev{};
        size_t imgui_depth = 0;
        size_t blocked_depth = -1;

        for (auto& var : variables) {
            const auto& path = var.path;

            size_t lcp = 0;
            while (lcp < path.segments.size() && lcp < (prev.size() == 0 ? 0 : prev.size() - 1) &&
                   path.segments[lcp] == prev[lcp]) {
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
                if (imgui_draw_var(var)) modified();
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

    void Registry::destroy() {
        for (auto& variable : variables) {
            variable.destroy();
        }
    }

    bool Registry::imgui_draw_var(Var& var) {
        if (var.type.count == 1) ImGui::SameLine();
        else ImGui::Indent();

        bool modified = false;
        var.type.visit(
            [&](auto&& value) {
                using T = std::remove_pointer_t<std::decay_t<decltype(value)>>;

                for (size_t i = 0; i < var.type.count; i++) {
                    ImGui::PushID(i);
                    std::visit(
                        [&](auto&& im) {
                            using IM = std::decay_t<decltype(im)>;
                            if constexpr ((std::same_as<IM, imgui_reflection::Drag> ||
                                           std::same_as<IM, imgui_reflection::Slider>) &&
                                          (std::same_as<T, bool> || std::same_as<T, std::string>)) {
                                assert(false);
                            } else {
                                modified |= imgui_reflection::input("", im, value + i);
                            }
                        },
                        var.input_method);
                    ImGui::PopID();
                }
            },
            var.address);

        if (var.type.count != 1) ImGui::Unindent();

        return modified;
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
