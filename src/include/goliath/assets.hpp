#pragma once

#include "goliath/models.hpp"
#include "goliath/scenes.hpp"
#include <cstdint>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <vector>

namespace engine::assets {
    struct SceneIterator;
}
namespace engine {
    class Assets {
      public:
        struct SceneHandle {
            uint64_t n;
        };

        struct ModelHandle {
            uint64_t n;
        };

        struct TextureHandle {
            uint64_t n;
        };

        struct SceneAssetEntry {
            const char* name;
            SceneHandle* handle;
        };

        struct ModelAssetEntry {
            const char* name;
            ModelHandle* handle;
        };

        struct TextureAssetEntry {
            const char* name;
            TextureHandle* handle;
        };

        struct Inputs {
            SceneAssetEntry* scenes;
            size_t scenes_size;
            ModelAssetEntry* models;
            size_t models_size;
            TextureAssetEntry* textures;
            size_t textures_size;

            Inputs(std::vector<SceneAssetEntry> scenes,  std::vector<ModelAssetEntry> models, std::vector<TextureAssetEntry> textures) : scenes_size(scenes.size()), models_size(models.size()), textures_size(textures.size()) {
                this->scenes = (SceneAssetEntry*)malloc(sizeof(SceneAssetEntry)*scenes_size);
                std::memcpy(this->scenes, scenes.data(), scenes_size);

                this->models = (ModelAssetEntry*)malloc(sizeof(ModelAssetEntry)*models_size);
                std::memcpy(this->models, models.data(), models_size);

                this->textures = (TextureAssetEntry*)malloc(sizeof(TextureAssetEntry)*textures_size);
                std::memcpy(this->textures, textures.data(), textures_size);
            }

            void destroy() {
                free(scenes);
                free(models);
                free(textures);

                scenes_size = 0;
                models_size = 0;
                textures_size = 0;
            }
        };

        static Assets init(Inputs& assets);
        void destroy();

        void load(const nlohmann::json& j);
        nlohmann::json save();
        static nlohmann::json default_json();

        void acquire(SceneHandle handle);
        void acquire(ModelHandle handle);
        void acquire(TextureHandle handle);
        void release(SceneHandle handle);
        void release(ModelHandle handle);
        void release(TextureHandle handle);

        struct ModelDraw {
            models::gid gid;

            operator bool() {
                return gid != models::gid{};
            }
        };
        // if returned value true, draw model and call end_model_draw
        ModelDraw draw_model(ModelHandle handle);
        void end_model_draw(ModelHandle handle);

        struct TextureDraw {
            textures::gid gid;
            GPUImage image;

            operator bool() {
                return gid == textures::gid{};
            }
        };
        // if -1, texture isn't usable
        TextureDraw draw_texture(TextureHandle handle);
        void end_texture_draw(TextureHandle handle);

        assets::SceneIterator* draw_scene(SceneHandle handle, transport2::ticket& t, uint64_t& transforms_addr);
        scenes::Draw scene_next_model(assets::SceneIterator* it);
        void end_scene_draw(assets::SceneIterator* it);

        void set(size_t ix, size_t gid);
        void set(size_t ix, models::gid gid);
        void set(size_t ix, textures::gid gid);

        std::span<const std::string> get_scene_names() const;
        std::span<const std::string> get_model_names() const;
        std::span<const std::string> get_texture_names() const;

        std::span<const size_t> get_scene_gids() const;
        std::span<const models::gid> get_model_gids() const;
        std::span<const textures::gid> get_texture_gids() const;

      private:
        uint32_t models_start;
        uint32_t textures_start;

        std::vector<std::string> names;
        std::vector<uint16_t> ref_counts;
        std::vector<bool> in_draw;

        std::vector<size_t> scene_gids;
        std::vector<models::gid> model_gids;
        std::vector<textures::gid> texture_gids;
    };
}
