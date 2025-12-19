#pragma once

#include "goliath/model.hpp"
#include <glm/ext/matrix_float4x4.hpp>
#include <volk.h>


// <model count><total instance count><external count><names size>{<is external><name metadata><name data|if <name metadata> valid><instance count><instance transforms><model data size><model data> |*model count}
// <model data> = <embedded||GLB||GLTF>(<binary embeded data> || <file path>)
namespace engine {
    struct Scene {
        struct Str {
            uint32_t start{0};
            uint32_t length{0};

            constexpr bool is_valid() const {
                return !(start == 0 && length == 0);
            }
        };

        struct Instances {
            uint32_t start{0};
            uint32_t count{0};
        };

        uint32_t model_count;
        Str* model_names;
        Instances* model_instances;
        Model* models;

        glm::mat4* instance_transforms;

        uint32_t external_count;
        Str* external_names;
        uint32_t* external;

        char* name_data;

        static void load(Scene* out, uint8_t* data, uint32_t size);

        void destroy();
    };

    struct GPUScene {
        struct DrawCommand {
            VkDrawIndirectCommand cmd;
            uint32_t start_offset;
            uint32_t instance_transform_start;
        };

        Buffer draw_indirect;
        uint32_t draw_count;

        static void upload(GPUScene* out, const Scene* scene, VkBufferMemoryBarrier2* barrier);

        void destroy();
    };
}
