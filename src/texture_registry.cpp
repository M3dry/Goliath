#include "goliath/texture_registry.hpp"
#include "goliath/engine.hpp"
#include "goliath/texture_pool.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <queue>
#include <thread>
#include <vector>

#include "xxHash/xxhash.h"

#include "stb_image.h"

namespace engine::texture_registry {
    struct Metadata {
        uint32_t width;
        uint32_t height;
        VkFormat format;
    };

    std::vector<std::string> names{};
    std::vector<std::filesystem::path> paths{};
    std::vector<std::pair<uint8_t*, uint32_t>> blobs{};
    std::vector<Metadata> metadatas{};

    std::vector<uint32_t> ref_counts{};
    std::vector<GPUImage> gpu_images{};
    std::vector<VkImageView> gpu_image_views{};
    std::vector<uint32_t> samplers{};

    std::vector<uint64_t> sampler_hashes{};
    std::vector<uint32_t> sampler_ref_counts{};
    std::vector<Sampler> sampler_prototypes{};
    std::vector<VkSampler> initialized_samplers{};

    static constexpr std::size_t gpu_queue_size = 64;
    std::array<uint32_t, gpu_queue_size> gpu_queue{};
    std::atomic<uint32_t> gpu_queue_write_ix{0};
    std::atomic<uint32_t> gpu_queue_processing_ix{0};

    void load_texture_data(uint32_t gid) {
        const auto& path = paths[gid];
        auto& blob = blobs[gid];

        if (!path.empty() && blobs[gid].first == nullptr) {
            auto image = Image::load8(path.c_str());

            blobs[gid].first = (uint8_t*)image.data;
            blobs[gid].second = image.size;

            metadatas[gid] = Metadata{
                .width = image.width,
                .height = image.height,
                .format = image.format,
            };
        } else {
            assert(blob.first != nullptr);
        }
    }

    struct task {
        uint32_t gid;
    };

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

                        load_texture_data(task.gid);

                        uint32_t ix = gpu_queue_write_ix.fetch_add(1, std::memory_order_acq_rel);
                        uint32_t slot = ix % gpu_queue_size;

                        while (ix >= gpu_queue_processing_ix.load(std::memory_order_acquire) + gpu_queue_size) {
                            std::this_thread::yield();
                        }

