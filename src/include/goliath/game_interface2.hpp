#pragma once

#include "goliath/assets.hpp"
#include "goliath/material.hpp"
#include "imgui.h"

namespace engine::game_interface2 {
    struct EngineService {
        struct AssetsServicePtrs {
            void (*acquire_scene)(Assets::SceneHandle handle);
            void (*acquire_model)(Assets::ModelHandle handle);
            void (*acquire_texture)(Assets::TextureHandle handle);

            void (*release_scene)(Assets::SceneHandle handle);
            void (*release_model)(Assets::ModelHandle handle);
            void (*release_texture)(Assets::TextureHandle handle);
        };

        class AssetsService {
          private:
            AssetsServicePtrs ptrs;

          public:
            AssetsService(AssetsServicePtrs ptrs) : ptrs(ptrs) {}
            operator AssetsServicePtrs() const {
                return ptrs;
            }

            void acquire(Assets::SceneHandle handle) const {
                return ptrs.acquire_scene(handle);
            }

            void acquire(Assets::ModelHandle handle) const {
                return ptrs.acquire_model(handle);
            }

            void acquire(Assets::TextureHandle handle) const {
                return ptrs.acquire_texture(handle);
            }

            void release(Assets::SceneHandle handle) const {
                return ptrs.release_scene(handle);
            }

            void release(Assets::ModelHandle handle) const {
                return ptrs.release_model(handle);
            }

            void release(Assets::TextureHandle handle) const {
                return ptrs.release_texture(handle);
            }
        };

        struct TexturesServicePtrs {
            std::string* (*name)(textures::gid gid, textures::Err* err, bool* erred);
            GPUImage (*image)(textures::gid gi, textures::Err* err, bool* erred);
            VkImageView (*image_view)(textures::gid gi, textures::Err* err, bool* erred);

            const TexturePool* (*texture_pool)();
        };

        class TexturesService {
          private:
            TexturesServicePtrs ptrs;

          public:
            TexturesService(TexturesServicePtrs ptrs) : ptrs(ptrs) {}
            TexturesService() {}
            operator TexturesServicePtrs() const {
                return ptrs;
            }

            const TexturePool& texture_pool() const {
                return *ptrs.texture_pool();
            }

