#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <variant>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

namespace engine::imgui_reflection {
    enum InputFlags : uint64_t {
        Input_ReadOnly = ImGuiInputTextFlags_ReadOnly,
    };

    enum SliderFlags : uint64_t {
        Slider_Allocated = 1ULL << 33,
    };

    enum DragFlags : uint64_t {
        Drag_Allocated = 1ULL << 33,
    };

    struct Input {
        uint64_t flags = 0;
    };

    struct Slider {
        void* min;
        void* max;
        const char* format = nullptr;
        uint64_t flags = 0;
    };

    struct Drag {
        float speed = 1.0f;
        void* min;
        void* max;
        const char* format = nullptr;
        uint64_t flags = 0;
    };

    template <typename T> inline ImGuiDataType imgui_data_type();

#define HELPER(t, imgui_t)                                                                                             \
    template <> inline ImGuiDataType imgui_data_type<t>() {                                                            \
        return ImGuiDataType_##imgui_t;                                                                                \
    }
    HELPER(int8_t, S8)
    HELPER(int16_t, S16)
    HELPER(int32_t, S32)
    HELPER(int64_t, S64)
    HELPER(uint8_t, U8)
    HELPER(uint16_t, U16)
    HELPER(uint32_t, U32)
    HELPER(uint64_t, U64)
    HELPER(float, Float)
    HELPER(double, Double)
#undef HELPER

    template <typename T> bool input(const char* label, const Input& i, T* value, size_t n = 1) {
        auto data_type = imgui_data_type<T>();

        return ImGui::InputScalarN(label, data_type, value, n);
    }

    template <> inline bool input<bool>(const char* label, const Input& i, bool* value, size_t n) {
        if (i.flags & Input_ReadOnly) {
            for (size_t i = 0; i < n; i++) {
                bool v = ((bool*)value)[i];

                if (i != 0) ImGui::SameLine();
                ImGui::TextColored(v ? ImVec4(41 / 255.0f, 240 / 255.0f, 94 / 255.0f, 1.0f)
                                     : ImVec4(230 / 255.0f, 75 / 255.0f, 55 / 255.0f, 1.0f),
                                   v ? "Enabled" : "Disabled");

                ImGui::SameLine();
                ImGui::TextUnformatted(label);
            }

            return false;
        } else {
            bool modified = false;
            for (size_t i = 0; i < n; i++) {
                ImGui::PushID(i);

                if (i != 0) ImGui::SameLine();
                modified |= ImGui::Checkbox(i == n - 1 ? label : "##checkbox", value);

                ImGui::PopID();
            }

            return modified;
        }
    }

    template <> inline bool input<std::string>(const char* label, const Input& input, std::string* value, size_t n) {
        bool modified = false;

        for (size_t i = 0; i < n; i++) {
            ImGui::PushID(i);
            if (i != 0) ImGui::SameLine();
            modified |= ImGui::InputText(i == n - 1 ? label : "##checkbox", value, input.flags);
            ImGui::PopID();
        }

        return modified;
    }

    template <typename T> inline bool input(const char* label, const Slider& s, T* value, size_t n = 1) {
        auto data_type = imgui_data_type<T>();

        return ImGui::SliderScalarN(label, data_type, value, n, s.min, s.max, s.format, s.flags);
    }

    template <typename T> inline bool input(const char* label, const Drag& d, T* value, size_t n = 1) {
        auto data_type = imgui_data_type<T>();

        return ImGui::DragScalarN(label, data_type, value, n, d.speed, d.min, d.max, d.format, d.flags);
    }

    using InputMethod = std::variant<Input, Slider, Drag>;

    template <typename T>
    inline bool input(const char* label, const InputMethod& im, T* value, std::array<size_t, 2> dimension = {1, 1}) {
        return std::visit(
            [&](auto&& arg) {
                bool modified = false;
                if (dimension[0] != 1 && dimension[1] != 1) {
                    ImGui::BeginGroup();
                    for (size_t n = 0; n < dimension[1]; n++) {
                        ImGui::PushID(n);
                        ImGui::PushMultiItemsWidths(dimension[0], ImGui::CalcItemWidth());

                        for (size_t m = 0; m < dimension[0]; m++) {
                            ImGui::PushID(m);
                            if (m != 0) ImGui::SameLine();

                            auto off = m * dimension[1] + n;
                            modified |= input<T>(n == dimension[1] - 1 && m == dimension[0] - 1 ? label : "", arg, value + off);

                            ImGui::PopID();
                            ImGui::PopItemWidth();
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndGroup();
                } else if (dimension[1] == 1) {
                    modified |= input<T>(label, arg, value, dimension[0]);
                } else if (dimension[0] == 1) {
                    for (size_t i = 0; i < dimension[1]; i++) {
                        ImGui::PushID(i);
                        modified |= input<T>(i == dimension[1] - 1 ? label : "", arg, value + i, 1);
                        ImGui::PopID();
                    }
                }

                return modified;
            },
            im);
    }
}
