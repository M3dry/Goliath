#pragma once

#include "GLFW/glfw3.h"
#include <cmath>
#include <expected>
#include <filesystem>
#include <glm/detail/qualifier.hpp>
#include <nlohmann/json.hpp>

#include <volk.h>

#define XSTR(x) STR(x)
#define STR(x) #x

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

namespace engine {
    enum struct CompareOp {
        Never = VK_COMPARE_OP_NEVER,
        Always = VK_COMPARE_OP_ALWAYS,
        Equal = VK_COMPARE_OP_EQUAL,
        NotEqual = VK_COMPARE_OP_NOT_EQUAL,
        Greater = VK_COMPARE_OP_GREATER,
        Less = VK_COMPARE_OP_LESS,
        GreaterOrEqual = VK_COMPARE_OP_GREATER_OR_EQUAL,
        LessOrEqual = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

}

namespace engine::util {
    struct padding64 {
        uint64_t _;
    };

    struct padding32 {
        uint32_t _;
    };

    struct padding16 {
        uint16_t _;
    };

    struct padding8 {
        uint8_t _;
    };

    template <typename T> struct is_padding : std::false_type {};
    template <> struct is_padding<padding64> : std::true_type {};
    template <> struct is_padding<padding32> : std::true_type {};
    template <> struct is_padding<padding16> : std::true_type {};
    template <> struct is_padding<padding8> : std::true_type {};

    uint8_t* read_file(const std::filesystem::path& path, uint32_t* size);
    void save_file(const std::filesystem::path& path, uint8_t* data, uint32_t size);

    enum struct ReadJsonErr {
        ParseErr,
        FileErr,
    };

    std::expected<nlohmann::json, ReadJsonErr> read_json(const std::filesystem::path& path);

    // `alignment` needs to be a power of two
    uint32_t constexpr align_up(uint32_t alignment, uint32_t size) {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    template <typename T> struct is_vec : std::false_type {};
    template <size_t N, typename T> struct is_vec<glm::vec<N, T>> : std::true_type {};
    template <typename T> static constexpr bool is_vec_v = is_vec<T>::value;

    template <typename T> struct vec_data;
    template <size_t N, typename T> struct vec_data<glm::vec<N, T>> {
        using Component = T;
        static constexpr size_t dimension = N;
    };

    template <typename T> struct is_mat : std::false_type {};
    template <size_t N, size_t M, typename T> struct is_mat<glm::mat<N, M, T>> : std::true_type {};
    template <typename T> static constexpr bool is_mat_v = is_mat<T>::value;

    template <typename T> struct mat_data;
    template <size_t N, size_t M, typename T> struct mat_data<glm::mat<N, M, T>> {
        using Component = T;
        static constexpr std::array<size_t, 2> dimension = {N, M};
    };
}
