#pragma once

#include <cstdint>

struct AssetPaths {
    const char* scenes;
    const char* materials;
    const char* models_reg;
    const char* models_dir;

    const char* textures_reg;
    const char* textures_dir;
};

using InitFn = void(const AssetPaths*);
using DestroyFn = void();
using TickFn = void();
using RenderFn = void();
using DrawExVarsFn = void();

struct Game {
    uint32_t tps;

    InitFn* init;
    DestroyFn* destroy;

    TickFn* tick;
    RenderFn* render;

    DrawExVarsFn* draw_exvars;
};
