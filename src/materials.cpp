#include "goliath/materials.hpp"
#include "goliath/buffer.hpp"
#include "goliath/transport.hpp"
#include "materials_.hpp"

#include <vector>
#include <vulkan/vulkan_core.h>

namespace engine::materials {
    bool init_called = false;
    bool update = false;
    bool want_save = false;

    struct Instances {
        uint32_t count = 0;
        std::vector<std::string> names{};
        std::vector<uint8_t> data{};
        std::vector<uint32_t> ref_counts{};

        std::vector<uint32_t> deleted{};
    };

    std::vector<std::string> names{};
    std::vector<uint32_t> offsets{};
    std::vector<Instances> instances{};
    std::vector<Material> schemas{};

    std::vector<uint32_t> deleted{};

    uint32_t current_buffer = 0;
    std::array<Buffer, 2> gpu_buffers{};
    uint64_t next_buffer_timeline = -1;

    void init() {
        init_called = true;
        update = true;
    }

    void destroy() {
        if (!init_called) return;

        for (auto& buf : gpu_buffers) {
            buf.destroy();
        }
    }

    bool update_gpu_buffer(VkBufferMemoryBarrier2& barrier, bool& want_to_save) {
        if (!init_called) return false;

        want_to_save |= want_save;
        want_save = false;

        if (transport::is_ready(next_buffer_timeline)) {
            current_buffer = (current_buffer + 1) % 2;
            next_buffer_timeline = -1;
        }

        if (!update) return false;

        uint32_t instances_size = 0;
        for (size_t i = 0; i < instances.size(); i++) {
            instances_size += schemas[i].total_size * instances[i].count;
        }

        auto upload_size = sizeof(uint32_t) + offsets.size() * sizeof(uint32_t) + instances_size;
        auto upload = (uint8_t*)malloc(upload_size);

        uint32_t off = 0;
        auto offsets_size = offsets.size();
        std::memcpy(upload, &offsets_size, sizeof(uint32_t));
        off += sizeof(uint32_t);

        std::memcpy(upload + off, offsets.data(), offsets_size * sizeof(uint32_t));
        off += offsets_size * sizeof(uint32_t);

        for (size_t i = 0; i < instances.size(); i++) {
            std::memcpy(upload + off + offsets[i], instances[i].data.data(), instances[i].data.size());
        }

        auto& buf = gpu_buffers[(current_buffer + 1) % 2];
        if (buf.size() < upload_size) {
            buf.destroy();
            buf =
                Buffer::create("Material buffer", upload_size,
                               VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT, std::nullopt);
        }

        next_buffer_timeline = transport::begin();
        transport::upload(&barrier, upload, upload_size, gpu_buffers[(current_buffer + 1) % 2], 0);
        transport::end();

        update = false;
        return true;
    }

    struct JsonInstanceEntry {
        uint32_t ix;
        std::vector<uint32_t> data;
        std::string name;
        uint32_t ref_count;
    };

    struct JsonEntry {
        uint32_t ix;
        std::string name;
        Material schema;
        std::vector<JsonInstanceEntry> instances;
    };

    void from_json(const nlohmann::json& j, JsonInstanceEntry& inst) {
        j["ix"].get_to(inst.ix);
        j["data"].get_to(inst.data);
        j["name"].get_to(inst.name);
        j["ref_count"].get_to(inst.ref_count);
    }

    void from_json(const nlohmann::json& j, JsonEntry& e) {
        j["ix"].get_to(e.ix);
        j["name"].get_to(e.name);
        j["schema"].get_to(e.schema);
        j["instances"].get_to(e.instances);
    }

    void load(const nlohmann::json& j) {
        assert(init_called);

        std::vector<JsonEntry> entries = j;

        offsets.clear();
        names.clear();
        instances.clear();
        schemas.clear();

        deleted.clear();

        uint32_t mat_ix = 0;
        for (auto&& entry : entries) {
            while (entry.ix > mat_ix) {
                offsets.emplace_back(offsets.empty() ? 0 : offsets.back());
                instances.emplace_back();
                schemas.emplace_back();

                deleted.emplace_back(mat_ix);

                mat_ix++;
            }

            auto schema_size = entry.schema.total_size;
            schemas.emplace_back(entry.schema);
            names.emplace_back(entry.name);
            instances.emplace_back(Instances{});

            auto& insts = instances[entry.ix];

            for (auto&& inst_entry : entry.instances) {
                while (inst_entry.ix > insts.count) {
                    insts.data.resize(insts.data.size() + schema_size);
                    insts.deleted.emplace_back(insts.count);
                    insts.names.emplace_back();
                    insts.ref_counts.emplace_back(0);
                    insts.count++;
                }

                auto write_off = insts.data.size();
                insts.data.resize(write_off + schema_size);
                std::memcpy(insts.data.data() + write_off, inst_entry.data.data(), schema_size);
                insts.names.emplace_back(inst_entry.name);
                insts.ref_counts.emplace_back(inst_entry.ref_count);
                insts.count++;
            }

            offsets.emplace_back(
                mat_ix == 0 ? 0 : instances[mat_ix - 1].count * schemas[mat_ix - 1].total_size + offsets.back());

            mat_ix++;
        }

        update = true;
    }

