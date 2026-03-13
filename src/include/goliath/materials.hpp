#pragma once

#include "goliath/buffer.hpp"
#include "goliath/material.hpp"
#include "goliath/util.hpp"

#include <nlohmann/json.hpp>

namespace engine {
    class MaterialSchemas {
      public:
        struct gid {
            uint64_t value;

            static constexpr uint64_t id_mask = 0x0000'0000'FFFF'FFFFu;
            static constexpr uint64_t gen_mask = 0x00FF'FFFF'0000'0000u;
            static constexpr uint64_t dim_mask = 0xFF00'0000'0000'0000u;
            static constexpr uint64_t gen_shift = 32;
            static constexpr uint64_t dim_shift = 56;

            gid() : value(-1) {}
            gid(uint32_t dimension, uint32_t generation, uint32_t id)
                : value((id & id_mask) | (((uint64_t)generation & 0xFFFF'FFFFu) << gen_shift) |
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

        std::expected<MaterialSchemas, util::ReadJsonErr> init(const nlohmann::json& j);
        MaterialSchemas() = default;
        MaterialSchemas(MaterialSchemas&& other) noexcept
            : update(other.update), want_save(other.want_save), current_buffer(other.current_buffer),
              gpu_buffers(std::move(other.gpu_buffers)), next_buffer_ticket(std::move(other.next_buffer_ticket)),
              names(std::move(other.names)), schemas(std::move(other.schemas)),
              offsets(std::move(other.offsets)),
              deleted(std::move(other.deleted)), instances(std::move(other.instances)) {
        }

        MaterialSchemas& operator=(MaterialSchemas&&) = delete;
        MaterialSchemas(const MaterialSchemas&) = delete;
        MaterialSchemas& operator=(const MaterialSchemas&) = delete;

        ~MaterialSchemas();

        nlohmann::json save();
        nlohmann::json default_json();

        uint32_t add_schema(Material schema, std::string name);
        bool remove_schema(uint32_t mat_id);
        std::optional<std::reference_wrapper<const Material>>  get_schema(uint32_t mat_id);

        std::span<uint8_t> get_instance_data(gid gid);
        void update_instance_data(gid gid, uint8_t* new_data);
        gid add_instance(uint32_t mat_id, std::string name, std::span<uint8_t> data);
        bool remove_instance(gid gid);
        void acquire_instance(gid gid);
        void release_instance(gid gid);

        Buffer get_buffer();

      private:
        struct Instance {
            std::vector<std::string> names{};
            std::vector<uint32_t> generations{};
            std::vector<uint32_t> ref_counts{};
            std::vector<uint32_t> deleted{};

            std::vector<uint8_t> data{};
        };

        bool update = false;
        bool want_save = false;

        std::mutex mutex{};

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
