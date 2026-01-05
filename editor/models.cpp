#include "models.hpp"
#include "goliath/culling.hpp"
#include "goliath/engine.hpp"
#include "goliath/gpu_group.hpp"
#include "goliath/mspc_queue.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/thread_pool.hpp"
#include "goliath/transport.hpp"
#include "goliath/util.hpp"
#include "project.hpp"

#include <atomic>
#include <expected>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <fstream>


namespace models {
    std::filesystem::path make_model_path(gid gid) {
        return std::format("{:02X}{:06X}.gom", (uint8_t)gid.gen(), gid.id() & 0x00ffffff);
    }

    void to_json(nlohmann::json& j, const gid& gid) {
        j = gid.value;
    }

    void from_json(const nlohmann::json& j, gid& gid) {
        gid.value = j;
    }

    struct UploadedModelData {
        uint64_t timeline = (uint64_t)-1;
        engine::Buffer draw_buffer{};
        engine::GPUModel gpu{};
        engine::GPUGroup group{};

        void destroy() {
            draw_buffer.destroy();
            group.destroy();
        }
    };

    std::vector<std::string> names{};
    std::vector<uint32_t> ref_counts{};

    std::vector<std::optional<engine::Model>> cpu_datas{};
    std::vector<UploadedModelData> gpu_datas{};

    std::vector<std::unique_ptr<std::atomic<uint8_t>>> generations{};
    std::vector<bool> deleted{};

    struct JsonModelEntry {
        std::string name;
        gid gid;
    };

    void to_json(nlohmann::json& j, const JsonModelEntry& entry) {
        j = nlohmann::json{
            {"name", entry.name},
            {"path", entry.gid},
        };
    }

    void from_json(const nlohmann::json& j, JsonModelEntry& entry) {
        j["name"].get_to(entry.name);
        j["path"].get_to(entry.gid);
    }

    struct task {
        enum Type {
            Acquire,
            Add,
        };

        Type type;
        gid gid;
        std::filesystem::path orig_path{};
    };

    static constexpr std::size_t gpu_queue_size = 64;
    engine::MSPCQueue<gid, gpu_queue_size> gpu_queue{};
    std::mutex gid_read{};

    std::vector<gid> initializing_models{};
    static constexpr std::size_t initialized_queue_size = 32;
    engine::MSPCQueue<gid, initialized_queue_size> initialized_queue{};

    bool is_initializing(gid gid) {
        return std::find(initializing_models.begin(), initializing_models.end(), gid) != initializing_models.end();
    }

    void load_model_data(gid gid) {
        std::lock_guard lock{gid_read};

        if (generations[gid.id()]->load() != gid.gen()) return;

        uint32_t model_size;
        auto* model_data = engine::util::read_file(project::models_directory / make_model_path(gid), &model_size);

        cpu_datas[gid.id()] = engine::Model{};
        engine::Model::load_optimized(cpu_datas[gid.id()].value(), {model_data, model_size});

        free(model_data);
    }

    void add_model(gid gid, std::filesystem::path orig_path) {
        std::lock_guard lock{gid_read};

        if (generations[gid.id()]->load() != gid.gen()) return;

        uint32_t model_size;
        auto* model_data = engine::util::read_file(orig_path, &model_size);

        engine::Model model{};

        if (generations[gid.id()]->load() != gid.gen()) goto cleanup;

        {
            const auto& ext = orig_path.extension();
            auto path = project::models_directory / make_model_path(gid);
            if (ext == ".glb") {
                engine::Model::load_glb(&model, {model_data, model_size}, orig_path.parent_path().string());
            } else if (ext == ".gltf") {
                engine::Model::load_gltf(&model, {model_data, model_size}, orig_path.parent_path().string());
            } else {
                std::filesystem::copy(orig_path, path);
                return;
            }

            if (generations[gid.id()]->load() != gid.gen()) goto cleanup;

            auto save_size = model.get_optimized_size();
            uint8_t* save_data = (uint8_t*)malloc(save_size);

            model.save_optimized({save_data, save_size});

            engine::util::save_file(path, save_data, save_size);

            free(save_data);
        }

        if (generations[gid.id()]->load() != gid.gen()) {
            std::filesystem::remove(project::models_directory /
                                    std::format("{:02X}{:06X}.gom", (uint8_t)gid.gen(), gid.id() & 0x00ffffff));
            goto cleanup;
        }

    cleanup:
        model.destroy();
        free(model_data);
    }

