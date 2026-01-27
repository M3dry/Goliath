#pragma once

#include <volk.h>

namespace engine::materials {
    void init();
    void destroy();

    bool update_gpu_buffer();
}
