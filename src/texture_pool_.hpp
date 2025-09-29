#pragma once

#include <volk.h>

namespace engine::texture_pool {
    void init(uint32_t tex_count);
    void destroy();
}
