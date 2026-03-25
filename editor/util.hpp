#pragma once

#include "goliath/dependency_graph.hpp"

void remove_asset(engine::DependencyGraph::AssetGID gid);

uint32_t pick_id_3x3(const uint32_t* a);
