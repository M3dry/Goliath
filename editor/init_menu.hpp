#pragma once

#include "goliath/game_interface2.hpp"
#include <filesystem>

extern std::optional<std::filesystem::path> selected_project;
engine::game_interface2::GameConfig init_menu();
