#pragma once

#include <cstdint>
#include <functional>

#include "goliath/models.hpp"

namespace engine::errors {
    enum ErrType {
        Models_Load,
        Models_Add,
    };

    template <typename F> decltype(auto) visit(F&& f, ErrType type) {
        switch (type) {
            case Models_Load: return f.template operator()<models::LoadError>();
            case Models_Add: return f.template operator()<models::AddError>();
        }
    }

    using Handler = std::function<void(ErrType type, const void* err_data)>;

    uint64_t add_handler(Handler&& handler);
    void remove_handler(uint64_t id);

    void throw_err(ErrType type, const void* err_data);
}
