#include "goliath/dyn_module.hpp"

#include <expected>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace engine::dyn_module {
    std::expected<DynModule, std::string> load(const char* filename) {
#ifdef _WIN32
        HMODULE mod = LoadLibraryA(filename);
        if (!mod) {
            DWORD err = GetLastError();
            return std::unexpected("LoadLibrary failed with error " + std::to_string(err));
        }
        return reinterpret_cast<DynModule>(mod);
#else
        void* mod = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
        if (!mod) {
            return std::unexpected(dlerror());
        }
        return mod;
#endif
    }

    void destroy(DynModule module) {
#ifdef _WIN32
        if (module) {
            FreeLibrary(reinterpret_cast<HMODULE>(module));
        }
#else
        if (module) {
            dlclose(module);
        }
#endif
    }

    std::expected<void*, std::string> find_sym(const char* name, DynModule mod) {
#ifdef _WIN32
        FARPROC sym = GetProcAddress(reinterpret_cast<HMODULE>(mod), name);
        if (!sym) {
            DWORD err = GetLastError();
            return std::unexpected("GetProcAddress failed with error " + std::to_string(err));
        }
        return reinterpret_cast<void*>(sym);
#else
        void* sym = dlsym(mod, name);
        if (!sym) {
            return std::unexpected(dlerror());
        }
        return sym;
#endif
    }
}
