#pragma once

#include <functional>

namespace error_stack {
    using Fn = std::function<bool()>;

    void push(Fn&& f);

    void draw_top();
}
