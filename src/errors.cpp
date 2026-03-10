#include "goliath/errors.hpp"

namespace engine::errors {
    std::vector<Handler> handlers{};

    uint64_t add_handler(Handler&& handler) {
        handlers.emplace_back(handler);
        return handlers.size() - 1;
    }

    void remove_handler(uint64_t id) {
        handlers.erase(handlers.begin() + id);
    }

    void throw_err(ErrType type, const void* err_data) {
        for (auto& handler : handlers) {
            handler(type, err_data);
        }
    };
}