            std::expected<std::string*, textures::Err> name(textures::gid gid) const {
                bool erred = false;
                textures::Err err;
                auto res = ptrs.name(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<GPUImage, textures::Err> image(textures::gid gid) const {
                bool erred = false;
                textures::Err err;
                auto res = ptrs.image(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<VkImageView, textures::Err> image_view(textures::gid gid) const {
                bool erred = false;
                textures::Err err;
                auto res = ptrs.image_view(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }
        };

        struct MaterialsServicePtrs {
            // const Material* (*schema)(uint32_t mat_id); TODO: Material not ABI safe, probably just make custom std::vector
            uint8_t* (*instance_data)(uint32_t mat_id, uint32_t instance_ix, uint32_t* size);
            Buffer (*buffer)();
            void (*set_instance_data)(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data);
        };

        class MaterialsService {
          private:
            MaterialsServicePtrs ptrs;

          public:
            MaterialsService(MaterialsServicePtrs ptrs) : ptrs(ptrs) {}
            MaterialsService() {}
            operator MaterialsServicePtrs() const {
                return ptrs;
            }

            Buffer buffer() const {
                return ptrs.buffer();
            }

            // const Material& schema(uint32_t mat_id) const {
            //     return *_ptrs.schema(mat_id);
            // }

            std::span<uint8_t> instance_data(uint32_t mat_id, uint32_t instance_ix) const {
                uint32_t size;
                auto data = ptrs.instance_data(mat_id, instance_ix, &size);
                return {data, size};
            }

            void set_instance_data(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data) const {
                ptrs.set_instance_data(mat_id, instance_ix, new_data);
            }
        };

        struct ModelsServicePtrs {
            std::string* (*name)(models::gid gid, models::Err* err, bool* erred);
            engine::Model* (*cpu_model)(models::gid gid, models::Err* err, bool* erred);
            transport2::ticket (*ticket)(models::gid gid, models::Err* err, bool* erred);
            engine::Buffer (*draw_buffer)(models::gid gid, models::Err* err, bool* erred);
            engine::GPUModel (*gpu_model)(models::gid gid, models::Err* err, bool* erred);
            engine::GPUGroup (*gpu_group)(models::gid gid, models::Err* err, bool* erred);
        };

        class ModelsService {
          private:
            ModelsServicePtrs ptrs;

          public:
            ModelsService(ModelsServicePtrs ptrs) : ptrs(ptrs) {}
            ModelsService() {}
            operator ModelsServicePtrs() const {
                return ptrs;
            }

            std::expected<std::string*, models::Err> name(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = ptrs.name(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::Model*, models::Err> cpu_model(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = ptrs.cpu_model(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<transport2::ticket, models::Err> ticket(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = ptrs.ticket(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::Buffer, models::Err> draw_buffer(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = ptrs.draw_buffer(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::GPUModel, models::Err> gpu_model(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = ptrs.gpu_model(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::GPUGroup, models::Err> gpu_group(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = ptrs.gpu_group(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }
        };

        struct ScenesServicePtrs {
            Buffer (*instance_transforms_buffer)(size_t scene_ix, transport2::ticket* ticket);
            const models::gid* (*used_models)(size_t scene_ix, size_t* count);
        };

        class ScenesService {
          private:
            ScenesServicePtrs ptrs;

          public:
            ScenesService(ScenesServicePtrs ptrs) : ptrs(ptrs) {}
            ScenesService() {}
            operator ScenesServicePtrs() const {
                return ptrs;
            }

            Buffer instance_transforms_buffer(size_t scene_ix, transport2::ticket& ticket) {
                return ptrs.instance_transforms_buffer(scene_ix, &ticket);
            }

            std::span<const models::gid> used_models(size_t scene_ix) {
                size_t count;
                auto ptr = ptrs.used_models(scene_ix, &count);
                return {ptr, count};
            }
        };

        AssetsService assets;
        TexturesService textures;
        MaterialsService materials;
        ModelsService models;
    };

    struct FrameService {
        struct AssetsServicePtrs {
            Assets::ModelDraw (*draw_model)(Assets::ModelHandle handle);
            void (*end_draw_model)(Assets::ModelHandle handle);

            Assets::TextureDraw (*draw_texture)(Assets::TextureHandle handle);
            void (*end_draw_texture)(Assets::TextureHandle handle);

            assets::SceneIterator* (*draw_scene)(Assets::SceneHandle handle, transport2::ticket* t,
                                                 uint64_t* transforms_addr);
            scenes::Draw (*scene_next_model)(assets::SceneIterator* it);
            void (*end_scene_draw)(assets::SceneIterator* it);
        };

        class AssetsService {
          private:
            AssetsServicePtrs ptrs;

          public:
            AssetsService(AssetsServicePtrs ptrs) : ptrs(ptrs) {}
            operator AssetsServicePtrs() const {
                return ptrs;
            }

            Assets::ModelDraw draw_model(Assets::ModelHandle handle) const {
                return ptrs.draw_model(handle);
            }

            void end_draw_model(Assets::ModelHandle handle) const {
                ptrs.end_draw_model(handle);
            }

            Assets::TextureDraw draw_texture(Assets::TextureHandle handle) const {
                return ptrs.draw_texture(handle);
            }

            void end_draw_texture(Assets::TextureHandle handle) {
                ptrs.end_draw_texture(handle);
            }

            assets::SceneIterator* draw_scene(Assets::SceneHandle handle, transport2::ticket& t,
                                              uint64_t& transforms_addr) const {
                return ptrs.draw_scene(handle, &t, &transforms_addr);
            }

            scenes::Draw scene_next_model(assets::SceneIterator* it) const {
                return ptrs.scene_next_model(it);
            }

            void end_scene_draw(assets::SceneIterator* it) const {
                ptrs.end_scene_draw(it);
            }
        };

        AssetsService assets;
    };

    struct TickServicePtrs {
        bool (*is_held)(ImGuiKey code);
        bool (*was_released)(ImGuiKey code);

        glm::vec2 (*get_mouse_delta)();
        glm::vec2 (*get_mouse_absolute)();
    };

    class TickService {
      private:
        TickServicePtrs _ptrs;

      public:
        TickService(TickServicePtrs ptrs) : _ptrs(ptrs) {}
        TickService() {}
        operator TickServicePtrs() const {
            return _ptrs;
        }

        bool is_held(ImGuiKey code) const {
            return _ptrs.is_held(code);
        }

        bool was_released(ImGuiKey code) const {
            return _ptrs.was_released(code);
        }

        glm::vec2 get_mouse_delta() const {
            return _ptrs.get_mouse_delta();
        }

        glm::vec2 get_mouse_absolute() const {
            return _ptrs.get_mouse_absolute();
        }
    };

    void make_engine_service(EngineService&);
    void make_frame_service(FrameService&);
    void make_tick_service(TickService&);

    void update_frame_service(FrameService&, uint32_t current_frame);

    struct AssetPaths {
        const char* asset_inputs;

        const char* scenes;
        const char* materials;
        const char* models_reg;
        const char* models_dir;

        const char* textures_reg;
        const char* textures_dir;
    };

    using InitFn = void*(const EngineService*, uint32_t, char**);
    using DestroyFn = void(void*, const EngineService*);
    using ResizeFn = void(void*, EngineService*);
    using TickFn = void(void*, const TickService*, const EngineService*);
    using DrawImGuiFn = void(void*, const EngineService*);
    using RenderFn =
        uint32_t(void*, VkCommandBuffer, const FrameService*, const EngineService*, VkSemaphoreSubmitInfo*);

    namespace __ {
        void* engine_init_fn();
    }

    struct GameFunctions {
        InitFn* init;
        DestroyFn* destroy;
        ResizeFn* resize;

        TickFn* tick;
        DrawImGuiFn* draw_imgui;
        RenderFn* render;

        void* __engine_init = __::engine_init_fn();
    };

    struct GameConfig {
        enum BlitStrategy {
            LetterBox,
            Stretch,
        };

        const char* name;
        uint32_t tps;
        bool fullscreen;
        VkImageUsageFlags target_usage;
        VkFormat target_format;
        VkImageLayout target_create_layout;
        // // if == {0, 0}, then it's the same as the window/viewport size, and ResizeFn has to be defined
        glm::uvec2 target_dimensions;
        BlitStrategy target_blit_strategy;
        glm::vec4 clear_color;

        uint32_t max_wait_count;

        Assets::Inputs asset_inputs;
        GameFunctions funcs;
    };

    void start(const GameConfig& config, const AssetPaths& asset_paths, uint32_t argc, char** argv);
}
