#pragma once

#include "goliath/samplers.hpp"
#include "goliath/texture.hpp"
#include "goliath/texture_pool.hpp"
#include <cstdint>
#include <deque>
#include <expected>

#include <nlohmann/json.hpp>

namespace engine::textures {
    struct TexturesImpl;

    enum struct Err {
        BadGeneration,
    };
}

namespace engine {
    class Textures {
      public:
        struct gid {
            uint32_t value;

            static constexpr uint32_t id_mask = 0x00FF'FFFFu;
            static constexpr uint32_t gen_mask = 0xFF00'0000u;
            static constexpr uint32_t gen_shift = 24;

            gid() : value(-1) {}
            gid(uint32_t generation, uint32_t id) : value((id & id_mask) | ((generation & 0xFFu) << gen_shift)) {}

            uint32_t id() const {
                return value & id_mask;
            }

            uint32_t gen() const {
                return (value & gen_mask) >> gen_shift;
            }

            bool operator==(gid other) const {
                return value == other.value;
            }
        };

        ~Textures();

        static Textures* make(const char* textures_directry, size_t texture_capacity = 1000) {
            return new Textures{textures_directry, texture_capacity};
        }

        void load(nlohmann::json j);
        nlohmann::json save() const;

        gid add(std::filesystem::path path, std::string name, Sampler sampler);
        gid add(std::span<uint8_t> image, uint32_t width, uint32_t height, VkFormat format, std::string name, Sampler sampler);
        bool remove(gid gid);

        std::expected<std::string*, textures::Err> get_name(gid gid);
        std::expected<VkImage, textures::Err> get_image(gid gid);
        std::expected<VkImageView, textures::Err> get_image_view(gid gid);
        std::expected<uint32_t, textures::Err> get_sampler(gid gid);

        uint8_t get_generation(uint32_t ix) const;

        void acquire(std::span<const gid> gids);
        void release(std::span<const gid> gids);

        const TexturePool& get_texture_pool() const;
        std::span<std::string> get_names();

        bool want_to_save();
        void modified();

        void process_uploads();
      private:
        Textures(const char* textures_directry, size_t texture_capacity = 1000);

        bool want_save = false;
        std::filesystem::path texture_directory{};

        TexturePool texture_pool;

        std::vector<std::string> names{};
        std::vector<uint8_t> generations{};
        std::vector<bool> deleted{};

        std::vector<uint32_t> ref_counts{};
        std::vector<GPUImage> gpu_images{};
        std::vector<VkImageView> gpu_image_views{};
        std::vector<uint32_t> samplers{};

        std::deque<std::pair<transport2::ticket, Textures::gid>> finalize_queue{};

        std::optional<Textures::gid> find_empty_gid() {
            for (uint32_t i = 0; i < deleted.size(); i++) {
                if (deleted[i]) {
                    return gid{generations[i], i};
                }
            }

            return std::nullopt;
        }

        void set_default_texture(gid gid) {
            if (generations[gid.id()] != gid.gen()) return;

            texture_pool.update(gid.id(), gpu_image_views[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                samplers::get(samplers[0]));
        }

        void rebuild_pool();

        friend struct textures::TexturesImpl;
        textures::TexturesImpl* impl;
    };

    void to_json(nlohmann::json& j, const Textures::gid& gid);
    void from_json(const nlohmann::json& j, Textures::gid& gid);
}
