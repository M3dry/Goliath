#include "goliath/texture_registry.hpp"
#include "goliath/engine.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/transport.hpp"
#include "goliath/mspc_queue.hpp"

#include <array>
#include <condition_variable>
#include <cstring>
#include <glm/ext/vector_float4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <queue>
#include <thread>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "xxHash/xxhash.h"

#include "stb_image.h"

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

namespace engine::texture_registry {
    uint32_t get_texture_store_size(uint32_t gid);
    void serialize_texture(uint8_t* out, uint32_t gid);
    void deserialize_texture(const VkSamplerCreateInfo* samplers, uint8_t* data, uint32_t offset);

    void serialize_sampler(uint8_t* out, const Sampler& prototype);
    Sampler deserialize_sampler(uint8_t* in);

    struct Metadata {
        uint32_t width;
        uint32_t height;
        VkFormat format;
    };

    TexturePool texture_pool;

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

    struct task {
        uint32_t gid;
        uint8_t generation;
    };

    static constexpr std::size_t gpu_queue_size = 64;
    MSPCQueue<task, gpu_queue_size> gpu_queue;
    std::mutex gid_read{};
    std::deque<std::pair<uint64_t, task>> finalize_queue{};

    std::vector<bool> deleted{};
    std::vector<uint8_t> generations{};

    std::optional<uint32_t> find_empty_gid() {
        for (uint32_t i = 0; i < deleted.size(); i++) {
            if (deleted[i]) {
                return i;
            }
        }

        return std::nullopt;
    }

