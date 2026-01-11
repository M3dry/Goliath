#pragma once

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

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
        ComponentType component_type;
        size_t component_count;

        constexpr bool operator==(const Type& other) const = default;

        template <typename F> decltype(auto) visit(F&& f, void* address) const {
#define HELPER(component_type, type)                                                                                   \
    case component_type: return f(*(type*)address)
            switch (component_type) {
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
    };

    namespace __ {
        template <typename T> struct to_ComponentType;
#define TO_COMPONENTTYPE_HELPER(type, component_type)                                                                  \
    template <> struct to_ComponentType<type> { \
        static constexpr ComponentType value = component_type; \
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

        template <typename T> constexpr Type to_Type_v = to_Type<T>::value;
    }

    struct path {
        std::string path_str;
        std::vector<std::string_view> segments;

        path() {}
        path(std::string str);
    };

    void to_json(nlohmann::json& j, const path& sampler);
    void from_json(const nlohmann::json& j, path& sampler);

    class Registry {
      public:
        void add_reference(path path, Type type, void* address);
        template <typename T> void add_reference(path path, void* address) {
            add_reference(path, __::to_Type_v<T>, address);
        }

        void override(nlohmann::json j);
        nlohmann::json save() const;

        void imgui_ui();
        std::span<path> get_paths();
        std::span<Type> get_types();
        std::span<void*> get_addresses();
      private:
        std::vector<path> paths;
        std::vector<Type> types;
        std::vector<void*> addresses;
    };
}


#define EXVAR(path, type, var_name, default_value) do { \
        static type var_name{default_value}; \
        ::engine::exvar::add_reference<type>(path, &var_name)  \
    } while (0)
