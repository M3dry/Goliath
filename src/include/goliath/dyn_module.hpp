#pragma once

namespace engine::dyn_module {
    struct DynModule;

    DynModule* load();
    void destroy(DynModule* module);
};
