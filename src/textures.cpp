#include "goliath/textures.hpp"
#include "goliath/mspc_queue.hpp"
#include "goliath/samplers.hpp"
#include "goliath/thread_pool.hpp"
#include "textures_.hpp"

#include "xxHash/xxhash.h"

#include <cstring>
#include <fstream>
#include <mutex>
#include <vulkan/vulkan_core.h>

struct Metadata {
    uint32_t width;
    uint32_t height;
    VkFormat format;
};

namespace engine::textures {
    bool init_called = false;
    std::filesystem::path texture_directory{};

    std::filesystem::path make_texture_path(gid gid) {
        return std::format("{:02X}{:06X}.goi", (uint8_t)gid.gen(), gid.id());
    }

    void to_json(nlohmann::json& j, const gid& gid) {
        j = gid.value;
    }

    void from_json(const nlohmann::json& j, gid& gid) {
        gid.value = j;
    }

    TexturePool texture_pool;

    std::vector<std::string> names{};
    std::vector<uint8_t> generations{};
    std::vector<bool> deleted{};

    std::vector<uint32_t> ref_counts{};
    std::vector<GPUImage> gpu_images{};
    std::vector<VkImageView> gpu_image_views{};
    std::vector<uint32_t> samplers{};

    std::vector<gid> initializing_textures{};
    static constexpr std::size_t initialized_queue_size = 32;
    MSPCQueue<gid, initialized_queue_size> initialized_queue;

    struct upload_task {
        gid gid;
        uint8_t* image_data;
        uint32_t image_size;
        Metadata metadata;
    };

    static constexpr std::size_t upload_queue_size = 64;
    MSPCQueue<upload_task, upload_queue_size> upload_queue;
    std::deque<std::pair<transport2::ticket, gid>> finalize_queue{};
    std::mutex gid_read{};