    auto io_pool = engine::make_thread_pool([](task&& task) {
        switch (task.type) {
            case task::Acquire:
                while (is_initializing(task.gid)) {
                    _mm_pause();
                }

                load_model_data(task.gid);
                gpu_queue.enqueue(task.gid);
                break;
            case task::Add:
                add_model(task.gid, task.orig_path);
                initialized_queue.enqueue(task.gid);
                break;
        }
    });

    void process_uploads() {
        std::vector<gid> upload_gids{};
        gpu_queue.drain(upload_gids);

        std::vector<gid> initialized_gids{};
        initialized_queue.drain(initialized_gids);

        if (upload_gids.size() != 0) {
            engine::synchronization::begin_barriers();
            auto timeline = engine::transport::begin();
            for (const auto& gid : upload_gids) {
                if (generations[gid.id()]->load() == gid.gen() && ref_counts[gid.id()] != 0 && !deleted[gid.id()]) {
                    auto& cpu_data = *cpu_datas[gid.id()];
                    engine::gpu_group::begin();
                    auto [gpu, draw_buffer] = engine::model::upload(&cpu_data);

                    VkBufferMemoryBarrier2 barrier{};
                    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    barrier.dstStageMask =
                        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    barrier.dstQueueFamilyIndex = engine::graphics_queue_family;
                    auto& gpu_data = gpu_datas[gid.id()];
                    gpu_data.group = engine::gpu_group::end(&barrier, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT);
                    gpu_data.draw_buffer = draw_buffer;
                    gpu_data.gpu = gpu;
                    gpu_data.timeline = timeline;

                    engine::synchronization::apply_barrier(barrier);
                }
            }
            engine::transport::end();
            engine::synchronization::end_barriers();
        }

        bool initialized = false;
        std::erase_if(initializing_models, [&](auto gid) {
            auto found = std::find(initialized_gids.begin(), initialized_gids.end(), gid) != initialized_gids.end();
            initialized |= found;
            return found;
        });

        if (!initialized) return;

        std::ofstream mf{project::models_registry};
        mf << models::save();
        mf.flush();

        std::ofstream tf{project::textures_registry};
        tf << engine::textures::save();
        tf.flush();
    }

    std::optional<gid> find_empty_gid() {
        for (uint32_t i = 0; i < deleted.size(); i++) {
            if (deleted[i]) {
                return gid{generations[i]->load(), i};
            }
        }

        return std::nullopt;
    }

    void init(const nlohmann::json& j) {
        std::vector<JsonModelEntry> entries{};
        j.get_to(entries);

        uint32_t id_counter = 0;
        for (auto&& entry : entries) {
            auto gid = entry.gid;
            while (gid.id() > id_counter) {
                names.emplace_back();

                ref_counts.emplace_back(0);

                cpu_datas.emplace_back();
                gpu_datas.emplace_back();

                generations.emplace_back(std::make_unique<std::atomic<uint8_t>>(0));

                deleted.emplace_back(true);
                id_counter++;
            }

            names.emplace_back(std::move(entry.name));

            ref_counts.emplace_back(0);

            cpu_datas.emplace_back();
            gpu_datas.emplace_back();

            generations.emplace_back(std::make_unique<std::atomic<uint8_t>>(gid.gen()));
            deleted.emplace_back(false);

            id_counter++;
        }
    }

    void destroy() {
        for (size_t i = 0; i < names.size(); i++) {
            if (cpu_datas[i]) {
                cpu_datas[i]->destroy();
            }

            gpu_datas[i].destroy();
        }
    }

    nlohmann::json save() {
        std::vector<JsonModelEntry> entries{};

        for (uint32_t i = 0; i < names.size(); i++) {
            if (deleted[i]) continue;

            entries.emplace_back(names[i], gid{generations[i]->load(), i});
        }

        return nlohmann::json(entries);
    }

    gid add(std::filesystem::path path, std::string name) {
        gid gid;
        if (auto gid_ = find_empty_gid(); gid_) {
            gid = *gid_;
            uint8_t gid_gen = gid.gen();

            names[gid.id()] = std::move(name);

            ref_counts[gid.id()] = 0;

            cpu_datas[gid.id()] = std::nullopt;
            gpu_datas[gid.id()] = UploadedModelData{
                .timeline = (size_t)-1,
            };

            generations[gid.id()]->fetch_add(1, std::memory_order_release);
            deleted[gid.id()] = false;
        } else {
            std::lock_guard lock{gid_read};

            gid = {0, (uint32_t)names.size()};

            names.emplace_back(std::move(name));

            ref_counts.emplace_back(0);

            cpu_datas.emplace_back(std::nullopt);
            gpu_datas.emplace_back(UploadedModelData{
                .timeline = (size_t)-1,
            });

            generations.emplace_back(std::make_unique<std::atomic<uint8_t>>(0));
            deleted.emplace_back(false);
        }

        initializing_models.emplace_back(gid);
        io_pool.enqueue({task::Add, gid, path});

        return gid;
    }

