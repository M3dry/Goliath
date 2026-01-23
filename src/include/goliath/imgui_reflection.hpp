#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <variant>

#include "imgui.h"
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

#define HELPER(t, imgui_t) template <> inline ImGuiDataType imgui_data_type<t>() { return ImGuiDataType_ ## imgui_t; }
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

    template <typename T>
    bool input(const char* label, const Input& i, T* value) {
        auto data_type = imgui_data_type<T>();

        return ImGui::InputScalar(label, data_type, value);
    }

    template <>
    inline bool input<bool>(const char* label, const Input& i, bool* value) {
        if (i.flags & Input_ReadOnly) {
            ImGui::TextColored(*(bool*)value ? ImVec4(41 / 255.0f, 240 / 255.0f, 94 / 255.0f, 1.0f)
                                             : ImVec4(230 / 255.0f, 75 / 255.0f, 55 / 255.0f, 1.0f),
                               *(bool*)value ? "Enabled" : "Disabled");
            return false;
        } else {
            return ImGui::Checkbox(label, value);
        }
    }

    template <>
    inline bool input<std::string>(const char* label, const Input& i, std::string* value) {
        return ImGui::InputText("##input", value, i.flags);
    }

    template <typename T>
    inline bool input(const char* label, const Slider& s, T* value) {
        auto data_type = imgui_data_type<T>();

        return ImGui::SliderScalar(label, data_type, value, s.min, s.max, s.format, s.flags);
    }

    template <typename T>
    inline bool input(const char* label, const Drag& d, T* value) {
        auto data_type = imgui_data_type<T>();

        return ImGui::DragScalar(label, data_type, value, d.speed, d.min, d.max, d.format, d.flags);
    }
    
    using InputMethod = std::variant<Input, Slider, Drag>;

    template <typename T>
    inline bool input(const char* label, const InputMethod& im, T* value) {
        std::visit([&](auto&& arg){
            input<T>(label, arg, value);
        }, im);
    }
}
