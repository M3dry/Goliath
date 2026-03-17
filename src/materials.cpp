#include "goliath/materials.hpp"
#include "goliath/buffer.hpp"
#include "goliath/transport2.hpp"

#include <optional>
#include <vulkan/vulkan_core.h>

#include <expected>

namespace engine {
    void Materials::to_json(nlohmann::json& j, const gid& gid) {
        j = gid.value;
    }

    void Materials::from_json(const nlohmann::json& j, gid& gid) {
        gid.value = j;
    }

    std::expected<Materials*, util::ReadJsonErr> Materials::init(const nlohmann::json& j) {
        auto* ms = new Materials{};

        std::vector<nlohmann::json> arr = j;

        for (const auto& j : arr) {
            auto mat_ix = j["ix"];
            if (j.contains("deleted")) {
                ms->deleted.emplace_back(mat_ix);
                continue;
            }

            while (ms->names.size() <= mat_ix) {
                ms->names.emplace_back();
                ms->schemas.emplace_back();
                ms->offsets.emplace_back();
                ms->instances.emplace_back();
            }

            ms->names[mat_ix] = j["name"];
            ms->schemas[mat_ix] = j["schema"];
            ms->offsets[mat_ix] = j["offset"];

            auto& insts = ms->instances[mat_ix];
            auto schema_size = ms->schemas[mat_ix].total_size;
            for (const auto& j : j["instances"]) {
                uint32_t inst_ix = j["ix"];
                while (insts.names.size() <= inst_ix) {
                    insts.names.emplace_back();
                    insts.generations.emplace_back();
                    insts.ref_counts.emplace_back();
                    insts.deleted.emplace_back();
                    insts.data.resize(insts.data.size() + schema_size);
                }

                insts.generations[inst_ix] = j["gen"];
                if (j.contains("deleted")) {
                    insts.deleted[inst_ix] = true;
                } else {
                    insts.names[inst_ix] = j["name"];
                    insts.ref_counts[inst_ix] = j["ref_count"];

                    std::vector<uint8_t> blob = j["data"];
                    assert(blob.size() == schema_size);
                    std::memcpy(insts.data.data() + schema_size * inst_ix, blob.data(), schema_size);
                }
            }
        }

        ms->update = true;

        return ms;
    }

    Materials::~Materials() {
        for (auto buf : gpu_buffers) {
            buf.destroy();
        }
    }

    nlohmann::json Materials::save() {
        std::lock_guard lock{mutex};

        auto arr = nlohmann::json::array();

        for (size_t i = 0; i < offsets.size(); i++) {
            if (is_deleted(i)) {
                arr.emplace_back(nlohmann::json{
                    {"ix", i},
                    {"deleted", true},
                });
                continue;
            }

            auto& insts = instances[i];
            auto insts_j = nlohmann::json::array();

            auto schema_size = schemas[i].total_size;
            for (size_t j = 0; j < insts.names.size(); j++) {
                if (insts.deleted[j]) {
                    insts_j.emplace_back(nlohmann::json{
                        {"ix", j},
                        {"gen", insts.generations[j]},
                        {"deleted", true},
                    });
                }

                insts_j.emplace_back(nlohmann::json{
                    {"ix", j},
                    {"gen", insts.generations[j]},
                    {"name", insts.names[j]},
                    {"ref_count", insts.ref_counts[j]},
                    {"data", std::span{(insts.data.data() + schema_size * j), schema_size}},
                });
            }

            arr.emplace_back(nlohmann::json{
                {"ix", i},
                {"name", names[i]},
                {"schema", schemas[i]},
                {"instances", insts_j},
                {"offset", offsets[i]},
            });
        }

        return arr;
    }

    nlohmann::json Materials::default_json() {
        auto arr = nlohmann::json::array();
        arr.emplace_back(nlohmann::json{
            {"ix", 0},
            {"name", "PBR - Metallic Roughness"},
            {"schema", material::pbr::schema},
            {"instances", nlohmann::json::array()},
            {"offset", 0},
        });
        return arr;
    }

