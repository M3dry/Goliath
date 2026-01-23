#pragma once

#include "goliath/imgui_reflection.hpp"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace engine::exvar {
    enum ComponentType {
        Int8,
        Int16,
        Int32,
        Int64,
        UInt8,
        UInt16,
        UInt32,
        UInt64,
        Float,
        Double,
        Bool,
        String
    };

    struct Type {
        ComponentType type;
        size_t count;

        constexpr bool operator==(const Type& other) const = default;

        template <typename F> decltype(auto) visit(F&& f, void* address) const {
#define HELPER(component_type, type)                                                                                   \
    case component_type: return f((type*)address)

            switch (type) {
                HELPER(Int8, int8_t);
                HELPER(Int16, int16_t);
                HELPER(Int32, int32_t);
                HELPER(Int64, int64_t);

                HELPER(UInt8, uint8_t);
                HELPER(UInt16, uint16_t);
                HELPER(UInt32, uint32_t);
                HELPER(UInt64, uint64_t);

                HELPER(Float, float);
                HELPER(Double, double);

                HELPER(Bool, bool);
                HELPER(String, std::string);
                default: assert(false && "bad component type");
            }
        }
#undef HELPER

        size_t size() {
            return visit(
                [&](auto&& t) {
                    using T = std::remove_pointer_t<std::decay_t<decltype(t)>>;
                    return sizeof(T) * count;
                },
                nullptr);
        }
    };

    namespace __ {
        template <typename T> struct to_ComponentType;
#define TO_COMPONENTTYPE_HELPER(type, component_type)                                                                  \
    template <> struct to_ComponentType<type> {                                                                        \
        static constexpr ComponentType value = component_type;                                                         \
    }

        TO_COMPONENTTYPE_HELPER(int8_t, Int8);
        TO_COMPONENTTYPE_HELPER(int16_t, Int16);
        TO_COMPONENTTYPE_HELPER(int32_t, Int32);
        TO_COMPONENTTYPE_HELPER(int64_t, Int64);

        TO_COMPONENTTYPE_HELPER(uint8_t, UInt8);
        TO_COMPONENTTYPE_HELPER(uint16_t, UInt16);
        TO_COMPONENTTYPE_HELPER(uint32_t, UInt32);
        TO_COMPONENTTYPE_HELPER(uint64_t, UInt64);

        TO_COMPONENTTYPE_HELPER(float, Float);
        TO_COMPONENTTYPE_HELPER(double, Double);

        TO_COMPONENTTYPE_HELPER(bool, Bool);
        TO_COMPONENTTYPE_HELPER(std::string, String);

#undef TO_COMPONENTTYPE_HELPER
        template <typename T> constexpr ComponentType to_ComponentType_v = to_ComponentType<T>::value;

        template <typename T> struct to_Type {
            static constexpr Type value = Type{to_ComponentType_v<T>, 1};
        };
        template <typename T, size_t N> struct to_Type<glm::vec<N, T>> {
            static constexpr Type value = Type{to_ComponentType_v<T>, N};
        };
        template <typename T, size_t N> struct to_Type<std::array<T, N>> {
            static constexpr Type value = Type{to_ComponentType_v<T>, N};
        };

        template <typename T> constexpr Type to_Type_v = to_Type<T>::value;
    }

    struct path {
        std::string path_str;
        std::vector<std::string> segments;

        path() {}
        path(std::string str);
        path(const char*);
    };

    void to_json(nlohmann::json& j, const path& p);
    void from_json(const nlohmann::json& j, path& p);

    class Registry {
      public:
        struct Var {
            path path;
            Type type;
            void* address;
            imgui_reflection::InputMethod input_method;

            void destroy();
        };

        Registry();

        void add_input_reference(path path, Type type, void* address, uint64_t flags = 0);
        template <typename T> void add_input_reference(path path, T* address, uint64_t flags = 0) {
            add_input_reference(path, __::to_Type_v<T>, address, flags);
        }

        // min and max has the same type as address, allocates own copy of min and max
        void add_slider_reference(path path, Type type, void* address, void* min, void* max,
                                  const char* format = nullptr, uint64_t flags = 0);
        template <typename T>
        void add_slider_reference(path path, T* address, T min, T max, const char* format = nullptr,
                                  uint64_t flags = 0) {
            using T_ = std::remove_cvref_t<T>;
            static_assert(!std::is_same_v<std::string, T_> && !std::is_same_v<bool, T_>);

            add_slider_reference(path, __::to_Type_v<T>, address, &min, &max, format, flags);
        }

        // min and max has the same type as address, allocates own copy of min and max, can be nullptr
        void add_drag_reference(path path, Type type, void* address, void* min, void* max, float speed = 1.0f,
                                const char* format = nullptr, uint64_t flags = 0);
        template <typename T>
        void add_drag_reference(path path, T* address, std::optional<T> min = std::nullopt,
                                std::optional<T> max = std::nullopt, float speed = 1.0f, const char* format = nullptr,
                                uint64_t flags = 0) {
            if (max && min) {
                add_drag_reference(path, __::to_Type_v<T>, address, &*min, &*max, speed, format, flags);
            } else {
                add_drag_reference(path, __::to_Type_v<T>, address, nullptr, nullptr, speed, format, flags);
            }
        }

        void add_reference(path path, Type type, void* address, imgui_reflection::InputMethod&& input_method);

        void override(nlohmann::json j);
        nlohmann::json save() const;

        void modified();
        bool want_to_save();

        void imgui_ui();
        std::span<Var> get_variables();

        void destroy();
      private:
        bool want_save = false;
        std::vector<Var> variables;

        static bool imgui_draw_var(Var& var);
    };

    void to_json(nlohmann::json& j, const Registry::Var& var);
}

#define EXVAR_INPUT(registry, path, type, var_name, default_value, ...)                                                \
    static type var_name default_value;                                                                                \
    (registry).add_input_reference<type>(path, &var_name __VA_OPT__(, ) __VA_ARGS__)

#define EXVAR_SLIDER(registry, path, type, var_name, default_value, min, max, ...)                                     \
    static type var_name default_value;                                                                                \
    (registry).add_slider_reference<type>(path, &var_name, min, max __VA_OPT__(, ) __VA_ARGS__)

#define EXVAR_DRAG(registry, path, type, var_name, default_value, ...)                                                 \
    static type var_name default_value;                                                                                \
    (registry).add_drag_reference<type>(path, &var_name __VA_OPT__(, ) __VA_ARGS__)
