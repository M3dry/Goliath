#include "goliath/game_interface2.hpp"
#include "engine_.hpp"

namespace engine::game_interface2 {
    namespace __ {
        void* engine_init_fn() {
            return (void*)engine::share_load;
        }
    }
}