    nlohmann::json save() {
        assert(init_called);

        auto arr = nlohmann::json::array();

        for (size_t i = 0; i < offsets.size(); i++) {
            auto insts = instances[i];
            auto insts_j = nlohmann::json::array();

            auto schema_size = schemas[i].total_size;
            for (size_t j = 0; j < insts.count; j++) {
                if (std::find(insts.deleted.begin(), insts.deleted.end(), j) != insts.deleted.end()) continue;

                insts_j.emplace_back(nlohmann::json{
                    {"ix", j},
                    {"data", std::span{(uint32_t*)(insts.data.data() + schema_size * j), schema_size}},
                    {"name", insts.names[j]},
                    {"ref_count", insts.ref_counts[j]},
                });
            }

            arr.emplace_back(nlohmann::json{
                {"ix", i},
                {"name", names[i]},
                {"schema", schemas[i]},
                {"instances", insts_j},
            });
        }

        return arr;
    }

    nlohmann::json default_json() {
        return nlohmann::json{{
            {"ix", 0},
            {"name", "PBR - Metallic Roughness"},
            {"schema", material::pbr::schema},
            {"instances", nlohmann::json::array()},
        }};
    }

    const Material& get_schema(uint32_t mat_id) {
        assert(init_called);

        return schemas[mat_id];
    }

    uint32_t add_schema(Material schema, std::string name) {
        assert(init_called);

        std::optional<uint32_t> free_ix{};
        if (deleted.size() != 0) {
            free_ix = deleted.back();
            deleted.pop_back();
        }

        if (free_ix) {
            names[*free_ix] = name;
            schemas[*free_ix] = schema;

            return *free_ix;
        }

        names.emplace_back(name);
        if (offsets.size() == 0) {
            offsets.emplace_back(0);
        } else {
            offsets.emplace_back(offsets.back() + instances.back().data.size());
        }
        schemas.emplace_back(schema);
        instances.emplace_back();

        auto mat_id = instances.size() - 1;

        update = true;
        want_save = true;
        return mat_id;
    }

    bool remove_schema(uint32_t mat_id) {
        assert(init_called);

        bool no_refs = true;
        for (const auto& ref_count : instances[mat_id].ref_counts) {
            no_refs &= ref_count == 0;
        }
        if (no_refs) return false;

        if (offsets.size() != mat_id + 1) {
            offsets[mat_id + 1] = offsets[mat_id];
        }
        names[mat_id] = "";
        instances[mat_id] = {0, {}};
        schemas[mat_id] = Material{};

        deleted.emplace_back(mat_id);

        want_save = true;
        return true;
    }

    std::span<uint8_t> get_instance_data(uint32_t mat_id, uint32_t instance_ix) {
        assert(init_called);

        auto schema_size = schemas[mat_id].total_size;
        return {instances[mat_id].data.data() + schema_size * instance_ix, schema_size};
    }

    void update_instance_data(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data) {
        assert(init_called);

        auto schema_size = schemas[mat_id].total_size;
        auto mat_data = instances[mat_id].data.data() + schema_size * instance_ix;

        std::memmove(mat_data, new_data, schema_size);

        update = true;
        want_save = true;
    }

    uint32_t add_instance(uint32_t mat_id, std::string name, uint8_t* data) {
        assert(init_called);

        auto schema_size = schemas[mat_id].total_size;

        auto& insts = instances[mat_id];
        if (!insts.deleted.empty()) {
            auto instance_ix = insts.deleted.back();
            insts.deleted.pop_back();
            insts.names[instance_ix] = name;

            update_instance_data(mat_id, instance_ix, data);

            return instance_ix;
        }

        auto write_off = insts.data.size();
        insts.data.resize(insts.data.size() + schema_size);
        std::memcpy(insts.data.data() + write_off, data, schema_size);

        insts.names.emplace_back(name);
        insts.ref_counts.emplace_back(0);

        for (size_t i = mat_id + 1; i < offsets.size(); i++) {
            offsets[i] += schema_size;
        }

        auto instance_ix = insts.count++;

        update = true;
        want_save = true;
        return instance_ix;
    }

    bool remove_instance(uint32_t mat_id, uint32_t instance_ix) {
        assert(init_called);

        auto& insts = instances[mat_id];
        if (insts.ref_counts[instance_ix] != 0) {
            return false;
        }

        insts.deleted.emplace_back(instance_ix);

        want_save = true;
        return true;
    }

    void acquire_instance(uint32_t mat_id, uint32_t instance_ix) {
        assert(init_called);

        instances[mat_id].ref_counts[instance_ix]++;
    }

    void release_instance(uint32_t mat_id, uint32_t instance_ix) {
        assert(init_called);

        instances[mat_id].ref_counts[instance_ix]--;
    }

    Buffer get_buffer() {
        return gpu_buffers[current_buffer];
    }
}
