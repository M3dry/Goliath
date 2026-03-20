#include "goliath/textures.hpp"
#include "goliath/mspc_queue.hpp"
#include "goliath/samplers.hpp"
#include "goliath/thread_pool.hpp"

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

namespace engine {
    std::filesystem::path make_texture_path(Textures::gid gid) {
        return std::format("{:02X}{:06X}.goi", (uint8_t)gid.gen(), gid.id());
    }

    void to_json(nlohmann::json& j, const Textures::gid& gid) {
        j = gid.value;
    }

    void from_json(const nlohmann::json& j, Textures::gid& gid) {
        gid.value = j;
    }

    struct Metadata {
        uint32_t width;
        uint32_t height;
        VkFormat format;
    };

    struct upload_task {
        Textures::gid gid;
        uint8_t* image_data;
        uint32_t image_size;
        Metadata metadata;
    };

    struct task {
        enum Type {
            Acquire,
            Add,
        };

        Type type;
        Textures* texs;
        Textures::gid gid;
        std::filesystem::path orig_path;
    };

    struct textures::TexturesImpl {
        static TexturesImpl* make() {
            return new TexturesImpl{};
        }

        static constexpr std::size_t upload_queue_size = 64;
        MSPCQueue<upload_task, upload_queue_size> upload_queue{};

        std::vector<Textures::gid> initializing_textures{};
        static constexpr std::size_t initialized_queue_size = 32;
        MSPCQueue<Textures::gid, initialized_queue_size> initialized_queue{};

        std::mutex gid_read{};

        bool is_initializing(Textures::gid gid) {
            return std::find(initializing_textures.begin(), initializing_textures.end(), gid) !=
                   initializing_textures.end();
        }

