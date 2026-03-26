#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class InitMenu {
  public:
    InitMenu();

    void draw();

  private:
    std::vector<std::string> project_names;
    std::vector<std::filesystem::path> project_paths;
    std::vector<uint64_t> project_last_used;
};
