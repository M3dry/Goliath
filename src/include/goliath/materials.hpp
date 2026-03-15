#pragma once

#include "goliath/buffer.hpp"
#include "goliath/material.hpp"
#include "goliath/util.hpp"

#include <mutex>
#include <nlohmann/json.hpp>

namespace engine {
    class Materials {
      public:
        struct gid {
            uint64_t value;

            static constexpr uint64_t id_mask = 0x0000'0000'FFFF'FFFFu;
            static constexpr uint64_t gen_mask = 0x00FF'FFFF'0000'0000u;
            static constexpr uint64_t dim_mask = 0xFF00'0000'0000'0000u;
            static constexpr uint64_t gen_shift = 32;
            static constexpr uint64_t dim_shift = 56;

            gid() : value(-1) {}
            gid(uint64_t dimension, uint64_t generation, uint64_t id)
                : value((id & id_mask) | ((generation & 0xFFFF'FFFFu) << gen_shift) |
                        (((uint64_t)dimension & 0xFF) << dim_shift)) {}

            uint32_t id() const {
                return value & id_mask;
            }

            uint32_t gen() const {
                return (value & gen_mask) >> gen_shift;
            }

            uint32_t dim() const {
                return (value & dim_mask) >> dim_shift;
            }

            bool operator==(gid other) const {
                return value == other.value;
            }
        };

        static void to_json(nlohmann::json& j, const gid& gid);
        static void from_json(const nlohmann::json& j, gid& gid);

        static std::expected<Materials*, util::ReadJsonErr> init(const nlohmann::json& j);
        ~Materials();

        nlohmann::json save();
        static nlohmann::json default_json();

        void process();

        uint32_t add_schema(Material schema, std::string name);
        bool remove_schema(uint32_t mat_id);
        std::optional<Material> get_schema(uint32_t mat_id);

        std::vector<uint8_t> get_instance_data(gid gid);
        void update_instance_data(gid gid, uint8_t* new_data);
        gid add_instance(uint32_t mat_id, std::string name, std::span<uint8_t> data);
        bool remove_instance(gid gid);
        void acquire_instance(gid gid);
        void release_instance(gid gid);

        // thread unsafe, call from one thread only
        Buffer get_buffer();

        bool want_to_save() {
            std::lock_guard lock{mutex};
            auto res = want_save;
            want_save = false;
            return res;
        }

        template <typename F> void with_textures(F&& f, gid gid) {
            std::lock_guard lock{mutex};

            auto schema = get_schema(gid.dim());
            if (!schema) return;

            auto& insts = instances[gid.dim()];
            if (insts.generations.size() <= gid.id()) return;
            if (insts.generations[gid.id()] != gid.gen()) return;

            auto* data = insts.data.data() + schema->total_size * gid.id();
            for (const auto off : schema->texture_gid_offsets) {
                f(*(Textures::gid*)(data + off));
            }
        }

      private:
        Materials() = default;

        struct Instance {
            std::vector<std::string> names{};
            std::vector<uint32_t> generations{};
            std::vector<uint32_t> ref_counts{};
            std::vector<uint32_t> deleted{};

            std::vector<uint8_t> data{};
        };

        bool update = false;
        bool want_save = false;

        std::recursive_mutex mutex{};

        uint32_t current_buffer = 0;
        std::array<Buffer, 2> gpu_buffers{};
        transport2::ticket next_buffer_ticket{};

        std::vector<std::string> names{};
        std::vector<Material> schemas{};
        std::vector<uint32_t> offsets{};
        std::vector<Instance> instances{};

        std::vector<uint32_t> deleted{};

        bool is_deleted(uint32_t mat_id) {
            return std::find(deleted.begin(), deleted.end(), mat_id) != deleted.end();
        }
    };
}