    std::optional<gid> find_empty_gid() {
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

    bool is_initializing(gid gid) {
        return std::find(initializing_textures.begin(), initializing_textures.end(), gid) !=
               initializing_textures.end();
    }

    struct task {
        enum Type {
            Acquire,
            Add,
        };

        Type type;
        gid gid;
        std::filesystem::path orig_path;
    };

    void load_texture_data(gid gid, uint8_t*& image_data, uint32_t& image_size, Metadata& metadata) {
        std::lock_guard locK{gid_read};

        if (generations[gid.id()] != gid.gen()) return;

        auto path = texture_directory / make_texture_path(gid);

        std::ifstream file{path, std::ios::binary | std::ios::ate};
        auto size_signed = file.tellg();
        if (size_signed < 0) {
            printf("trying to load gid{.gen = %d, .id = %d}\n", gid.gen(), gid.id());
            fflush(stdout);
            assert(false && "texture somehow isn't on disk");
        }

        image_size = (uint32_t)size_signed - sizeof(Metadata);
        image_data = (uint8_t*)malloc(image_size);

        file.seekg(0, std::ios::beg);
        file.read((char*)&metadata, sizeof(Metadata));
        file.read((char*)image_data, image_size);
    }

    void add_texture(gid gid, std::filesystem::path orig_path) {
        std::lock_guard locK{gid_read};

        if (orig_path.extension() == ".goi") {
            std::filesystem::copy(orig_path, texture_directory / make_texture_path(gid));
        } else {
            auto img = Image::load8((const char*)orig_path.c_str());

            auto path = texture_directory / make_texture_path(gid);
            Metadata metadata{
                .width = img.width,
                .height = img.height,
                .format = img.format,
            };

            std::ofstream file{path, std::ios::binary};
            file.write((const char*)&metadata, sizeof(Metadata));
            file.write((const char*)img.data, img.size);
            file.flush();

            img.destroy();
        }
    }

    auto io_pool = make_thread_pool([](task&& task) {
        switch (task.type) {
            case task::Acquire: {
                while (is_initializing(task.gid)) {
                    _mm_pause();
                }

                upload_task up_task{
                    .gid = task.gid,
                };
                load_texture_data(task.gid, up_task.image_data, up_task.image_size, up_task.metadata);
                upload_queue.enqueue(up_task);
                break;
            }
            case task::Add:
                add_texture(task.gid, task.orig_path);
                initialized_queue.enqueue(task.gid);
                break;
        }
    });

    void init(uint32_t init_texture_capacity, std::filesystem::path texture_dir) {
        init_called = true;
        texture_directory = texture_dir;
        texture_pool = TexturePool{std::max<uint32_t>(init_texture_capacity, 1)};

        auto data = (uint8_t*)malloc(4);
        std::memset(data, 0xFF, 4);

        names.emplace_back("Default texture");
        generations.emplace_back(0);
        deleted.emplace_back(false);

        ref_counts.emplace_back(1);
        gpu_images.emplace_back();
        gpu_image_views.emplace_back();
        samplers.emplace_back(0);

        upload_queue.enqueue(upload_task{
            .gid = {0, 0},
            .image_data = data,
            .image_size = 4,
            .metadata =
                Metadata{
                    .width = 1,
                    .height = 1,
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                },
        });
    }

    void destroy() {
        if (!init_called) return;

        for (std::size_t i = 0; i < names.size(); i++) {
            gpu_image::destroy(gpu_images[i]);
            gpu_image_view::destroy(gpu_image_views[i]);
        }

        texture_pool.destroy();
    }

    bool process_uploads() {
        if (!init_called) return false;

        std::vector<upload_task> upload_tasks{};
        upload_queue.drain(upload_tasks);

        std::vector<gid> initialized_gids{};
        initialized_queue.drain(initialized_gids);

        if (upload_tasks.size() != 0) {
            for (const auto& up_task : upload_tasks) {
                auto gid = up_task.gid;
                if (generations[gid.id()] == gid.gen() && ref_counts[gid.id()] != 0 && !deleted[gid.id()]) {
                    auto metadata = up_task.metadata;
                    transport2::ticket ticket{};
                    auto image = gpu_image::upload(names[gid.id()].c_str(),
                                                   GPUImageInfo{}
                                                       .width(metadata.width)
                                                       .height(metadata.height)
                                                       .format(metadata.format)
                                                       .data(up_task.image_data, free, ticket, false)
                                                       .size(up_task.image_size)
                                                       .new_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                                       .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT),
                                                   VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                    gpu_images[gid.id()] = image;
                    gpu_image_views[gid.id()] =
                        gpu_image_view::create(GPUImageView{image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));

                    finalize_queue.emplace_back(ticket, gid);
                }
            }
        }

        while (finalize_queue.size() > 0 && transport2::is_ready(finalize_queue.front().first)) {
            auto gid = finalize_queue.front().second;

            if (generations[gid.id()] == gid.gen() && ref_counts[gid.id()] != 0 && !deleted[gid.id()]) {
                texture_pool.update(gid.id(), gpu_image_views[gid.id()], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    samplers::get(samplers[gid.id()]));
            }

            finalize_queue.pop_front();
        }

        bool initialized = false;
        std::erase_if(initializing_textures, [&](auto gid) {
            auto found =
                std::find(initialized_gids.begin(), initialized_gids.end(), gid) != initializing_textures.end();
            initialized |= found;
            return found;
        });

        return initialized;
    }

    void rebuild_pool() {
        assert(init_called);

        for (uint32_t gid = 0; gid < names.size(); gid++) {
            if (deleted[gid]) continue;
            if (ref_counts[gid] == 0) continue;

            texture_pool.update(gid, gpu_image_views[gid], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                samplers::get(samplers[gid]));
        }
    }

    struct JsonTextureEntry {
        std::string name;
        gid gid;
        uint32_t sampler;
    };

    void to_json(nlohmann::json& j, const JsonTextureEntry& entry) {
        j = nlohmann::json{
            {"name", entry.name},
            {"gid", entry.gid},
            {"sampler", entry.sampler},
        };
    }

    void from_json(const nlohmann::json& j, JsonTextureEntry& entry) {
        j["name"].get_to(entry.name);
        j["gid"].get_to(entry.gid);
        j["sampler"].get_to(entry.sampler);
    }

    void load(nlohmann::json j) {
        assert(init_called);

        std::vector<JsonTextureEntry> entries = j;

        names.resize(1);
        generations.resize(1);
        deleted.resize(1);

        ref_counts.resize(1);
        gpu_images.resize(1);
        gpu_image_views.resize(1);
        samplers.resize(1);

        uint32_t id_counter = 1;
        for (auto&& entry : entries) {
            auto gid = entry.gid;
            while (gid.id() > id_counter) {
                names.emplace_back();
                generations.emplace_back(0);
                deleted.emplace_back(true);

                ref_counts.emplace_back(0);
                gpu_images.emplace_back();
                gpu_image_views.emplace_back();
                samplers.emplace_back();

                id_counter++;
            }

            names.emplace_back(std::move(entry.name));
            generations.emplace_back((uint8_t)gid.gen());
            deleted.emplace_back(false);

            ref_counts.emplace_back(0);
            gpu_images.emplace_back();
            gpu_image_views.emplace_back();
            samplers.emplace_back();

            id_counter++;
        }
    }

    nlohmann::json save() {
        assert(init_called);

        std::vector<JsonTextureEntry> entries{};

        for (uint32_t i = 1; i < names.size(); i++) {
            if (deleted[i]) continue;

            entries.emplace_back(JsonTextureEntry{
                .name = names[i],
                .gid = {generations[i], i},
                .sampler = samplers[i],
            });
        }

        return entries;
    }

    gid add(std::filesystem::path path, std::string name, Sampler sampler) {
        assert(init_called);

        auto sampler_ix = samplers::add(sampler);

        gid gid;
        if (auto gid_ = find_empty_gid(); gid_) {
            gid = *gid_;
            uint8_t gid_gen = gid.gen();
            uint8_t gid_id = gid.id();

            names[gid.id()] = std::move(name);
            generations[gid.id()]++;
            deleted[gid.id()] = false;

            ref_counts[gid.id()] = 0;
            gpu_images[gid.id()] = GPUImage{};
            gpu_image_views[gid.id()] = nullptr;
            samplers[gid.id()] = sampler_ix;
        } else {
            std::lock_guard lock{gid_read};

            if (auto cap = texture_pool.get_capacity(); cap <= names.size()) {
                texture_pool.destroy();
                texture_pool = TexturePool{(uint32_t)(cap * 1.5)};
                rebuild_pool();
            }

            gid = {0, (uint32_t)names.size()};

            names.emplace_back(std::move(name));
            generations.emplace_back(0);
            deleted.emplace_back(false);

            ref_counts.emplace_back(0);
            gpu_images.emplace_back();
            gpu_image_views.emplace_back();
            samplers.emplace_back(sampler_ix);
        }

        initializing_textures.emplace_back(gid);
        io_pool.enqueue({task::Add, gid, path});

        return gid;
    }

    gid add(std::span<uint8_t> image, uint32_t width, uint32_t height, VkFormat format, std::string name,
            Sampler sampler) {
        assert(init_called);

        auto sampler_ix = samplers::add(sampler);

        gid gid;
        if (auto gid_ = find_empty_gid(); gid_) {
            gid = *gid_;
            names[gid.id()] = std::move(name);
            generations[gid.id()]++;
            deleted[gid.id()] = false;

            ref_counts[gid.id()] = 0;
            gpu_images[gid.id()] = GPUImage{};
            gpu_image_views[gid.id()] = nullptr;
            samplers[gid.id()] = sampler_ix;
        } else {
            std::lock_guard lock{gid_read};

            if (auto cap = texture_pool.get_capacity(); cap <= names.size()) {
                texture_pool.destroy();
                texture_pool = TexturePool{(uint32_t)(cap * 1.5)};
                rebuild_pool();
            }

            gid = {0, (uint32_t)names.size()};

            names.emplace_back(std::move(name));
            generations.emplace_back(0);
            deleted.emplace_back(false);

            ref_counts.emplace_back(0);
            gpu_images.emplace_back();
            gpu_image_views.emplace_back();
            samplers.emplace_back(sampler_ix);
        }

        auto path = texture_directory / make_texture_path(gid);
        Metadata metadata{
            .width = width,
            .height = height,
            .format = format,
        };

        std::ofstream file{path, std::ios::binary};
        file.write((const char*)&metadata, sizeof(Metadata));
        file.write((const char*)image.data(), image.size());
        file.flush();

        initializing_textures.emplace_back(gid);
        initialized_queue.enqueue(gid);

        return gid;
    }

    bool remove(gid gid) {
        assert(init_called);

        if (generations[gid.id()] != gid.gen()) return false;
        if (ref_counts[gid.id()] > 0) return false;
        if (deleted[gid.id()]) return false;

        deleted[gid.id()] = true;
        generations[gid.id()]++;

        std::filesystem::remove(texture_directory / make_texture_path(gid));

        gpu_image::destroy(gpu_images[gid.id()]);
        gpu_image_view::destroy(gpu_image_views[gid.id()]);
        samplers::remove(samplers[gid.id()]);

        names[gid.id()] = "";
        gpu_images[gid.id()] = GPUImage{};
        gpu_image_views[gid.id()] = nullptr;
        samplers[gid.id()] = -1;

        return true;
    }

    std::expected<std::string*, Err> get_name(gid gid) {
        assert(init_called);

        if (generations[gid.id()] != gid.gen()) return std::unexpected(Err::BadGeneration);

        return &names[gid.id()];
    }

    std::expected<GPUImage, Err> get_image(gid gid) {
        assert(init_called);

        if (generations[gid.id()] != gid.gen()) return std::unexpected(Err::BadGeneration);

        return gpu_images[gid.id()];
    }

    std::expected<VkImageView, Err> get_image_view(gid gid) {
        assert(init_called);

        if (generations[gid.id()] != gid.gen()) return std::unexpected(Err::BadGeneration);

        return gpu_image_views[gid.id()];
    }

    std::expected<uint32_t, Err> get_sampler(gid gid) {
        assert(init_called);

        if (generations[gid.id()] != gid.gen()) return std::unexpected(Err::BadGeneration);

        return samplers[gid.id()];
    }

    uint8_t get_generation(uint32_t ix) {
        assert(init_called);

        return generations[ix];
    }

    void acquire(const gid* gids, uint32_t count) {
        assert(init_called);

        for (size_t i = 0; i < count; i++) {
            auto gid = gids[i];

            if (generations[gid.id()] != gid.gen()) continue;
            if (++ref_counts[gid.id()] != 1) continue;

            set_default_texture(gid);

            gpu_images[gid.id()] = GPUImage{};
            gpu_image_views[gid.id()] = nullptr;
            io_pool.enqueue({task::Acquire, gid});
        }
    }

    void release(const gid* gids, uint32_t count) {
        assert(init_called);

        for (std::size_t i = 0; i < count; i++) {
            auto gid = gids[i];

            if (generations[gid.id()] != gid.gen()) continue;
            if (ref_counts[gid.id()] == 0 || --ref_counts[gid.id()] != 0) continue;

            gpu_image::destroy(gpu_images[gid.id()]);
            gpu_image_view::destroy(gpu_image_views[gid.id()]);

            gpu_images[gid.id()] = GPUImage{};
            gpu_image_views[gid.id()] = nullptr;
        }
    }

    const TexturePool& get_texture_pool() {
        assert(init_called);

        return texture_pool;
    }

    std::span<std::string> get_names() {
        return names;
    }
}
