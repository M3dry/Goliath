#pragma once

#include <expected>
#include <string>

namespace engine::dyn_module {
    using DynModule = void*;

    std::expected<DynModule, std::string> load(const char* filename);
    void destroy(DynModule module);

    std::expected<void*, std::string> find_sym(const char* name, DynModule mod);
};
