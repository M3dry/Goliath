#include "models.hpp"
#include "goliath/gpu_group.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/transport.hpp"
#include "goliath/util.hpp"
#include "goliath/culling.hpp"
#include "project.hpp"

#include <atomic>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace models {
    void to_json(nlohmann::json& j, const gid& gid) {
        j = nlohmann::json{gid};
    }

    void from_json(const nlohmann::json& j, gid& gid) {
        j.get_to(gid);
    }

    struct UploadedModelData {
        uint64_t timeline = -1;
        engine::Buffer draw_buffer{};
        engine::GPUModel gpu{};
        engine::GPUGroup group{};

        void destroy() {
            draw_buffer.destroy();
            group.destroy();
        }
    };

    std::vector<std::string> names{};
    std::vector<std::filesystem::path> paths{};

    std::vector<uint32_t> ref_counts{};

    std::vector<std::optional<engine::Model>> cpu_datas{};
    std::vector<UploadedModelData> gpu_datas{};

    std::vector<uint8_t> generations{};
    std::vector<bool> deleted{};

    struct JsonModelEntry {
        std::string name;
        std::filesystem::path path;
    };

    void to_json(nlohmann::json& j, const JsonModelEntry& entry) {
        j = nlohmann::json{
            {"name", entry.name},
            {"path", entry.path},
        };
    }

    void from_json(const nlohmann::json& j, JsonModelEntry& entry) {
        j["name"].get_to(entry.name);
        j["path"].get_to(entry.path);
    }

    using task = gid;

    static constexpr std::size_t queue_size = 64;
    std::array<task, queue_size> queue{};
    std::atomic<uint32_t> queue_write_ix{0};
    std::atomic<uint32_t> queue_processing_ix{0};
    std::mutex gid_read{};

    void load_model_data(gid gid) {
        std::lock_guard lock{gid_read};

        if (generations[gid.id != gid.generation]) return;

        uint32_t model_size;
        auto* model_data = engine::util::read_file(project::models_directory / paths[gid.id], &model_size);

        cpu_datas[gid.id] = engine::Model{};
        engine::Model::load_optimized(cpu_datas[gid.id].value(), {model_data, model_size});

        free(model_data);
    }

    struct queue_pool {
        queue_pool(std::size_t thread_count = std::thread::hardware_concurrency()) {
            for (size_t i = 0; i < thread_count; i++) {
                workers.emplace_back([&] {
                    while (true) {
                        task task;
                        {
                            std::unique_lock lock(mutex);
                            cv.wait(lock, [&] { return stop || !tasks.empty(); });
                            if (stop && tasks.empty()) return;
                            task = tasks.front();
                            tasks.pop();
                        }

                        load_model_data(task);

                        uint32_t ix = queue_write_ix.fetch_add(1, std::memory_order_acq_rel);
                        uint32_t slot = ix % queue_size;

                        while (ix >= queue_processing_ix.load(std::memory_order_acquire) + queue_size) {
                            std::this_thread::yield();
                        }

                        queue[slot] = task;
                    }
                });
            }
        }

        ~queue_pool() {
            {
                std::lock_guard lock{mutex};
                stop = true;
            }
            cv.notify_all();
            for (auto& w : workers)
                w.join();
        }

        void enqueue(task new_task) {
            {
                std::lock_guard lock{mutex};
                tasks.emplace(new_task);
            }
            cv.notify_one();
        }

        std::vector<std::thread> workers;
        std::queue<task> tasks;
        std::condition_variable cv;
        std::mutex mutex;
        bool stop = false;
    };

    queue_pool upload_queue{};

    void process_uploads() {
        uint32_t end = queue_write_ix.load(std::memory_order_acquire);
        uint32_t start = queue_processing_ix.load(std::memory_order_acquire);

        std::vector<task> tasks{};
        tasks.resize(end - start);
        std::memcpy(tasks.data(), queue.data() + start, sizeof(task) * (end - start));

        queue_processing_ix.store(end, std::memory_order_release);

        std::vector<VkImageMemoryBarrier2> barriers{};
        barriers.resize(tasks.size());

        uint64_t finish_timeline;
        if (tasks.size() != 0) {
            engine::synchronization::begin_barriers();
            auto timeline = engine::transport::begin();
            for (const auto& task : tasks) {
                if (generations[task.id] == task.generation && ref_counts[task.id] != 0 && !deleted[task.id]) {
                    auto& cpu_data = *cpu_datas[task.id];
                    engine::gpu_group::begin();
                    auto [gpu, draw_buffer] = engine::model::upload(&cpu_data);

                    VkBufferMemoryBarrier2 barrier{};
                    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    auto& gpu_data = gpu_datas[task.id];
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
    }

    std::optional<gid> find_empty_gid() {
        for (uint32_t i = 0; i < deleted.size(); i++) {
            if (deleted[i]) {
                return gid{generations[i], i};
            }
        }

        return std::nullopt;
    }

    void init(const nlohmann::json& j) {
        std::vector<JsonModelEntry> entries{};
        j.get_to(entries);

        uint32_t id_counter = 0;
        for (auto&& entry : entries) {
            auto stem = entry.path.stem().string();
            auto gen = (uint8_t)std::stoi(stem.substr(0, 2), 0, 16);
            auto id = (uint32_t)std::stoi(stem.substr(2, 8), 0, 16);
            while (id > id_counter) {
                names.emplace_back();
                paths.emplace_back();

                ref_counts.emplace_back(0);

                cpu_datas.emplace_back();
                gpu_datas.emplace_back();

                generations.emplace_back(0);
                deleted.emplace_back(true);
                id_counter++;
            }

            names.emplace_back(std::move(entry.name));
            paths.emplace_back(std::move(entry.path));

            ref_counts.emplace_back(0);

            cpu_datas.emplace_back();
            gpu_datas.emplace_back();

            generations.emplace_back(gen);
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

        for (size_t i = 0; i < names.size(); i++) {
            if (deleted[i]) continue;

            entries.emplace_back(names[i], paths[i]);
        }

        return nlohmann::json(entries);
    }

    gid add(std::filesystem::path path, std::string name) {
        gid gid;
        if (auto gid_ = find_empty_gid(); gid_) {
            gid = *gid_;
            uint8_t gid_gen = gid.generation;
            uint8_t gid_id = gid.id;

            names[gid.id] = std::move(name);
            paths[gid.id] = std::format("{:02X}{:06X}.gos", gid_gen, gid_id & 0x00ffffff);

            ref_counts[gid.id] = 0;

            cpu_datas[gid.id] = std::nullopt;
            gpu_datas[gid.id] = UploadedModelData{
                .timeline = (size_t)-1,
            };

            generations[gid.id]++;
            deleted[gid.id] = false;
        } else {
            std::lock_guard lock{gid_read};

            gid.generation = 0;
            gid.id = names.size();
            uint8_t gid_id = gid.id;

            names.emplace_back(std::move(name));
            paths.emplace_back(std::format("00{:06X}.gos", gid_id & 0x00ffffff));

            ref_counts.emplace_back(0);

            cpu_datas.emplace_back(std::nullopt);
            gpu_datas.emplace_back(UploadedModelData{
                .timeline = (size_t)-1,
            });

            generations.emplace_back(0);
            deleted.emplace_back(false);
        }

        uint32_t model_size;
        auto* model_data = engine::util::read_file(path, &model_size);

        const auto& ext = path.extension();
        bool optimized = false;
        engine::Model model{};

        if (ext == ".glb") {
            engine::Model::load_glb(&model, {model_data, model_size}, path.parent_path());
        } else if (ext == ".gltf") {
            engine::Model::load_gltf(&model, {model_data, model_size}, path.parent_path());
        } else {
            optimized = true;
        }

        if (optimized) {
            engine::util::save_file(project::models_directory / paths[gid.id], model_data, model_size);
        } else {
            auto save_size = model.get_optimized_size();
            uint8_t* save_data = (uint8_t*)malloc(save_size);
            model.save_optimized({save_data, save_size});

            engine::util::save_file(project::models_directory / paths[gid.id], save_data, save_size);
        }

        if (!optimized) model.destroy();

        free(model_data);
        return gid;
    }

    bool remove(gid gid) {
        if (generations[gid.id] == gid.generation) return false;
        if (ref_counts[gid.id] > 0) return false;
        std::lock_guard lock{gid_read};

        std::filesystem::remove(project::models_directory / paths[gid.id]);

        if (cpu_datas[gid.id]) cpu_datas[gid.id]->destroy();
        gpu_datas[gid.id].destroy();

        names[gid.id] = "";
        paths[gid.id] = "";

        cpu_datas[gid.id] = std::nullopt;
        gpu_datas[gid.id] = UploadedModelData{
            .timeline = (size_t)-1,
        };

        deleted[gid.id] = true;

        return true;
    }

    std::expected<std::string*, Err> get_name(gid gid) {
        if (generations[gid.id] != gid.generation) return std::unexpected(Err::BadGeneration);

        return &names[gid.id];
    }

    std::expected<const std::filesystem::path*, Err> get_path(gid gid) {
        if (generations[gid.id] != gid.generation) return std::unexpected(Err::BadGeneration);

        return &paths[gid.id];
    }

    std::expected<engine::Model*, Err> get_cpu_model(gid gid) {
        if (generations[gid.id] != gid.generation) return std::unexpected(Err::BadGeneration);

        auto& model = cpu_datas[gid.id];
        return model ? &*model : nullptr;
    }

    std::expected<uint64_t, Err> get_timeline(gid gid) {
        if (generations[gid.id] != gid.generation) return std::unexpected(Err::BadGeneration);

        return gpu_datas[gid.id].timeline;
    }

    std::expected<engine::Buffer, Err> get_draw_buffer(gid gid) {
        if (generations[gid.id] != gid.generation) return std::unexpected(Err::BadGeneration);

        return gpu_datas[gid.id].draw_buffer;
    }

    std::expected<engine::GPUModel, Err> get_gpu_model(gid gid) {
        if (generations[gid.id] != gid.generation) return std::unexpected(Err::BadGeneration);

        return gpu_datas[gid.id].gpu;
    }

    std::expected<engine::GPUGroup, Err> get_gpu_group(gid gid) {
        if (generations[gid.id] != gid.generation) return std::unexpected(Err::BadGeneration);

        return gpu_datas[gid.id].group;
    }

    uint8_t get_generation(uint32_t ix) {
        return generations[ix];
    }

    std::expected<LoadState, Err> is_loaded(gid gid) {
        if (gid.generation != generations[gid.id]) return std::unexpected(Err::BadGeneration);

        if (engine::transport::is_ready(gpu_datas[gid.id].timeline)) return LoadState::OnGPU;
        if (cpu_datas[gid.id]) return LoadState::OnCPU;
        return LoadState::OnDisk;
    }

    void acquire(const gid* gids, uint32_t count) {
        for (size_t i = 0; i < count; i++) {
            auto gid = gids[i];
            if (gid.generation != generations[gid.id]) continue;

            if (++ref_counts[gid.id] != 1) return;

            cpu_datas[gid.id] = std::nullopt;
            gpu_datas[gid.id].timeline = -1;
            upload_queue.enqueue(gid);
        }
    }

    void release(const gid* gids, uint32_t count) {
        for (std::size_t i = 0; i < count; i++) {
            auto gid = gids[i];
            if (generations[gid.id] != gid.generation) continue;
            if (ref_counts[gid.id] == 0 || --ref_counts[gid.id] != 0) continue;

            if (cpu_datas[gid.id]) cpu_datas[gid.id]->destroy();
            cpu_datas[gid.id] = std::nullopt;

            gpu_datas[gid.id].destroy();
            gpu_datas[gid.id] = UploadedModelData{};
        }
    }

    std::span<std::string> get_names() {
        return names;
    }
}

namespace engine::culling {
    std::expected<void, models::Err> flatten(models::gid gid, uint64_t transforms_addr, uint32_t default_transform_offset) {
        if (models::generations[gid.id] != gid.generation) return std::unexpected(models::Err::BadGeneration);

        auto& gpu = models::gpu_datas[gid.id];
        engine::culling::flatten(gpu.group.data.address(), gpu.gpu.mesh_count, gpu.draw_buffer.address(), transforms_addr, default_transform_offset);

        return {};
    }
}
