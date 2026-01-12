#include "goliath/exvar.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <type_traits>

namespace engine::exvar {
    void imgui_input_method(Registry::Input i, ComponentType ct, void* value) {
        uint32_t imgui_flags = (uint32_t)i.flags;

#define HELPER(ct_type, imgui_type)                                                                                    \
    case ct_type: ImGui::InputScalar("##input", imgui_type, value, NULL, NULL, NULL, imgui_flags); break

        switch (ct) {
            HELPER(Int8, ImGuiDataType_S8);
            HELPER(Int16, ImGuiDataType_S16);
            HELPER(Int32, ImGuiDataType_S32);
            HELPER(Int64, ImGuiDataType_S64);
            HELPER(UInt8, ImGuiDataType_U8);
            HELPER(UInt16, ImGuiDataType_U16);
            HELPER(UInt32, ImGuiDataType_U32);
            HELPER(UInt64, ImGuiDataType_U64);
            HELPER(Float, ImGuiDataType_Float);
            HELPER(Double, ImGuiDataType_Double);
            case Bool: {
                auto read_only = imgui_flags & (uint32_t)InputFlags::ReadOnly;

                if (read_only) {
                    ImGui::TextColored(*(bool*)value ? ImVec4(41 / 255.0f, 240 / 255.0f, 94 / 255.0f, 1.0f)
                                                     : ImVec4(230 / 255.0f, 75 / 255.0f, 55 / 255.0f, 1.0f),
                                       *(bool*)value ? "Enabled" : "Disabled");
                } else {
                    ImGui::Checkbox("##input", (bool*)value);
                }
                break;
            }
            case String: ImGui::InputText("##input", (std::string*)value, imgui_flags); break;
        }
#undef HELPER
    }

    void imgui_input_method(Registry::Slider slider, ComponentType ct, void* value) {
#define HELPER(ct_type, imgui_type)                                                                                    \
    case ct_type: ImGui::SliderScalar("##input", imgui_type, value, slider.min, slider.max); break

        switch (ct) {
            HELPER(Int8, ImGuiDataType_S8);
            HELPER(Int16, ImGuiDataType_S16);
            HELPER(Int32, ImGuiDataType_S32);
            HELPER(Int64, ImGuiDataType_S64);
            HELPER(UInt8, ImGuiDataType_U8);
            HELPER(UInt16, ImGuiDataType_U16);
            HELPER(UInt32, ImGuiDataType_U32);
            HELPER(UInt64, ImGuiDataType_U64);
            HELPER(Float, ImGuiDataType_Float);
            HELPER(Double, ImGuiDataType_Double);
            case Bool:
            case String: assert(false);
        }

#undef HELPER
    }

    void imgui_input_method(Registry::Drag drag, ComponentType ct, void* value) {
#define HELPER(ct_type, imgui_type)                                                                                    \
    case ct_type: ImGui::DragScalar("##input", imgui_type, value, 1.0f, drag.min, drag.max); break

        switch (ct) {
            HELPER(Int8, ImGuiDataType_S8);
            HELPER(Int16, ImGuiDataType_S16);
            HELPER(Int32, ImGuiDataType_S32);
            HELPER(Int64, ImGuiDataType_S64);
            HELPER(UInt8, ImGuiDataType_U8);
            HELPER(UInt16, ImGuiDataType_U16);
            HELPER(UInt32, ImGuiDataType_U32);
            HELPER(UInt64, ImGuiDataType_U64);
            HELPER(Float, ImGuiDataType_Float);
            HELPER(Double, ImGuiDataType_Double);
            case Bool:
            case String: assert(false);
        }

#undef HELPER
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

    void Registry::add_input_reference(path path, Type type, void* address, InputFlags flags) {
        add_reference(path, type, address, Input{flags});
    }

    void Registry::add_slider_reference(path path, Type type, void* address, void* min, void* max, SliderFlags flags) {
        assert(type.type != String || type.type != Bool);

        type.visit(
            [&](auto&& t) {
                using T = std::decay_t<decltype(t)>;

                auto slider = Slider{*(T)min, *(T)max};
                slider.flags = flags;

                add_reference(path, type, address, slider);
            },
            nullptr);
    }

    void Registry::add_drag_reference(path path, Type type, void* address, void* min, void* max, DragFlags flags) {
        assert(type.type != String || type.type != Bool);
        assert((min == nullptr && max == nullptr) || (min != nullptr && max != nullptr));

        auto type_size = type.size();

        type.visit(
            [&](auto&& t) {
                using T = std::decay_t<decltype(t)>;

                Drag drag{};
                if (min != nullptr && max != nullptr) {
                    drag = Drag{*(T)min, *(T)max};
                }
                drag.flags = flags;

                add_reference(path, type, address, drag);
            },
            nullptr);
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
                imgui_draw_var(var);
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

    void Registry::add_reference(path path, Type type, void* address, InputMethod input_method) {
        Var var{std::move(path), type, address, input_method};

        auto it = std::lower_bound(variables.begin(), variables.end(), var,
                                   [](const Var& a, const Var& b) { return a.path.segments < b.path.segments; });

        if (it != variables.end() && it->path.path_str == var.path.path_str) {
            assert(false && "Duplicate exvar path");
            return;
        }

        variables.insert(it, std::move(var));
    }

    void Registry::imgui_draw_var(Var& var) {
        if (var.type.count == 1) ImGui::SameLine();
        else ImGui::Indent();

        var.type.visit(
            [&](auto&& value) {
                for (size_t i = 0; i < var.type.count; i++) {
                    ImGui::PushID(i);
                    std::visit([&](auto&& im) { imgui_input_method(im, var.type.type, value + i); }, var.input_method);
                    ImGui::PopID();
                }
            },
            var.address);

        if (var.type.count != 1) ImGui::Unindent();
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