    void load_texture_data(uint32_t gid, uint8_t generation) {
        std::lock_guard lock{gid_read};

        if (generations[gid] != generation) return;

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

                        load_texture_data(task.gid, task.generation);

                        gpu_queue.enqueue(task);
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
        std::vector<task> tasks{};
        gpu_queue.drain(tasks);

        std::vector<VkImageMemoryBarrier2> barriers{};
        barriers.resize(tasks.size());

        uint64_t finish_timeline;
        if (tasks.size() != 0) {
            synchronization::begin_barriers();
            transport::begin();
            for (const auto& task : tasks) {
                if (ref_counts[task.gid] != 0 && !deleted[task.gid] && generations[task.gid] == task.generation) {
                    auto metadata = metadatas[task.gid];
                    auto [image, barrier] = GPUImage::upload(GPUImageInfo{}
                                                                 .width(metadata.width)
                                                                 .height(metadata.height)
                                                                 .format(metadata.format)
                                                                 .data(blobs[task.gid].first)
                                                                 .size(blobs[task.gid].second)
                                                                 .new_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                                                 .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
                    gpu_images[task.gid] = image;
                    gpu_image_views[task.gid] = GPUImageView{image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).create();

                    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                    barrier.dstStageMask =
                        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    synchronization::apply_barrier(barrier);

                    if (!paths[task.gid].empty()) {
                        free(blobs[task.gid].first);
                        blobs[task.gid].first = nullptr;
                        blobs[task.gid].second = 0;
                    }
                }
            }
            finish_timeline = transport::end();
            synchronization::end_barriers();
        }

        while (finalize_queue.size() > 0 && transport::is_ready(finalize_queue.front().first)) {
            auto task = finalize_queue.front().second;

            if (ref_counts[task.gid] != 0 && !deleted[task.gid] && generations[task.gid] == task.generation) {
                texture_pool.update(task.gid, gpu_image_views[task.gid], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    initialized_samplers[samplers[task.gid]]);
            }

            finalize_queue.pop_front();
        }

        for (const auto& task : tasks) {
            if (ref_counts[task.gid] != 0 && !deleted[task.gid] && generations[task.gid] == task.generation) {
                finalize_queue.emplace_back(finish_timeline, task);
            }
        }
    }

    void init() {
        texture_pool = TexturePool{1000};

        void* data = malloc(4);
        std::memset(data, 0xFF, 4);

        auto default_tex =
            add((uint8_t*)data, 4 * sizeof(uint8_t), 1, 1, VK_FORMAT_R8G8B8A8_UNORM, "default texture", Sampler{});

        ref_counts[default_tex]++;
        task task{
            .gid = default_tex,
            .generation = generations[default_tex],
        };
        upload_queue.enqueue(task);
    }

    void destroy() {
        for (auto& sampler : initialized_samplers) {
            vkDestroySampler(device, sampler, nullptr);
        }

        for (std::size_t i = 0; i < names.size(); i++) {
            free(blobs[i].first);
            if (ref_counts[i] == 0) continue;

            gpu_images[i].destroy();
            GPUImageView::destroy(gpu_image_views[i]);
        }

        texture_pool.destroy();
    }

    void load(uint8_t* file_data, uint32_t file_size) {
        uint32_t off = 0;

        uint32_t texture_count;
        std::memcpy(&texture_count, file_data + off, sizeof(uint32_t));
        off += sizeof(uint32_t);

        uint32_t sampler_count;
        std::memcpy(&sampler_count, file_data + off, sizeof(uint32_t));
        off += sizeof(uint32_t);

        for (uint32_t i = 0; i < texture_count; i++) {
            uint32_t texture_offset;
            std::memcpy(&texture_offset, file_data + off, sizeof(uint32_t));
            off += sizeof(uint32_t);

            deserialize_texture((VkSamplerCreateInfo*)(file_data + sizeof(uint32_t) + sizeof(uint32_t) +
                                                       sizeof(uint32_t) * texture_count),
                                file_data, texture_offset);
        }
    }

    uint8_t* save(uint32_t& size) {
        uint32_t texture_count = names.size() - 1;
        uint32_t sampler_count = sampler_prototypes.size();

        uint32_t header_size = sizeof(uint32_t) + texture_count * sizeof(uint32_t) + sizeof(uint32_t) +
                               sampler_count * sizeof(VkSamplerCreateInfo);
        uint32_t total_size = header_size;

        for (uint32_t gid = 1; gid <= texture_count; gid++) {
            total_size += get_texture_store_size(gid);
        }

        size = total_size;
        uint8_t* out = (uint8_t*)malloc(total_size);
        uint32_t off = 0;

        std::memcpy(out + off, &texture_count, sizeof(uint32_t));
        off += sizeof(uint32_t);

        std::memcpy(out + off, &sampler_count, sizeof(uint32_t));
        off += sizeof(uint32_t);

        uint32_t texture_offset = header_size;
        for (uint32_t gid = 1; gid <= texture_count; gid++) {
            std::memcpy(out + off, &texture_offset, sizeof(uint32_t));

            serialize_texture(out + texture_offset, gid);

            off += sizeof(uint32_t);
            texture_offset += get_texture_store_size(gid);
        }

        for (uint32_t sampler_gid = 0; sampler_gid < sampler_count; sampler_gid++) {
            const auto& sampler = sampler_prototypes[sampler_gid];

            serialize_sampler(out + off, sampler);
            off += sizeof(VkSamplerCreateInfo);
        }

        return out;
    }

    uint32_t get_texture_store_size(uint32_t gid) {
        uint32_t total = sizeof(uint32_t) + names[gid].size() + sizeof(uint32_t) + paths[gid].string().size() +
                         sizeof(uint32_t) + sizeof(uint32_t);

        if (auto blob_size = blobs[gid].second; blob_size != 0) {
            total += blob_size + sizeof(Metadata);
        }

        return total;
    }

    void serialize_texture(uint8_t* out, uint32_t gid) {
        uint32_t off = 0;

        auto sampler_gid = samplers[gid];
        std::memcpy(out + off, &sampler_gid, sizeof(uint32_t));
        off += sizeof(uint32_t);

        const auto& name = names[gid];
        uint32_t name_size = name.size();
        std::memcpy(out + off, &name_size, sizeof(uint32_t));
        off += sizeof(uint32_t);

        std::memcpy(out + off, name.data(), name_size);
        off += name_size;

        const auto& path_str = paths[gid].string();
        uint32_t path_size = path_str.size();
        std::memcpy(out + off, &path_size, sizeof(uint32_t));
        off += sizeof(uint32_t);

        std::memcpy(out + off, path_str.data(), path_size);
        off += path_size;

        const auto& blob = blobs[gid];

        std::memcpy(out + off, &blob.second, sizeof(uint32_t));
        off += sizeof(uint32_t);

        if (blob.second == 0) return;

        const auto& metadata = metadatas[gid];
        std::memcpy(out + off, &metadata, sizeof(Metadata));
        off += sizeof(Metadata);

        std::memcpy(out + off, blob.first, blob.second);
        off += blob.second;
    }

    void deserialize_texture(const VkSamplerCreateInfo* samplers, uint8_t* data, uint32_t off) {
        uint32_t sampler_gid;
        std::memcpy(&sampler_gid, data + off, sizeof(uint32_t));
        off += sizeof(uint32_t);

        Sampler sampler{};
        sampler._info = samplers[sampler_gid];

        uint32_t name_size;
        std::memcpy(&name_size, data + off, sizeof(uint32_t));
        off += sizeof(uint32_t);

        const uint8_t* name_data = data + off;
        off += name_size;

        uint32_t path_size;
        std::memcpy(&path_size, data + off, sizeof(uint32_t));
        off += sizeof(uint32_t);

        const uint8_t* path_data = data + off;
        off += path_size;

        uint32_t blob_size;
        std::memcpy(&blob_size, data + off, sizeof(uint32_t));
        off += sizeof(uint32_t);

        if (blob_size != 0) {
            Metadata metadata{};
            std::memcpy(&metadata, data + off, sizeof(Metadata));
            off += sizeof(Metadata);

            uint8_t* blob = (uint8_t*)malloc(blob_size);
            std::memcpy(blob, data + off, blob_size);
            off += blob_size;

            add(blob, blob_size, metadata.width, metadata.height, metadata.format,
                std::string{(const char*)name_data, name_size}, sampler);
        } else {
            add(std::string{(const char*)path_data, path_size}, std::string{(const char*)name_data, name_size},
                sampler);
        }
    }

    void serialize_sampler(uint8_t* out, const Sampler& prototype) {
        std::memcpy(out, &prototype._info, sizeof(VkSamplerCreateInfo));
    }

    Sampler deserialize_sampler(uint8_t* in) {
        Sampler res{};
        std::memcpy(&res._info, in, sizeof(VkSamplerCreateInfo));
        return res;
    }

    uint32_t add(std::filesystem::path path, std::string name, const Sampler& sampler) {
        auto sampler_ix = acquire_sampler(sampler);

        if (auto gid_ = find_empty_gid(); gid_) {
            auto gid = *gid_;

            names[gid] = std::move(name);
            paths[gid] = std::move(path);
            blobs[gid] = {nullptr, 0};
            metadatas[gid] = Metadata{};
            ref_counts[gid] = 0;
            gpu_images[gid] = GPUImage{};
            gpu_image_views[gid] = nullptr;
            samplers[gid] = sampler_ix;

            deleted[gid] = false;
            generations[gid]++;

            return gid;
        } else {
            std::lock_guard lock{gid_read};

            if (auto cap = texture_pool.get_capacity(); cap <= names.size()) {
                texture_pool.destroy();
                texture_pool = TexturePool{(uint32_t)(cap * 1.5)};
                rebuild_pool();
            }

            names.emplace_back(std::move(name));
            paths.emplace_back(std::move(path));
            blobs.emplace_back(nullptr, 0);
            metadatas.emplace_back();
            ref_counts.emplace_back(0);
            gpu_images.emplace_back();
            gpu_image_views.emplace_back();
            samplers.emplace_back(sampler_ix);

            deleted.emplace_back(false);
            generations.emplace_back(0);

            return names.size() - 1;
        }
    }

    uint32_t add(uint8_t* data, uint32_t data_size, uint32_t width, uint32_t height, VkFormat format, std::string name,
                 const Sampler& sampler) {
        auto sampler_ix = acquire_sampler(sampler);

        if (auto gid_ = find_empty_gid(); gid_) {
            auto gid = *gid_;

            names[gid] = std::move(name);
            paths.emplace_back();
            blobs[gid] = {(uint8_t*)data, data_size};

            metadatas[gid] = Metadata{
                .width = width,
                .height = height,
                .format = format,
            };
            ref_counts[gid] = 0;
            gpu_images[gid] = GPUImage{};
            gpu_image_views[gid] = nullptr;
            samplers[gid] = sampler_ix;

            deleted[gid] = false;
            generations[gid]++;

            return gid;
        } else {
            std::lock_guard lock{gid_read};

            if (auto cap = texture_pool.get_capacity(); cap <= names.size()) {
                texture_pool.destroy();
                texture_pool = TexturePool{(uint32_t)(cap * 1.5)};
                rebuild_pool();
            }

            names.emplace_back(std::move(name));
            paths.emplace_back();
            blobs.emplace_back((uint8_t*)data, data_size);

            metadatas.emplace_back(Metadata{
                .width = width,
                .height = height,
                .format = format,
            });
            ref_counts.emplace_back(0);
            gpu_images.emplace_back();
            gpu_image_views.emplace_back();
            samplers.emplace_back(sampler_ix);

            deleted.emplace_back(false);
            generations.emplace_back(0);

            return names.size() - 1;
        }
    }

    bool remove(uint32_t gid) {
        if (ref_counts[gid] > 0) return false;
        std::lock_guard lock{gid_read};

        gpu_images[gid].destroy();
        GPUImageView::destroy(gpu_image_views[gid]);
        release_sampler(samplers[gid]);

        names[gid] = "";
        paths[gid] = "";
        free(blobs[gid].first);
        blobs[gid] = {nullptr, 0};
        metadatas[gid] = Metadata{};

        deleted[gid] = true;

        texture_pool.update(gid, texture_pool::default_texture_view, texture_pool::default_texture_layout,
                            texture_pool::default_sampler);

        return true;
    }

    std::string& get_name(uint32_t gid) {
        return names[gid];
    }

    const std::filesystem::path& get_path(uint32_t gid) {
        return paths[gid];
    }

    std::span<const uint8_t> get_blob(uint32_t gid) {
        return {blobs[gid].first, blobs[gid].second};
    }

    GPUImage get_image(uint32_t gid) {
        return gpu_images[gid];
    }

    VkImageView get_image_view(uint32_t gid) {
        return gpu_image_views[gid];
    }

    Sampler get_sampler(uint32_t gid) {
        return sampler_prototypes[samplers[gid]];
    }

    void change_path(uint32_t gid, std::filesystem::path new_path) {
        std::lock_guard lock{gid_read};

        paths[gid] = std::move(new_path);
        generations[gid]++;

        free(blobs[gid].first);
        blobs[gid].first = nullptr;
        blobs[gid].second = 0;
        gpu_images[gid].destroy();
        GPUImageView::destroy(gpu_image_views[gid]);

        task task{
            .gid = gid,
            .generation = generations[gid],
        };
        upload_queue.enqueue(task);
    }

    void change_data(uint32_t gid, uint8_t* data, uint32_t data_size) {
        std::lock_guard lock{gid_read};

        paths[gid] = "";
        generations[gid]++;

        free(blobs[gid].first);
        gpu_images[gid].destroy();
        GPUImageView::destroy(gpu_image_views[gid]);

        void* blob = malloc(data_size);
        std::memcpy(blob, data, data_size);

        blobs[gid].first = (uint8_t*)blob;
        blobs[gid].second = data_size;

        task task{
            .gid = gid,
            .generation = generations[gid],
        };
        upload_queue.enqueue(task);
    }

    void acquire(const uint32_t* gids, uint32_t count) {
        for (std::size_t i = 0; i < count; i++) {
            auto gid = gids[i];
            if (++ref_counts[gid] != 1) continue;

            task task{
                .gid = gid,
                .generation = generations[gid],
            };
            upload_queue.enqueue(task);

            texture_pool.update(gid, texture_pool::default_texture_view, texture_pool::default_texture_layout,
                                texture_pool::default_sampler);
        }
    }

    void release(const uint32_t* gids, uint32_t count) {
        for (std::size_t i = 0; i < count; i++) {
            auto gid = gids[i];
            if (--ref_counts[gid] != 0) continue;

            gpu_images[gid].destroy();
            GPUImageView::destroy(gpu_image_views[gid]);
            texture_pool.update(gid, texture_pool::default_texture_view, texture_pool::default_texture_layout,
                                texture_pool::default_sampler);

            gpu_images[gid] = GPUImage{};
            gpu_image_views[gid] = nullptr;
        }
    }

    void rebuild_pool() {
        for (uint32_t gid = 0; gid < names.size(); gid++) {
            if (deleted[gid]) continue;
            if (ref_counts[gid] == 0) continue;

            texture_pool.update(gid, gpu_image_views[gid], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                initialized_samplers[samplers[gid]]);
        }
    }

    const TexturePool& get_texture_pool() {
        return texture_pool;
    }
}