        void load_texture_data(Textures& texs, Textures::gid gid, uint8_t*& image_data, uint32_t& image_size,
                               Metadata& metadata) {
            std::lock_guard locK{gid_read};

            if (texs.generations[gid.id()] != gid.gen()) return;

            auto path = texs.texture_directory / make_texture_path(gid);

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

        void add_texture(Textures& texs, Textures::gid gid, std::filesystem::path orig_path) {
            std::lock_guard locK{gid_read};

            if (orig_path.extension() == ".goi") {
                std::filesystem::copy(orig_path, texs.texture_directory / make_texture_path(gid));
            } else {
                auto img = Image::load8((const char*)orig_path.c_str());

                auto path = texs.texture_directory / make_texture_path(gid);
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

        static decltype(auto) thread_pool() {
            return make_thread_pool([](task&& task) {
                switch (task.type) {
                    case task::Acquire: {
                        while (task.texs->impl->is_initializing(task.gid)) {
                            _mm_pause();
                        }

                        upload_task up_task{
                            .gid = task.gid,
                        };
                        task.texs->impl->load_texture_data(*task.texs, task.gid, up_task.image_data, up_task.image_size,
                                                           up_task.metadata);
                        task.texs->impl->upload_queue.enqueue(up_task);
                        break;
                    }
                    case task::Add:
                        task.texs->impl->add_texture(*task.texs, task.gid, task.orig_path);
                        task.texs->impl->initialized_queue.enqueue(task.gid);
                        break;
                }
            });
        };
    };

    auto io_pool = textures::TexturesImpl::thread_pool();

    Textures::Textures(const char* textures_directry, size_t texture_capacity)
        : texture_directory(textures_directry), texture_pool(std::max<uint32_t>(texture_capacity, 1)),
          impl(textures::TexturesImpl::make()) {
        auto data = (uint8_t*)malloc(4);
        std::memset(data, 0xFF, 4);

        names.emplace_back("Default texture");
        generations.emplace_back(0);
        deleted.emplace_back(false);

        ref_counts.emplace_back(1);
        gpu_images.emplace_back();
        gpu_image_views.emplace_back();
        sampler_prototypes.emplace_back();
        samplers.emplace_back(sampler::create({}));

        impl->upload_queue.enqueue(upload_task{
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

    Textures::~Textures() {
        for (std::size_t i = 0; i < names.size(); i++) {
            gpu_image::destroy(gpu_images[i]);
            gpu_image_view::destroy(gpu_image_views[i]);
        }

        texture_pool.destroy();
        delete impl;
    }

    void Textures::process_uploads() {
        std::vector<upload_task> upload_tasks{};
        impl->upload_queue.drain(upload_tasks);

        std::vector<gid> initialized_gids{};
        impl->initialized_queue.drain(initialized_gids);

        if (upload_tasks.size() != 0) {
            for (const auto& up_task : upload_tasks) {
                auto gid = up_task.gid;
                if (generations[gid.id()] == gid.gen() && ref_counts[gid.id()] != 0 && !deleted[gid.id()]) {
                    auto metadata = up_task.metadata;
                    transport2::ticket ticket{};
                    samplers[gid.id()] = sampler::create(sampler_prototypes[gid.id()]);
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

                    gpu_image_views[gid.id()] =
                        gpu_image_view::create(GPUImageView{image}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
                    gpu_images[gid.id()] = std::move(image);

                    finalize_queue.emplace_back(ticket, gid);
                }
            }
        }

        while (finalize_queue.size() > 0 && transport2::is_ready(finalize_queue.front().first)) {
            auto gid = finalize_queue.front().second;

            if (generations[gid.id()] == gid.gen() && ref_counts[gid.id()] != 0 && !deleted[gid.id()]) {
                texture_pool.update(gid.id(), gpu_image_views[gid.id()], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    samplers[gid.id()]);
            }

            finalize_queue.pop_front();
        }

        bool initialized = false;
        std::erase_if(impl->initializing_textures, [&](auto gid) {
            auto found =
                std::find(initialized_gids.begin(), initialized_gids.end(), gid) != impl->initializing_textures.end();
            initialized |= found;
            return found;
        });

        want_save |= initialized;
    }

    void Textures::rebuild_pool() {
        for (uint32_t gid = 0; gid < names.size(); gid++) {
            if (deleted[gid]) continue;
            if (ref_counts[gid] == 0) continue;

            texture_pool.update(gid, gpu_image_views[gid], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                samplers[gid]);
        }
    }

    struct JsonTextureEntry {
        std::string name;
        Textures::gid gid;
        Sampler sampler;
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

    void Textures::load(nlohmann::json j) {
        std::vector<JsonTextureEntry> entries = j;

        names.resize(1);
        generations.resize(1);
        deleted.resize(1);

        ref_counts.resize(1);
        gpu_images.resize(1);
        gpu_image_views.resize(1);
        sampler_prototypes.resize(1);
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
                sampler_prototypes.emplace_back();
                samplers.emplace_back();

                id_counter++;
            }

            printf("adding gid: %d %d, %s %d\n", gid.gen(), gid.id(), make_texture_path(gid).c_str(), gid.value);
            names.emplace_back(std::move(entry.name));
            generations.emplace_back((uint8_t)gid.gen());
            deleted.emplace_back(false);

            ref_counts.emplace_back(0);
            gpu_images.emplace_back();
            gpu_image_views.emplace_back();
            sampler_prototypes.emplace_back(entry.sampler);
            samplers.emplace_back();

            id_counter++;
        }
    }

    nlohmann::json Textures::save() const {
        std::vector<JsonTextureEntry> entries{};

        for (uint32_t i = 1; i < names.size(); i++) {
            if (deleted[i]) continue;

            entries.emplace_back(JsonTextureEntry{
                .name = names[i],
                .gid = {generations[i], i},
                .sampler = sampler_prototypes[i],
            });
        }

        return entries;
    }

    Textures::gid Textures::add(std::filesystem::path path, std::string name, Sampler sampler) {
        auto vk_sampler = sampler::create(sampler);

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
            sampler_prototypes[gid.id()] = sampler;
            samplers[gid.id()] = vk_sampler;

            gid = {generations[gid.gen()], gid.id()};
        } else {
            std::lock_guard lock{impl->gid_read};

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
            sampler_prototypes.emplace_back(sampler);
            samplers.emplace_back(vk_sampler);
        }

        impl->initializing_textures.emplace_back(gid);
        io_pool.enqueue({task::Add, this, gid, path});

        return gid;
    }

    Textures::gid Textures::add(std::span<uint8_t> image, uint32_t width, uint32_t height, VkFormat format,
                                std::string name, Sampler sampler) {
        auto vk_sampler = sampler::create(sampler);

        gid gid;
        if (auto gid_ = find_empty_gid(); gid_) {
            gid = *gid_;
            names[gid.id()] = std::move(name);
            generations[gid.id()]++;
            deleted[gid.id()] = false;

            ref_counts[gid.id()] = 0;
            gpu_images[gid.id()] = GPUImage{};
            gpu_image_views[gid.id()] = nullptr;
            sampler_prototypes[gid.id()] = sampler;
            samplers[gid.id()] = vk_sampler;

            gid = {generations[gid.id()], gid.id()};
        } else {
            std::lock_guard lock{impl->gid_read};

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
            sampler_prototypes.emplace_back(sampler);
            samplers.emplace_back(vk_sampler);
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

        impl->initializing_textures.emplace_back(gid);
        impl->initialized_queue.enqueue(gid);

        return gid;
    }

    bool Textures::remove(gid gid) {
        if (gid == Textures::gid{0, 0}) return false;

        if (generations[gid.id()] != gid.gen()) return false;
        if (deleted[gid.id()]) return false;

        deleted[gid.id()] = true;
        generations[gid.id()]++;

        std::filesystem::remove(texture_directory / make_texture_path(gid));

        gpu_image::destroy(gpu_images[gid.id()]);
        gpu_image_view::destroy(gpu_image_views[gid.id()]);
        sampler::destroy(samplers[gid.id()]);

        names[gid.id()] = "";
        gpu_images[gid.id()] = GPUImage{};
        gpu_image_views[gid.id()] = nullptr;
        sampler_prototypes[gid.id()] = {};
        samplers[gid.id()] = nullptr;

        modified();

        return true;
    }

    std::expected<std::string*, textures::Err> Textures::get_name(gid gid) {
        if (generations[gid.id()] != gid.gen()) return std::unexpected(textures::Err::BadGeneration);

        return &names[gid.id()];
    }

    std::expected<VkImage, textures::Err> Textures::get_image(gid gid) {
        if (generations[gid.id()] != gid.gen()) return std::unexpected(textures::Err::BadGeneration);

        return gpu_images[gid.id()].image;
    }

    std::expected<VkImageView, textures::Err> Textures::get_image_view(gid gid) {
        if (generations[gid.id()] != gid.gen()) return std::unexpected(textures::Err::BadGeneration);

        return gpu_image_views[gid.id()];
    }

    std::expected<VkSampler, textures::Err> Textures::get_sampler(gid gid) {
        if (generations[gid.id()] != gid.gen()) return std::unexpected(textures::Err::BadGeneration);

        return samplers[gid.id()];
    }

    std::expected<Sampler, textures::Err> Textures::get_sampler_prototype(gid gid) {
        if (generations[gid.id()] != gid.gen()) return std::unexpected(textures::Err::BadGeneration);

        return sampler_prototypes[gid.id()];
    }

    uint8_t Textures::get_generation(uint32_t ix) const {
        return generations[ix];
    }

    bool Textures::is_deleted(gid gid) const {
        if (generations[gid.gen()] > gid.gen()) return true;
        return deleted[gid.id()];
    }

    void Textures::acquire(std::span<const gid> gids) {
        for (size_t i = 0; i < gids.size(); i++) {
            auto gid = gids[i];
            if (gid == Textures::gid{}) continue;
            if (generations[gid.id()] != gid.gen()) continue;
            if (++ref_counts[gid.id()] != 1) continue;

            set_default_texture(gid);

            gpu_images[gid.id()] = GPUImage{};
            gpu_image_views[gid.id()] = nullptr;
            io_pool.enqueue({task::Acquire, this, gid});
        }
    }

    void Textures::release(std::span<const gid> gids) {
        for (std::size_t i = 0; i < gids.size(); i++) {
            auto gid = gids[i];
            if (gid == Textures::gid{}) continue;
            if (generations[gid.id()] != gid.gen()) continue;
            if (ref_counts[gid.id()] == 0 || --ref_counts[gid.id()] != 0) continue;

            gpu_image::destroy(gpu_images[gid.id()]);
            gpu_image_view::destroy(gpu_image_views[gid.id()]);

            gpu_images[gid.id()] = GPUImage{};
            gpu_image_views[gid.id()] = nullptr;

            sampler::destroy(samplers[gid.id()]);
        }
    }

    const TexturePool& Textures::get_texture_pool() const {
        return texture_pool;
    }

    std::span<std::string> Textures::get_names() {
        return names;
    }

    bool Textures::want_to_save() {
        auto res = want_save;
        want_save = false;
        return res;
    }

    void Textures::modified() {
        want_save = true;
    }
}
