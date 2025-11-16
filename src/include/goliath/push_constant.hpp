#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace engine::push_constant {
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
}

namespace engine::push_constant::__ {
    template <typename T> struct is_padding : std::false_type {};

    template <> struct is_padding<padding64> : std::true_type {};

    template <> struct is_padding<padding32> : std::true_type {};

    template <> struct is_padding<padding16> : std::true_type {};

    template <> struct is_padding<padding8> : std::true_type {};

    template <typename... T> struct count_padding;
    ;

    template <typename T, typename... Ts> struct count_padding<T, Ts...> {
        static constexpr uint32_t value = (is_padding<T>::value ? 1 : 0) + count_padding<Ts...>::value;
    };

    template <> struct count_padding<> {
        static constexpr uint32_t value = 0;
    };

    template <std::size_t offset> void write(void* out) {}

    template <std::size_t offset, typename T, typename... Ts, typename Arg, typename... Args> void write(void* out, Arg&& arg, Args&&... args) {
        if constexpr (is_padding<T>::value) {
            write<offset + sizeof(T), Ts...>(out, std::forward<Arg>(arg), std::forward<Args>(args)...);
        } else {
            static_assert(std::is_convertible_v<std::remove_cvref_t<Arg>, T>);

            auto arg_ = static_cast<T>(arg);
            std::memcpy((uint8_t*)out + offset, &arg_, sizeof(T));
            write<offset + sizeof(T), Ts...>(out, std::forward<Args>(args)...);
        }
    }
}

namespace engine {
    template <typename... T> struct PushConstant {
        static constexpr uint32_t size = (0 + ... + sizeof(T));

        template <typename... Args>
            requires(sizeof...(Args) == (sizeof...(T) - push_constant::__::count_padding<T...>::value))
        static constexpr void write(void* out, Args&&... args) {
            push_constant::__::write<0, T...>(out, std::forward<Args>(args)...);
        }
    };
}