                        gpu_queue[slot] = task.gid;
                    }
                });
            }
        }

        ~queue_pool() {
            {
                std::lock_guard lock(mutex);
                stop = true;
            }
            cv.notify_all();
            for (auto& w : workers)
                w.join();
        }

        void enqueue(task* new_tasks, uint32_t count) {
            {
                std::lock_guard lock(mutex);
                for (std::size_t i = 0; i < count; i++) {
                    tasks.emplace(new_tasks[i]);
                }
            }
            cv.notify_all();
        }

        std::vector<std::thread> workers;
        std::queue<task> tasks;
        std::condition_variable cv;
        std::mutex mutex;
        bool stop = false;
    };

    queue_pool upload_queue{};

    uint64_t hash_sampler(const Sampler& sampler) {
        return XXH3_64bits(&sampler, sizeof(Sampler));
    }

    uint32_t acquire_sampler(const Sampler& new_sampler) {
        uint64_t sampler_hash = hash_sampler(new_sampler);
        uint32_t empty_spot = -1;

        for (uint32_t i = 0; i < sampler_hashes.size(); i++) {
            if (sampler_ref_counts[i] == 0) {
                empty_spot = i;
                continue;
            }
            if (sampler_hash != sampler_hashes[i]) continue;
            if (new_sampler != sampler_prototypes[i]) continue;

            sampler_ref_counts[i]++;
            return i;
        }

        if (empty_spot == -1) {
            sampler_hashes.emplace_back(sampler_hash);
            sampler_ref_counts.emplace_back(1);
            sampler_prototypes.emplace_back(new_sampler);
            initialized_samplers.emplace_back(new_sampler.create());

            return sampler_hashes.size() - 1;
        } else {
            sampler_hashes[empty_spot] = sampler_hash;
            sampler_ref_counts[empty_spot] = 1;
            sampler_prototypes[empty_spot] = new_sampler;
            initialized_samplers[empty_spot] = new_sampler.create();

            return empty_spot;
        }
    }

    void release_sampler(uint32_t ix) {
        if (--sampler_ref_counts[ix] != 0) return;

        Sampler::destroy(initialized_samplers[ix]);

        sampler_hashes[ix] = 0;
        initialized_samplers[ix] = nullptr;
    }

    void process_uploads() {
        uint32_t end = gpu_queue_write_ix.load(std::memory_order_acquire);
        uint32_t start = gpu_queue_processing_ix.load(std::memory_order_acquire);
        if (end == start) return;

        std::vector<uint32_t> gids{};
        gids.resize(end - start);
        std::memcpy(gids.data(), gpu_queue.data(), sizeof(uint32_t) * (end - start));

        gpu_queue_processing_ix.store(end, std::memory_order_release);

        for (const auto& gid : gids) {
            // TODO: upload to GPU please

            printf("freed: %s\n", names[gid].c_str());
            free(blobs[gid].first);
        }
    }

    void init() {}

    void destroy() {
        for (auto& sampler : initialized_samplers) {
            vkDestroySampler(device, sampler, nullptr);
        }

        for (std::size_t i = 0; i < names.size(); i++) {
            if (ref_counts[i] == 0) continue;

            gpu_images[i].destroy();
            GPUImageView::destroy(gpu_image_views[i]);
        }
    }

    void load(uint8_t* file_data, uint32_t file_size) {

    }

    void save(const std::filesystem::path& save_file) {

    }

    uint32_t add(std::filesystem::path path, std::string name, const Sampler& sampler) {
        auto sampler_ix = acquire_sampler(sampler);

        names.emplace_back(std::move(name));
        paths.emplace_back(std::move(path));
        blobs.emplace_back(nullptr, 0);
        metadatas.emplace_back();
        ref_counts.emplace_back(0);
        gpu_images.emplace_back();
        gpu_image_views.emplace_back();
        samplers.emplace_back(sampler_ix);

        return names.size() - 1;
    }

    // makes a copy of `data`, no ownership assumed
    uint32_t add(uint8_t* data, uint32_t data_size, uint32_t width, uint32_t height, VkFormat format, std::string name, const Sampler& sampler) {
        auto sampler_ix = acquire_sampler(sampler);

        names.emplace_back(std::move(name));
        paths.emplace_back();

        void* data_copy = malloc(data_size);
        std::memcpy(data_copy, data, data_size);
        blobs.emplace_back((uint8_t*)data_copy, data_size);

        metadatas.emplace_back(Metadata{
            .width = width,
            .height = height,
            .format = format,
        });
        ref_counts.emplace_back(0);
        gpu_images.emplace_back();
        gpu_image_views.emplace_back();
        samplers.emplace_back(sampler_ix);

        return names.size() - 1;
    }

    void remove(uint32_t gid) {}

    void acquire(uint32_t* gids, uint32_t count) {
        for (std::size_t i = 0; i < count; i++) {
            auto gid = gids[i];
            if (++ref_counts[gid] != 1) continue;

            task task{
                .gid = gid,
            };
            upload_queue.enqueue(&task, 1);

            texture_pool::update(gid, texture_pool::default_texture_view, texture_pool::default_texture_layout,
                                 texture_pool::default_sampler);
        }
    }

    void release(uint32_t* gids, uint32_t count) {
        for (std::size_t i = 0; i < count; i++) {
            auto gid = gids[i];
            if (--ref_counts[gid] != 0) return;

            gpu_images[gid].destroy();
            GPUImageView::destroy(gpu_image_views[gid]);
            texture_pool::update(gid, texture_pool::default_texture_view, texture_pool::default_texture_layout,
                                 texture_pool::default_sampler);
        }
    }
}
