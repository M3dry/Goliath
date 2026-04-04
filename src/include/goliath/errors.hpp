#pragma once

#include <cstdint>
#include <functional>

#include "goliath/models.hpp"

namespace engine::errors {
    enum ErrType {
        Models_Load,
        Textures_Load,
        Custom
    };

    template <typename F, typename Custom_T = void*> decltype(auto) visit(F&& f, ErrType type) {
        switch (type) {
            case Models_Load: return f.template operator()<models::LoadError>();
            case Textures_Load: return f.template operator()<Textures::LoadError>();
            case Custom: return f.template operator()<Custom_T>();
            // case Models_Add: return f.template operator()<models::AddError>();
        }
    }

    using Handler = std::function<void(ErrType type, const void* err_data)>;

    uint64_t add_handler(Handler&& handler);
    void remove_handler(uint64_t id);

    void throw_err(ErrType type, const void* err_data);
}
