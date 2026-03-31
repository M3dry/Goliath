#include "goliath/fs.hpp"

#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <unistd.h>
#endif

namespace engine::fs {
    std::filesystem::path get_runtime_dir() {
        static auto path = []() -> std::filesystem::path {
#define PATH_MAX 512
#ifdef _WIN32
            char buffer[MAX_PATH];
            DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
            if (len == 0 || len == MAX_PATH) throw std::runtime_error("GetModuleFileName failed");
            return std::filesystem::absolute(std::filesystem::path(buffer).parent_path());
#elif __linux__
            char buffer[PATH_MAX];
            ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
            if (len == -1) throw std::runtime_error("readlink failed");
            buffer[len] = '\0';
            return std::filesystem::absolute(std::filesystem::path(buffer).parent_path());
#else
#error Unsupported platform
#endif
        }();

        return path;
    }
}
