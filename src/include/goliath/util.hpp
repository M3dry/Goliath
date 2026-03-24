#pragma once

#include <cctype>
#include <cmath>
#include <expected>
#include <filesystem>
#include <glm/detail/qualifier.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
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

namespace engine::rendering {
    void begin_mark_block();
    void end_mark_block();
    void mark(const char* name);
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

    template <typename GID> constexpr GID parse_gid2(std::string_view str, std::string_view file_ext) {
        using value_t = std::decay_t<decltype(GID::id_mask)>;
        static constexpr auto hex_digits = [](value_t mask) {
            value_t bits = 0;
            while (mask) {
                bits++;
                mask >>= 1;
            }
            return (bits + 3) / 4;
        };

        constexpr auto id_digits = hex_digits(GID::id_mask);
        constexpr auto gen_digits = hex_digits(GID::gen_mask >> GID::gen_shift);

        constexpr bool has_dim = requires {
            GID::dim_mask;
            GID::dim_shift;
        };
        auto dim_digits = 0;
        if constexpr (has_dim) {
            dim_digits = hex_digits(GID::dim_mask >> GID::dim_shift);
        }

        auto total_digits = id_digits + gen_digits + dim_digits;

        if (str.size() != total_digits + file_ext.size()) return {};
        if (str.substr(total_digits) != file_ext) return {};

        for (size_t i = 0; i < total_digits; i++) {
            if (!std::isxdigit(str[i])) return {};
        }

        const char* p = str.data();

        value_t dim = 0;
        value_t gen = 0;
        value_t id = 0;

        if constexpr (has_dim) {
            auto r = std::from_chars(p, p + dim_digits, dim, 16);
            if (r.ec != std::errc()) return {};
            p += dim_digits;
        }

        {
            auto r = std::from_chars(p, p + gen_digits, gen, 16);
            if (r.ec != std::errc()) return {};
            p += gen_digits;
        }

        {
            auto r = std::from_chars(p, p + id_digits, id, 16);
            if (r.ec != std::errc()) return {};
        }

        if constexpr (has_dim) {
            return GID{dim, gen, id};
        } else {
            return GID{gen, id};
        }
    }

    template <typename GID> constexpr std::string format_gid(GID gid, std::string_view file_ext) {
        using value_t = std::decay_t<decltype(GID::id_mask)>;
        static constexpr auto hex_digits = [](value_t mask) {
            value_t bits = 0;
            while (mask) {
                bits++;
                mask >>= 1;
            }
            return (bits + 3) / 4;
        };

        constexpr auto id_digits = hex_digits(GID::id_mask);
        constexpr auto gen_digits = hex_digits(GID::gen_mask >> GID::gen_shift);

        constexpr bool has_dim = requires {
            GID::dim_mask;
            GID::dim_shift;
        };

        std::string out{};

        if constexpr (has_dim) {
            constexpr auto dim_digits = hex_digits(GID::dim_mask >> GID::dim_shift);
            std::format_to(std::back_inserter(out), "{:0{}X}", gid.gen(), dim_digits);
        }
        std::format_to(std::back_inserter(out), "{:0{}X}{:0{}X}{}", gid.gen(), gen_digits, gid.id(), id_digits, file_ext);

        return out;
    }

    constexpr std::pair<uint32_t, uint32_t> parse_gid(std::string_view str, std::string_view file_ext) {
        if (str.size() != file_ext.size() + 9 || str.substr(9) != file_ext) return {-1, -1};

        for (uint32_t i = 0; i < 8; ++i) {
            if (!std::isxdigit(str[i])) return {-1, -1};
        }

        uint32_t a = -1;
        uint32_t b = -1;

        auto gen = std::from_chars(str.data(), str.data() + 2, a, 16);
        auto id = std::from_chars(str.data() + 2, str.data() + 8, b, 16);

        if (gen.ec != std::errc() || id.ec != std::errc()) return {-1, -1};

        return {a, b};
    }
}

namespace glm {
    inline void to_json(nlohmann::json& j, const glm::vec2& v) {
        j = nlohmann::json::array({v.x, v.y});
    }

    inline void from_json(const nlohmann::json& j, glm::vec2& v) {
        v.x = j.at(0).get<float>();
        v.y = j.at(1).get<float>();
    }

    inline void to_json(nlohmann::json& j, const glm::vec3& v) {
        j = nlohmann::json::array({v.x, v.y, v.z});
    }

    inline void from_json(const nlohmann::json& j, glm::vec3& v) {
        v.x = j.at(0).get<float>();
        v.y = j.at(1).get<float>();
        v.z = j.at(2).get<float>();
    }

    inline void to_json(nlohmann::json& j, const glm::quat& q) {
        j = nlohmann::json::array({q.x, q.y, q.z, q.w});
    }

    inline void from_json(const nlohmann::json& j, glm::quat& q) {
        q.x = j.at(0).get<float>();
        q.y = j.at(1).get<float>();
        q.z = j.at(2).get<float>();
        q.w = j.at(3).get<float>();
    }

    inline void to_json(nlohmann::json& j, const glm::mat4& m) {
        j = nlohmann::json::array();

        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                j.push_back(m[col][row]);
            }
        }
    }

    inline void from_json(const nlohmann::json& j, glm::mat4& m) {
        int k = 0;

        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                m[col][row] = j.at(k++).get<float>();
            }
        }
    }
}
