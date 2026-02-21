#pragma once

#include "goliath/assets.hpp"
#include "goliath/engine.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <exception>

namespace engine::game_interface2 {
    struct GameFatalException : public std::exception {
      public:
          explicit GameFatalException(std::string message) : _message(message) {};
          const char* what() const noexcept override {
              return _message.c_str();
          }
      private:
          std::string _message;
    };

    struct EngineService {
        struct AssetsServicePtrs {
            Assets* assets;
            void (*acquire_scene)(Assets* assets, Assets::SceneHandle handle);
            void (*acquire_model)(Assets* assets, Assets::ModelHandle handle);
            void (*acquire_texture)(Assets* assets, Assets::TextureHandle handle);

            void (*release_scene)(Assets* assets, Assets::SceneHandle handle);
            void (*release_model)(Assets* assets, Assets::ModelHandle handle);
            void (*release_texture)(Assets* assets, Assets::TextureHandle handle);
        };

        class AssetsService {
          private:
            AssetsServicePtrs ptrs;

          public:
            AssetsService(AssetsServicePtrs ptrs) : ptrs(ptrs) {}
            AssetsService() {}
            operator AssetsServicePtrs() const {
                return ptrs;
            }

            void acquire(Assets::SceneHandle handle) const {
                return ptrs.acquire_scene(ptrs.assets, handle);
            }

            void acquire(Assets::ModelHandle handle) const {
                return ptrs.acquire_model(ptrs.assets, handle);
            }

            void acquire(Assets::TextureHandle handle) const {
                return ptrs.acquire_texture(ptrs.assets, handle);
            }

            void release(Assets::SceneHandle handle) const {
                return ptrs.release_scene(ptrs.assets, handle);
            }

            void release(Assets::ModelHandle handle) const {
                return ptrs.release_model(ptrs.assets, handle);
            }

            void release(Assets::TextureHandle handle) const {
                return ptrs.release_texture(ptrs.assets, handle);
            }
        };

        struct TexturesServicePtrs {
            std::string* (*name)(textures::gid gid, textures::Err* err, bool* erred);
            GPUImage (*image)(textures::gid gid, textures::Err* err, bool* erred);
            VkImageView (*image_view)(textures::gid gid, textures::Err* err, bool* erred);

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
            Model* (*cpu_model)(models::gid gid, models::Err* err, bool* erred);
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
        ScenesService scenes;

        void (*fatal)(const char* message);
    };

    struct FrameService {
        struct AssetsServicePtrs {
            Assets* assets;
            Assets::ModelDraw (*draw_model)(Assets* assets, Assets::ModelHandle handle);
            void (*end_model_draw)(Assets* assets, Assets::ModelHandle handle);

            Assets::TextureDraw (*draw_texture)(Assets* assets, Assets::TextureHandle handle);
            void (*end_texture_draw)(Assets* assets, Assets::TextureHandle handle);

            assets::SceneIterator* (*draw_scene)(Assets* assets, Assets::SceneHandle handle, transport2::ticket* t,
                                                 uint64_t* transforms_addr);
            scenes::Draw (*scene_next_model)(Assets* assets, assets::SceneIterator* it);
            void (*end_scene_draw)(Assets* assets, assets::SceneIterator* it);
        };

        class AssetsService {
          private:
            AssetsServicePtrs ptrs;

          public:
            AssetsService(AssetsServicePtrs ptrs) : ptrs(ptrs) {}
            AssetsService() {}
            operator AssetsServicePtrs() const {
                return ptrs;
            }

            Assets::ModelDraw draw_model(Assets::ModelHandle handle) const {
                return ptrs.draw_model(ptrs.assets, handle);
            }

            void end_draw_model(Assets::ModelHandle handle) const {
                ptrs.end_model_draw(ptrs.assets, handle);
            }

            Assets::TextureDraw draw_texture(Assets::TextureHandle handle) const {
                return ptrs.draw_texture(ptrs.assets, handle);
            }

            void end_draw_texture(Assets::TextureHandle handle) {
                ptrs.end_texture_draw(ptrs.assets, handle);
            }

            assets::SceneIterator* draw_scene(Assets::SceneHandle handle, transport2::ticket& t,
                                              uint64_t& transforms_addr) const {
                return ptrs.draw_scene(ptrs.assets, handle, &t, &transforms_addr);
            }

            scenes::Draw scene_next_model(assets::SceneIterator* it) const {
                return ptrs.scene_next_model(ptrs.assets, it);
            }

            void end_scene_draw(assets::SceneIterator* it) const {
                ptrs.end_scene_draw(ptrs.assets, it);
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

    EngineService make_engine_service(Assets* assets);
    FrameService make_frame_service(Assets* assets);
    TickService make_tick_service();

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
        uint32_t(void*, const FrameService*, const EngineService*, VkSemaphoreSubmitInfo*);

    struct GameFunctionsPtrs {
        InitFn* init;
        DestroyFn* destroy;
        ResizeFn* resize;

        TickFn* tick;
        DrawImGuiFn* draw_imgui;
        RenderFn* render;
    };

    struct GameFunctions {
        GameFunctionsPtrs game;
        void(*__engine_set_state)(void*);
        void(*__engine_set_swapchain)(ForeignSwapchainState*);
        void(*__engine_set_imgui_context)(ImGuiContext*);
        void(*__engine_set_vk_funcs)();
        void(*__engine_set_transport_state)(void*);
        void(*__engine_set_visbuffer_state)(void*);
        void(*__engine_set_vma_ptrs)(void*);

        static GameFunctions make(GameFunctionsPtrs ptrs);

        void set_state(void* state) {
            __engine_set_state(state);
        }

        void set_swapchain(ForeignSwapchainState* state) {
            __engine_set_swapchain(state);
        }

        void set_imgui_context(ImGuiContext* ctx) {
            __engine_set_imgui_context(ctx);
        }
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
        VkImageLayout target_start_layout;
        VkPipelineStageFlags2 target_start_stage;
        VkAccessFlags2 target_start_access;
        // // if == {0, 0}, then it's the same as the window/viewport size, and ResizeFn has to be defined
        glm::uvec2 target_dimensions;
        BlitStrategy target_blit_strategy;
        glm::vec4 clear_color;

        uint32_t max_wait_count;

        Assets::Inputs asset_inputs;
        GameFunctions funcs;
    };

    using MainFn = GameConfig();

    void start(GameConfig config, const AssetPaths& asset_paths, uint32_t argc, char** argv);
}

#define GAME_INTERFACE_MAIN _goliath_main_
#define GAME_INTERFACE_MAIN_SYM XSTR(GAME_INTERFACE_MAIN)