    void Materials::process() {
        std::lock_guard lock{mutex};

        if (transport2::is_ready(next_buffer_ticket)) {
            current_buffer = (current_buffer + 1) % 2;
            next_buffer_ticket = {};
        }

        if (!update) {
            return;
        }

        uint32_t instances_size = 0;
        for (size_t i = 0; i < instances.size(); i++) {
            instances_size += schemas[i].total_size * instances[i].names.size();
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

        next_buffer_ticket =
            transport2::upload(true, upload, free, upload_size, gpu_buffers[(current_buffer + 1) % 2], 0,
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

        update = false;
    }

    uint32_t Materials::add_schema(Material schema, std::string name) {
        std::lock_guard lock{mutex};

        uint32_t mat_id;
        if (deleted.empty()) {
            names.emplace_back(name);
            schemas.emplace_back(schema);
            offsets.emplace_back(offsets.back());
            instances.emplace_back();

            mat_id = names.size() - 1;
        } else {
            mat_id = deleted.back();
            deleted.pop_back();

            names[mat_id] = name;
            schemas[mat_id] = schema;
            offsets[mat_id] = mat_id == 0 ? 0 : offsets[mat_id - 1];
            instances[mat_id] = {};
        }

        update = true;
        want_save = true;

        return mat_id;
    }

    bool Materials::remove_schema(uint32_t mat_id) {
        std::lock_guard lock{mutex};

        if (names.size() <= mat_id) return false;

        for (auto rc : instances[mat_id].ref_counts) {
            if (rc != 0) return false;
        }

        if (mat_id + 1 != offsets.size()) {
            auto d_offset = offsets[mat_id + 1] - offsets[mat_id];
            for (size_t i = mat_id + 1; i < offsets.size(); i++) {
                offsets[i] -= d_offset;
            }
        }

        names[mat_id] = "";
        schemas[mat_id] = {};

        deleted.emplace_back(mat_id);

        want_save = true;
        return true;
    }

    std::optional<Material> Materials::get_schema(uint32_t mat_id) {
        std::lock_guard lock{mutex};

        if (schemas.size() <= mat_id) return {};
        if (is_deleted(mat_id)) return {};
        return schemas[mat_id];
    }

    std::optional<std::vector<uint8_t>> Materials::get_instance_data(gid gid) {
        std::lock_guard lock{mutex};

        const auto& schema = get_schema(gid.dim());
        if (!schema) return {};

        auto size = schema->total_size;
        auto& insts = instances[gid.dim()];

        if (insts.deleted[gid.id()]) return {};

        auto* begin = insts.data.data() + size * gid.id();
        return std::vector<uint8_t>{begin, begin + size};
    }

    void Materials::update_instance_data(gid gid, uint8_t* new_data) {
        std::lock_guard lock{mutex};

        const auto& schema = get_schema(gid.dim());
        if (!schema) return;

        auto size = schema->total_size;
        auto& insts = instances[gid.dim()];

        if (insts.names.size() <= gid.id()) return;
        if (insts.deleted[gid.id()]) return;
        if (insts.generations[gid.id()] != gid.gen()) return;
        std::memcpy(insts.data.data() + size * gid.id(), new_data, size);

        update = true;
        want_save = true;
    }

    Materials::gid Materials::add_instance(uint32_t mat_id, std::string name, std::span<uint8_t> data) {
        std::lock_guard lock{mutex};

        const auto& schema = get_schema(mat_id);
        if (!schema) return {};

        auto& insts = instances[mat_id];

        uint32_t id = -1;
        for (uint32_t i = 0; i < insts.deleted.size(); i++) {
            if (insts.deleted[i]) id = i;
        }

        if (id == -1) {
            insts.deleted.emplace_back(false);
            insts.generations.emplace_back(0);
            insts.names.emplace_back(name);
            insts.ref_counts.emplace_back(0);

            if (data.empty()) {
                insts.data.resize(insts.data.size() + schema->total_size);
            } else {
                assert(schema->total_size == data.size());
                insts.data.insert(insts.data.end(), data.begin(), data.end());
            }

            id = insts.names.size() - 1;
        } else {
            insts.deleted[id] = false;
            insts.generations[id]++;
            insts.names[id] = name;
            insts.ref_counts[id] = 0;

            if (data.empty()) {
                std::memset(insts.data.data() + id * schema->total_size, 0, schema->total_size);
            } else {
                assert(schema->total_size == data.size());
                std::memcpy(insts.data.data() + id * schemas[mat_id].total_size, data.data(), schema->total_size);
            }
        }

        for (uint32_t i = mat_id + 1; i < schemas.size(); i++) {
            offsets[i] += schema->total_size;
        }

        want_save = true;
        update = true;
        return {mat_id, insts.generations[id], id};
    }

    bool Materials::remove_instance(gid gid) {
        std::lock_guard lock{mutex};

        auto schema = get_schema(gid.dim());
        if (!schema) return false;

        auto& insts = instances[gid.dim()];
        if (insts.generations.size() <= gid.id()) return false;
        if (insts.generations[gid.id()] != gid.gen()) return false;

        auto begin = insts.data.begin() + schema->total_size * gid.id();
        insts.data.erase(begin, begin + schema->total_size);

        insts.generations[gid.id()]++;
        insts.deleted[gid.id()] = true;
        insts.names[gid.id()] = "";
        insts.ref_counts[gid.id()] = 0;

        want_save = true;
        return true;
    }

    void Materials::acquire_instance(gid gid) {
        std::lock_guard lock{mutex};

        auto schema = get_schema(gid.dim());
        if (!schema) return;

        auto& insts = instances[gid.dim()];
        if (insts.generations.size() <= gid.id()) return;
        if (insts.generations[gid.id()] != gid.gen()) return;

        insts.ref_counts[gid.id()]++;
    }

    void Materials::release_instance(gid gid) {
        std::lock_guard lock{mutex};

        auto schema = get_schema(gid.dim());
        if (!schema) return;

        auto& insts = instances[gid.dim()];
        if (insts.generations.size() <= gid.id()) return;
        if (insts.generations[gid.id()] != gid.gen()) return;

        if (insts.ref_counts[gid.id()] == 0) return;
        insts.ref_counts[gid.id()]--;
    }

    Buffer Materials::get_buffer() {
        return gpu_buffers[current_buffer];
    }

    std::optional<std::reference_wrapper<std::string>> Materials::get_name(gid gid) {
        auto schema = get_schema(gid.dim());
        if (!schema) return {};

        if (instances[gid.dim()].deleted.size() <= gid.id()) return {};
        if (instances[gid.dim()].deleted[gid.id()]) return {};
        if (instances[gid.dim()].generations[gid.id()] != gid.gen()) return {};

        return instances[gid.dim()].names[gid.id()];
    }

    std::optional<std::reference_wrapper<std::string>> Materials::get_schema_name(uint32_t dim) {
        if (names.size() <= dim) return {};
        return names[dim];
    }

    void Materials::modified() {
        want_save = true;
    }
}