    bool remove(gid gid) {
        if (generations[gid.id()]->load() != gid.gen()) return false;
        if (ref_counts[gid.id()] > 0) return false;
        if (deleted[gid.id()]) return false;

        deleted[gid.id()] = true;
        generations[gid.id()]->fetch_add(1, std::memory_order_release);

        std::filesystem::remove(project::models_directory / make_model_path(gid));

        if (cpu_datas[gid.id()]) cpu_datas[gid.id()]->destroy();
        gpu_datas[gid.id()].destroy();

        names[gid.id()] = "";

        cpu_datas[gid.id()] = std::nullopt;
        gpu_datas[gid.id()] = UploadedModelData{
            .timeline = (size_t)-1,
        };

        return true;
    }

    std::expected<std::string*, Err> get_name(gid gid) {
        if (generations[gid.id()]->load() != gid.gen()) return std::unexpected(Err::BadGeneration);

        return &names[gid.id()];
    }

    std::expected<engine::Model*, Err> get_cpu_model(gid gid) {
        if (generations[gid.id()]->load() != gid.gen()) return std::unexpected(Err::BadGeneration);

        auto& model = cpu_datas[gid.id()];
        return model ? &*model : nullptr;
    }

    std::expected<uint64_t, Err> get_timeline(gid gid) {
        if (generations[gid.id()]->load() != gid.gen()) return std::unexpected(Err::BadGeneration);

        return gpu_datas[gid.id()].timeline;
    }

    std::expected<engine::Buffer, Err> get_draw_buffer(gid gid) {
        if (generations[gid.id()]->load() != gid.gen()) return std::unexpected(Err::BadGeneration);

        return gpu_datas[gid.id()].draw_buffer;
    }

    std::expected<engine::GPUModel, Err> get_gpu_model(gid gid) {
        if (generations[gid.id()]->load() != gid.gen()) return std::unexpected(Err::BadGeneration);

        return gpu_datas[gid.id()].gpu;
    }

    std::expected<engine::GPUGroup, Err> get_gpu_group(gid gid) {
        if (generations[gid.id()]->load() != gid.gen()) return std::unexpected(Err::BadGeneration);

        return gpu_datas[gid.id()].group;
    }

    uint8_t get_generation(uint32_t ix) {
        return generations[ix]->load();
    }

    std::expected<LoadState, Err> is_loaded(gid gid) {
        if (generations[gid.id()]->load() != gid.gen()) return std::unexpected(Err::BadGeneration);

        if (engine::transport::is_ready(gpu_datas[gid.id()].timeline)) return LoadState::OnGPU;
        if (cpu_datas[gid.id()]) return LoadState::OnCPU;
        return LoadState::OnDisk;
    }

    void acquire(const gid* gids, uint32_t count) {
        for (size_t i = 0; i < count; i++) {
            auto gid = gids[i];
            if (generations[gid.id()]->load() != gid.gen()) continue;

            if (++ref_counts[gid.id()] != 1) return;

            cpu_datas[gid.id()] = std::nullopt;
            gpu_datas[gid.id()].timeline = -1;
            io_pool.enqueue({task::Acquire, gid});
        }
    }

    void release(const gid* gids, uint32_t count) {
        for (std::size_t i = 0; i < count; i++) {
            auto gid = gids[i];
            if (generations[gid.id()]->load() != gid.gen()) continue;
            if (ref_counts[gid.id()] == 0 || --ref_counts[gid.id()] != 0) continue;

            if (cpu_datas[gid.id()]) cpu_datas[gid.id()]->destroy();
            cpu_datas[gid.id()] = std::nullopt;

            gpu_datas[gid.id()].destroy();
            gpu_datas[gid.id()] = UploadedModelData{};
        }
    }

    std::span<std::string> get_names() {
        return names;
    }
}

namespace engine::culling {
    std::expected<void, models::Err> flatten(models::gid gid, uint64_t transforms_addr,
                                             uint32_t default_transform_offset) {
        if (models::generations[gid.id()]->load() != gid.gen()) return std::unexpected(models::Err::BadGeneration);

        auto& gpu = models::gpu_datas[gid.id()];
        engine::culling::flatten(gpu.group.data.address(), gpu.gpu.mesh_count, gpu.draw_buffer.address(),
                                 transforms_addr, default_transform_offset);

        return {};
    }
}
