#pragma once

#include "GLFW/glfw3.h"
#include "goliath/buffer.hpp"
#include "goliath/compute.hpp"
#include "goliath/material.hpp"
#include "goliath/models.hpp"
#include "goliath/rendering.hpp"
#include "goliath/samplers.hpp"
#include "goliath/textures.hpp"
#include "goliath/transport2.hpp"
#include "goliath/visbuffer.hpp"
#include <cstdint>
#include <expected>
#include <glm/ext/vector_float2.hpp>
#include <vulkan/vulkan_core.h>
#define VK_NO_PROTOTYPES
#include <vk_mem_alloc.h>
#include <volk.h>

extern "C" namespace engine::game_interface {
    struct GameConfig;

    using MainFn = GameConfig();

    struct EngineState {
        GLFWwindow** window;

        VkInstance* instance;
        VkPhysicalDevice* physical_device;
        VkPhysicalDeviceProperties* physical_device_properties;
        VkDevice* device;
        VmaAllocator* allocator;

        VkSurfaceKHR* surface;
        const VkFormat* swapchain_format;
        VkExtent2D* swapchain_extent;
        VkSwapchainKHR* swapchain;
        VkImage* (*swapchain_images)(uint32_t* size);
        VkImageView* (*swapchain_image_views)(uint32_t* size);

        VkQueue* graphics_queue;
        uint32_t* graphics_queue_family;
        VkQueue* transport_queue;
        uint32_t* transport_queue_family;
    };

    struct EngineService {
        struct SamplerServicePtrs {
            VkSampler (*create)(const Sampler* prototype);
            void (*destroy)(VkSampler sampler);
        };

        class SamplerService {
          private:
            SamplerServicePtrs _ptrs;

          public:
            SamplerService(SamplerServicePtrs ptrs) : _ptrs(ptrs) {}
            SamplerService() {}
            operator SamplerServicePtrs() const {
                return _ptrs;
            }

            VkSampler create(const Sampler& prototype) const {
                return _ptrs.create(&prototype);
            }

            void destroy(VkSampler sampler) const {
                _ptrs.destroy(sampler);
            }
        };

        struct SamplersServicePtrs {
            uint32_t (*add)(const Sampler* new_sampler);
            void (*remove)(uint32_t ix);
            VkSampler (*get)(uint32_t ix);
        };

        class SamplersService {
          private:
            SamplersServicePtrs _ptrs;

          public:
            SamplersService(SamplersServicePtrs ptrs) : _ptrs(ptrs) {}
            SamplersService() {}
            operator SamplersServicePtrs() const {
                return _ptrs;
            }

            void remove(uint32_t ix) const {
                _ptrs.remove(ix);
            }

            VkSampler get(uint32_t ix) const {
                return _ptrs.get(ix);
            }

            uint32_t add(const Sampler& new_sampler) const {
                return _ptrs.add(&new_sampler);
            }
        };

        struct TransportServicePtrs {
            VkSemaphoreSubmitInfo (*uploads_to_wait_on)(transport2::ticket* tickets, uint32_t count);
            bool (*is_uploaded)(transport2::ticket ticket);
            transport2::ticket (*upload_buffer)(bool priority, void* src, bool own, transport2::FreeFn* own_fn,
                                                uint32_t size, VkBuffer dst, uint32_t dst_offset,
                                                VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access);
            transport2::ticket (*upload_image)(bool priority, VkFormat format, VkExtent3D dimension, void* src,
                                               bool own, transport2::FreeFn* own_fn, VkImage dst,
                                               VkImageSubresourceLayers dst_layers, VkOffset3D dst_offset,
                                               VkImageLayout current_layout, VkImageLayout dst_layout,
                                               VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access);
        };

        class TransportService {
          private:
            TransportServicePtrs _ptrs;

          public:
            TransportService(TransportServicePtrs ptrs) : _ptrs(ptrs) {}
            TransportService() {}
            operator TransportServicePtrs() const {
                return _ptrs;
            }

            bool is_uploaded(transport2::ticket ticket) const {
                return _ptrs.is_uploaded(ticket);
            }

            VkSemaphoreSubmitInfo uploads_to_wait_on(std::span<transport2::ticket> tickets) const {
                return _ptrs.uploads_to_wait_on(tickets.data(), tickets.size());
            }

            transport2::ticket upload(bool priority, void* src, std::optional<transport2::FreeFn*> own, uint32_t size,
                                      VkBuffer dst, uint32_t dst_offset, VkPipelineStageFlagBits2 dst_stage,
                                      VkAccessFlagBits2 dst_access) const {
                return _ptrs.upload_buffer(priority, src, own.has_value(), own ? *own : nullptr, size, dst, dst_offset,
                                           dst_stage, dst_access);
            }

            transport2::ticket upload(bool priority, VkFormat format, VkExtent3D dimension, void* src,
                                      std::optional<transport2::FreeFn*> own, VkImage dst,
                                      VkImageSubresourceLayers dst_layers, VkOffset3D dst_offset,
                                      VkImageLayout current_layout, VkImageLayout dst_layout,
                                      VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access) const {
                return _ptrs.upload_image(priority, format, dimension, src, own.has_value(), own ? *own : nullptr, dst,
                                          dst_layers, dst_offset, current_layout, dst_layout, dst_stage, dst_access);
            }
        };

        struct TexturesServicePtrs {
            std::string* (*name)(textures::gid gid, textures::Err* err, bool* erred);
            GPUImage (*image)(textures::gid gi, textures::Err* err, bool* erred);
            VkImageView (*image_view)(textures::gid gi, textures::Err* err, bool* erred);
            uint32_t (*sampler)(textures::gid gi, textures::Err* err, bool* erred);

            void (*acquire)(const textures::gid* gids, uint32_t count);
            void (*release)(const textures::gid* gids, uint32_t count);

            const TexturePool* (*texture_pool)();
        };

        class TexturesService {
          private:
            TexturesServicePtrs _ptrs;

          public:
            TexturesService(TexturesServicePtrs ptrs) : _ptrs(ptrs) {}
            TexturesService() {}
            operator TexturesServicePtrs() const {
                return _ptrs;
            }

            const TexturePool& texture_pool() const {
                return *_ptrs.texture_pool();
            }

            void acquire(const std::span<textures::gid>& gids) const {
                _ptrs.acquire(gids.data(), gids.size());
            }

            void release(const std::span<textures::gid>& gids) const {
                _ptrs.release(gids.data(), gids.size());
            }

            std::expected<std::string*, textures::Err> name(textures::gid gid) const {
                bool erred = false;
                textures::Err err;
                auto res = _ptrs.name(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<GPUImage, textures::Err> image(textures::gid gid) const {
                bool erred = false;
                textures::Err err;
                auto res = _ptrs.image(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<VkImageView, textures::Err> image_view(textures::gid gid) const {
                bool erred = false;
                textures::Err err;
                auto res = _ptrs.image_view(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<uint32_t, textures::Err> sampler(textures::gid gid) const {
                bool erred = false;
                textures::Err err;
                auto res = _ptrs.sampler(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }
        };

        struct MaterialsServicePtrs {
            const Material* (*schema)(uint32_t mat_id);
            uint8_t* (*instance_data)(uint32_t mat_id, uint32_t instance_ix, uint32_t* size);
            Buffer (*buffer)();
            void (*set_instance_data)(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data);
        };

        class MaterialsService {
          private:
            MaterialsServicePtrs _ptrs;

          public:
            MaterialsService(MaterialsServicePtrs ptrs) : _ptrs(ptrs) {}
            MaterialsService() {}
            operator MaterialsServicePtrs() const {
                return _ptrs;
            }

            Buffer buffer() const {
                return _ptrs.buffer();
            }

            const Material& schema(uint32_t mat_id) const {
                return *_ptrs.schema(mat_id);
            }

            std::span<uint8_t> instance_data(uint32_t mat_id, uint32_t instance_ix) const {
                uint32_t size;
                auto data = _ptrs.instance_data(mat_id, instance_ix, &size);
                return {data, size};
            }

            void set_instance_data(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data) const {
                _ptrs.set_instance_data(mat_id, instance_ix, new_data);
            }
        };

        struct ModelsServicePtrs {
            std::string* (*name)(models::gid gid, models::Err* err, bool* erred);
            engine::Model* (*cpu_model)(models::gid gid, models::Err* err, bool* erred);
            transport2::ticket (*ticket)(models::gid gid, models::Err* err, bool* erred);
            engine::Buffer (*draw_buffer)(models::gid gid, models::Err* err, bool* erred);
            engine::GPUModel (*gpu_model)(models::gid gid, models::Err* err, bool* erred);
            engine::GPUGroup (*gpu_group)(models::gid gid, models::Err* err, bool* erred);

            models::LoadState (*is_loaded)(models::gid gid, models::Err* err, bool* erred);

            void (*acquire)(const models::gid* gids, uint32_t count);
            void (*release)(const models::gid* gids, uint32_t count);
        };

        class ModelsService {
          private:
            ModelsServicePtrs _ptrs;

          public:
            ModelsService(ModelsServicePtrs ptrs) : _ptrs(ptrs) {}
            ModelsService() {}
            operator ModelsServicePtrs() const {
                return _ptrs;
            }

            void acquire(const std::span<models::gid>& gids) const {
                _ptrs.acquire(gids.data(), gids.size());
            }

            void release(const std::span<models::gid>& gids) const {
                _ptrs.release(gids.data(), gids.size());
            }

            std::expected<std::string*, models::Err> name(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = _ptrs.name(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::Model*, models::Err> cpu_model(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = _ptrs.cpu_model(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<transport2::ticket, models::Err> ticket(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = _ptrs.ticket(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::Buffer, models::Err> draw_buffer(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = _ptrs.draw_buffer(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::GPUModel, models::Err> gpu_model(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = _ptrs.gpu_model(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::GPUGroup, models::Err> gpu_group(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = _ptrs.gpu_group(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<models::LoadState, models::Err> is_loaded(models::gid gid) const {
                bool erred = false;
                models::Err err;
                auto res = _ptrs.is_loaded(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }
        };

        struct ComputeServicePtrs {
            ComputePipeline (*create)(const ComputePipelineBuilder* builder);
            void (*destroy)(ComputePipeline* pipeline);
        };

        class ComputeService {
          private:
            ComputeServicePtrs _ptrs;

          public:
            ComputeService(ComputeServicePtrs ptrs) : _ptrs(ptrs) {}
            ComputeService() {}
            operator ComputeServicePtrs() const {
                return _ptrs;
            }

            ComputePipeline create(const ComputePipelineBuilder& builder) const {
                return _ptrs.create(&builder);
            }

            void destroy(ComputePipeline& pipeline) const {
                _ptrs.destroy(&pipeline);
            }
        };

        struct GPUGroupServicePtrs {
            uint32_t (*upload)(uint32_t texture_gid_count, uint32_t data_size,
                               void (*upload_ptr)(uint8_t*, uint32_t, uint32_t, textures::gid*, uint32_t, void*),
                               void* ctx);
            void (*begin)();
            [[nodiscard]] GPUGroup (*end)(bool priority, VkBufferUsageFlags usage_flags,
                                          VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access);
        };

        class GPUGroupService {
          private:
            GPUGroupServicePtrs _ptrs;

          public:
            GPUGroupService(GPUGroupServicePtrs ptrs) : _ptrs(ptrs) {}
            GPUGroupService() {}
            operator GPUGroupServicePtrs() const {
                return _ptrs;
            }

            void begin() const {
                _ptrs.begin();
            }

            [[nodiscard]] GPUGroup end(bool priority, VkBufferUsageFlags usage_flags,
                                       VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access) const {
                return _ptrs.end(priority, usage_flags, dst_stage, dst_access);
            }

            uint32_t upload(uint32_t texture_gid_count, uint32_t data_size,
                            void (*upload_ptr)(uint8_t*, uint32_t, uint32_t, textures::gid*, uint32_t, void*),
                            void* ctx = nullptr) const {
                return _ptrs.upload(texture_gid_count, data_size, upload_ptr, ctx);
            }
        };

        struct ShaderServicePtrs {
            VkShaderModule (*create)(uint8_t* code, uint32_t code_size);
            void (*destroy)(VkShaderModule shader);
        };

        class ShaderService {
          private:
            ShaderServicePtrs _ptrs;

          public:
            ShaderService(ShaderServicePtrs ptrs) : _ptrs(ptrs) {}
            ShaderService() {}
            operator ShaderServicePtrs() const {
                return _ptrs;
            }

            void destroy(VkShaderModule shader) const {
                _ptrs.destroy(shader);
            }

            VkShaderModule create(std::span<uint8_t> code) const {
                return _ptrs.create(code.data(), code.size());
            }
        };

        struct RenderingServicePtrs {
            GraphicsPipeline (*create_pipeline)(const GraphicsPipelineBuilder* builder);
            void (*destroy_pipeline)(GraphicsPipeline* pipeline);
        };

        class RenderingService {
          private:
            RenderingServicePtrs _ptrs;

          public:
            RenderingService(RenderingServicePtrs ptrs) : _ptrs(ptrs) {}
            RenderingService() {}
            operator RenderingServicePtrs() const {
                return _ptrs;
            }

            GraphicsPipeline create_pipeline(const GraphicsPipelineBuilder& builder) const {
                return _ptrs.create_pipeline(&builder);
            }

            void destroy_pipeline(GraphicsPipeline& pipeline) const {
                _ptrs.destroy_pipeline(&pipeline);
            }
        };

        struct VisBufferServicePtrs {
            VkDescriptorSetLayout* shading_layout;

            VisBuffer (*create)(glm::uvec2 dimensions);
            void (*resize)(VisBuffer* visbuffer, glm::uvec2 new_dims);
            void (*destroy)(VisBuffer* visbuffer);

            void (*push_material)(VisBuffer* visbuffer, uint16_t n);
            void (*pop_material)(VisBuffer* visbuffer, uint16_t n);
        };

        class VisBufferService {
          private:
            VisBufferServicePtrs _ptrs;

          public:
            VisBufferService(VisBufferServicePtrs ptrs) : _ptrs(ptrs) {}
            VisBufferService() {}
            operator VisBufferServicePtrs() const {
                return _ptrs;
            }

            VkDescriptorSetLayout shading_layout() const {
                return *_ptrs.shading_layout;
            }

            VisBuffer create(glm::uvec2 dimensions) const {
                return _ptrs.create(dimensions);
            }

            void resize(VisBuffer& visbuffer, glm::uvec2 new_dims) const {
                return _ptrs.resize(&visbuffer, new_dims);
            }

            void destroy(VisBuffer& visbuffer) const {
                _ptrs.destroy(&visbuffer);
            }

            void push_material(VisBuffer& visbuffer, uint16_t n = 1) const {
                _ptrs.push_material(&visbuffer, n);
            }

            void pop_material(VisBuffer& visbuffer, uint16_t n = 1) const {
                _ptrs.pop_material(&visbuffer, n);
            }
        };

        struct CullingServicePtrs {
            void (*resize)(uint32_t max_draw_count);
        };

        class CullingService {
          private:
            CullingServicePtrs _ptrs;

          public:
            CullingService(CullingServicePtrs ptrs) : _ptrs(ptrs) {}
            CullingService() {}
            operator CullingServicePtrs() const {
                return _ptrs;
            }

            void resize(uint32_t max_draw_count) const {
                return _ptrs.resize(max_draw_count);
            }
        };

        struct TextureServicePtrs {
            GPUImage (*upload1)(bool priority, bool own, const char* name, const Image* img, VkImageLayout new_layout,
                                transport2::ticket* ticket, VkPipelineStageFlagBits2 dst_stage,
                                VkAccessFlagBits2 dst_access);
            GPUImage (*upload2)(const char* name, const GPUImageInfo* builder, VkPipelineStageFlagBits2 dst_stage,
                                VkAccessFlagBits2 dst_access);

            VkImageView (*create_view)(const GPUImageView* gpu_image_view);

            void (*destroy_image)(GPUImage* gpu_image);
            void (*destroy_view)(VkImageView view);
        };

        class TextureService {
          private:
            TextureServicePtrs _ptrs;

          public:
            TextureService(TextureServicePtrs ptrs) : _ptrs(ptrs) {}
            TextureService() {}
            operator TextureServicePtrs() const {
                return _ptrs;
            }

            void destroy(GPUImage& image) const {
                _ptrs.destroy_image(&image);
            }

            void destroy(VkImageView view) const {
                _ptrs.destroy_view(view);
            }

            VkImageView create_view(const GPUImageView& gpu_image_view) const {
                return _ptrs.create_view(&gpu_image_view);
            }

            GPUImage upload(bool priority, bool own, const char* name, const Image& img, VkImageLayout new_layout,
                            transport2::ticket& ticket, VkPipelineStageFlagBits2 dst_stage,
                            VkAccessFlagBits2 dst_access) const {
                auto gpu_img = _ptrs.upload1(priority, own, name, &img, new_layout, &ticket, dst_stage, dst_access);
                return gpu_img;
            }

            GPUImage upload(const char* name, const GPUImageInfo& builder, VkPipelineStageFlagBits2 dst_stage,
                            VkAccessFlagBits2 dst_access) const {
                auto gpu_img = _ptrs.upload2(name, &builder, dst_stage, dst_access);
                return gpu_img;
            }
        };

        struct BufferServicePtrs {
            Buffer (*create)(const char* name, uint32_t size, VkBufferUsageFlags usage, bool coherent, void** ptr,
                             bool* need_flush, VmaAllocationCreateFlags alloc_flags);
            void (*destroy)(Buffer* buffer);
        };

        class BufferService {
          private:
            BufferServicePtrs _ptrs;

          public:
            BufferService(BufferServicePtrs ptrs) : _ptrs(ptrs) {}
            BufferService() {}
            operator BufferServicePtrs() const {
                return _ptrs;
            }

            Buffer create(const char* name, uint32_t size, VkBufferUsageFlags usage,
                          std::optional<std::pair<void**, bool*>> host,
                          VmaAllocationCreateFlags alloc_flags = 0) const {
                return _ptrs.create(name, size, usage, host.has_value(), host.has_value() ? host->first : nullptr,
                                    host.has_value() ? host->second : nullptr, alloc_flags);
            }

            void destroy(Buffer& buffer) const {
                _ptrs.destroy(&buffer);
            }
        };

        struct DescriptorServicePtrs {
            VkDescriptorSetLayout (*create_layout)(const VkDescriptorSetLayoutCreateInfo* info);
            void (*destroy_layout)(VkDescriptorSetLayout set_layout);
        };

        class DescriptorService {
          private:
            DescriptorServicePtrs _ptrs;

          public:
            DescriptorService(DescriptorServicePtrs ptrs) : _ptrs(ptrs) {}
            DescriptorService() {}
            operator DescriptorServicePtrs() const {
                return _ptrs;
            }

            VkDescriptorSetLayout create_layout(const VkDescriptorSetLayoutCreateInfo& info) const {
                return _ptrs.create_layout(&info);
            }

            void destroy_layout(VkDescriptorSetLayout set_layout) const {
                _ptrs.destroy_layout(set_layout);
            }
        };

        uint32_t frame_ix;
        uint32_t frames_in_flight;
        VkDescriptorSetLayout empty_set;
        EngineState (*get_engine_state)();

        SamplerService sampler;
        SamplersService samplers;
        TransportService transport;
        TexturesService textures;
        MaterialsService materials;
        ModelsService models;
        ComputeService compute;
        GPUGroupService gpu_group;
        ShaderService shader;
        RenderingService rendering;
        VisBufferService visbuffer;
        CullingService culling;
        TextureService texture;
        BufferService buffer;
        DescriptorService descriptor;
    };

    struct TickServicePtrs {
        bool (*is_held)(uint32_t code);
        bool (*was_released)(uint32_t code);

        glm::vec2 (*get_mouse_delta)();
        glm::vec2 (*get_mouse_absolute)();
    };

    class TickService {
      private:
        TickServicePtrs _ptrs;

      public:
        TickService(TickServicePtrs ptrs) : _ptrs(ptrs) {}
        TickService() {}
        operator TickServicePtrs() const {
            return _ptrs;
        }

        bool is_held(uint32_t code) const {
            return _ptrs.is_held(code);
        }

        bool was_released(uint32_t code) const {
            return _ptrs.was_released(code);
        }

        glm::vec2 get_mouse_delta() const {
            return _ptrs.get_mouse_delta();
        }

        glm::vec2 get_mouse_absolute() const {
            return _ptrs.get_mouse_absolute();
        }
    };

    struct FrameService {
        struct DescriptorServicePtrs {
            uint64_t (*new_set)(VkDescriptorSetLayout layout);
            void (*bind_set)(uint64_t id, VkPipelineBindPoint bind_point, VkPipelineLayout layout, uint32_t set);
            void (*update_set)(uint64_t id, VkWriteDescriptorSet* writes, uint32_t write_count);

            void (*begin_update)(uint64_t id);
            void (*end_update)();

            void (*update_sampled_image)(uint32_t binding, VkImageLayout layout, VkImageView view, VkSampler sampler);
            void (*update_storage_image)(uint32_t binding, VkImageLayout layout, VkImageView view);
            void (*update_ubo)(uint32_t binding, uint8_t* ubo, uint32_t ubo_size);
        };

        class DescriptorService {
          private:
            DescriptorServicePtrs _ptrs;

          public:
            DescriptorService(DescriptorServicePtrs ptrs) : _ptrs(ptrs) {}
            DescriptorService() {}
            operator DescriptorServicePtrs() const {
                return _ptrs;
            }

            uint64_t new_set(VkDescriptorSetLayout layout) const {
                return _ptrs.new_set(layout);
            }

            void bind_set(uint64_t id, VkPipelineBindPoint bind_point, VkPipelineLayout layout, uint32_t set) const {
                _ptrs.bind_set(id, bind_point, layout, set);
            }

            void update_set(uint64_t id, VkWriteDescriptorSet* writes, uint32_t write_count) const {
                _ptrs.update_set(id, writes, write_count);
            }

            void begin_update(uint64_t id) const {
                _ptrs.begin_update(id);
            }

            void end_update() const {
                _ptrs.end_update();
            }

            void update_sampled_image(uint32_t binding, VkImageLayout layout, VkImageView view,
                                      VkSampler sampler) const {
                _ptrs.update_sampled_image(binding, layout, view, sampler);
            }

            void update_storage_image(uint32_t binding, VkImageLayout layout, VkImageView view) const {
                _ptrs.update_storage_image(binding, layout, view);
            }

            void update_ubo(uint32_t binding, std::span<uint8_t> ubo) const {
                _ptrs.update_ubo(binding, ubo.data(), ubo.size());
            }
        };

        struct ComputeServicePtrs {
            void (*bind)(const ComputePipeline* pipeline);
            void (*dispatch)(const ComputePipeline* pipeline, const ComputePipeline::DispatchParams* params);
            void (*dispatch_indirect)(const ComputePipeline* pipeline,
                                      const ComputePipeline::IndirectDispatchParams* params);
        };

        class ComputeService {
          private:
            ComputeServicePtrs _ptrs;

          public:
            ComputeService(ComputeServicePtrs ptrs) : _ptrs(ptrs) {}
            ComputeService() {}
            operator ComputeServicePtrs() const {
                return _ptrs;
            }

            void bind(const ComputePipeline& pipeline) const {
                _ptrs.bind(&pipeline);
            }

            void dispatch(const ComputePipeline& pipeline, const ComputePipeline::DispatchParams& params) const {
                _ptrs.dispatch(&pipeline, &params);
            }

            void dispatch_indirect(const ComputePipeline& pipeline,
                                   const ComputePipeline::IndirectDispatchParams& params) const {
                _ptrs.dispatch_indirect(&pipeline, &params);
            }
        };

        struct CullingServicePtrs {
            void (*bind_flatten)();

            void (*flatten)(uint64_t group_addr, uint32_t draw_count, uint64_t draw_buffer_addr,
                            uint64_t transforms_addr, uint32_t default_transform_offset);
            void (*flatten_models)(models::gid gid, uint64_t transforms_addr, uint32_t default_transform_offset,
                                   models::Err* err, bool* erred);

            void (*cull)(uint32_t max_draw_count, uint64_t draw_id_addr, uint64_t indirect_draw_addr);

            void (*sync_for_draw)(Buffer* draw_id_buffer, Buffer* indirect_draw_buffer);

            void (*clear_buffers)(Buffer* draw_ids, Buffer* indirect_draws, VkAccessFlags2 draw_ids_src_access,
                                  VkPipelineStageFlags2 draw_ids_src_stage, VkAccessFlags2 indirect_draws_access,
                                  VkPipelineStageFlags2 indirect_draws_stage);
        };

        class CullingService {
          private:
            CullingServicePtrs _ptrs;

          public:
            CullingService(CullingServicePtrs ptrs) : _ptrs(ptrs) {}
            CullingService() {}
            operator CullingServicePtrs() const {
                return _ptrs;
            }

            void bind_flatten() const {
                _ptrs.bind_flatten();
            }

            void cull(uint32_t max_draw_count, uint64_t draw_id_addr, uint64_t indirect_draw_addr) const {
                _ptrs.cull(max_draw_count, draw_id_addr, indirect_draw_addr);
            }

            void sync_for_draw(Buffer& draw_id_buffer, Buffer& indirect_draw_buffer) const {
                return _ptrs.sync_for_draw(&draw_id_buffer, &indirect_draw_buffer);
            }

            void flatten(uint64_t group_addr, uint32_t draw_count, uint64_t draw_buffer_addr, uint64_t transforms_addr,
                         uint32_t default_transform_offset) const {
                return _ptrs.flatten(group_addr, draw_count, draw_buffer_addr, transforms_addr,
                                     default_transform_offset);
            }

            std::expected<void, models::Err> flatten(models::gid gid, uint64_t transforms_addr,
                                                     uint32_t default_transform_offset) const {
                bool erred = false;
                models::Err err;
                _ptrs.flatten_models(gid, transforms_addr, default_transform_offset, &err, &erred);
                if (erred) return std::unexpected(err);

                return {};
            }

            void
            clear_buffers(Buffer& draw_ids, Buffer& indirect_draws,
                          VkAccessFlags2 draw_ids_src_access = VK_ACCESS_2_SHADER_READ_BIT,
                          VkPipelineStageFlags2 draw_ids_src_stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                          VkAccessFlags2 indirect_draws_access = VK_ACCESS_2_SHADER_READ_BIT |
                                                                 VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                          VkPipelineStageFlags2 indirect_draws_stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                                       VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT) const {
                return _ptrs.clear_buffers(&draw_ids, &indirect_draws, draw_ids_src_access, draw_ids_src_stage,
                                           indirect_draws_access, indirect_draws_stage);
            }
        };

        struct RenderingServicePtrs {
            void (*begin)(VkRenderingInfo info);
            void (*end)();

            void (*bind)(const GraphicsPipeline* pipeline);
            void (*draw)(const GraphicsPipeline* pipeline, const GraphicsPipeline::DrawParams* params);
            void (*draw_indirect)(const GraphicsPipeline* pipeline, const GraphicsPipeline::DrawIndirectParams* params);
            void (*draw_indirect_count)(const GraphicsPipeline* pipeline,
                                        const GraphicsPipeline::DrawIndirectCountParams* params);
        };
        class RenderingService {
          private:
            RenderingServicePtrs _ptrs;

          public:
            RenderingService(RenderingServicePtrs ptrs) : _ptrs(ptrs) {}
            RenderingService() {}
            operator RenderingServicePtrs() const {
                return _ptrs;
            }

            void end() const {
                _ptrs.end();
            }

            void begin(const RenderPass& attachment) const {
                _ptrs.begin(attachment._info);
            }

            void bind(const GraphicsPipeline& pipeline) const {
                _ptrs.bind(&pipeline);
            }

            void draw(const GraphicsPipeline& pipeline, const GraphicsPipeline::DrawParams& params) const {
                _ptrs.draw(&pipeline, &params);
            }

            void draw_indirect(const GraphicsPipeline& pipeline,
                               const GraphicsPipeline::DrawIndirectParams& params) const {
                _ptrs.draw_indirect(&pipeline, &params);
            }

            void draw_indirect_count(const GraphicsPipeline& pipeline,
                                     const GraphicsPipeline::DrawIndirectCountParams& params) const {
                _ptrs.draw_indirect_count(&pipeline, &params);
            }
        };

        struct SynchronizationServicePtrs {
            void (*begin_barriers)();
            void (*end_barriers)();

            void (*apply_barrier_img)(VkImageMemoryBarrier2 barrier);
            void (*apply_barrier_buf)(VkBufferMemoryBarrier2 barrier);
            void (*apply_barrier)(VkMemoryBarrier2 barrier);
        };

        class SynchronizationService {
          private:
            SynchronizationServicePtrs _ptrs;

          public:
            SynchronizationService(SynchronizationServicePtrs ptrs) : _ptrs(ptrs) {}
            SynchronizationService() {}
            operator SynchronizationServicePtrs() const {
                return _ptrs;
            }

            void begin_barriers() const {
                _ptrs.begin_barriers();
            }

            void end_barriers() const {
                _ptrs.end_barriers();
            }

            void apply_barrier(VkImageMemoryBarrier2 barrier) const {
                _ptrs.apply_barrier_img(barrier);
            }

            void apply_barrier(VkBufferMemoryBarrier2 barrier) const {
                _ptrs.apply_barrier_buf(barrier);
            }

            void apply_barrier(VkMemoryBarrier2 barrier) const {
                _ptrs.apply_barrier(barrier);
            }
        };

        struct VisBufferServicePtrs {
            void (*clear_buffers)(VisBuffer* visbuffer, uint32_t current_frame);

            void (*prepare_for_draw)(VisBuffer* visbuffer, uint32_t current_frame);
            void (*count_materials)(VisBuffer* visbuffer, uint64_t draw_id_addr, uint32_t current_frame);
            void (*get_offsets)(VisBuffer* visbuffer, uint32_t current_frame);
            void (*write_fragment_ids)(VisBuffer* visbuffer, uint64_t draw_id_addr, uint32_t current_frame);
            visbuffer::Shading (*shade)(VisBuffer* visbuffer, VkImageView target, uint32_t current_frame);
        };

        class VisBufferService {
          private:
            VisBufferServicePtrs _ptrs;
            uint32_t current_frame;

          public:
            VisBufferService(VisBufferServicePtrs ptrs, uint32_t current_frame) : _ptrs(ptrs), current_frame(current_frame) {}
            VisBufferService() {}
            operator VisBufferServicePtrs() const {
                return _ptrs;
            }

            void clear_buffers(VisBuffer& visbuffer) {
                _ptrs.clear_buffers(&visbuffer, current_frame);
            }

            void prepare_for_draw(VisBuffer& visbuffer) {
                _ptrs.prepare_for_draw(&visbuffer, current_frame);
            }

            void count_materials(VisBuffer& visbuffer, uint64_t draw_id_addr) {
                _ptrs.count_materials(&visbuffer, draw_id_addr, current_frame);
            }

            void get_offsets(VisBuffer& visbuffer) {
                _ptrs.get_offsets(&visbuffer, current_frame);
            }

            void write_fragment_ids(VisBuffer& visbuffer, uint64_t draw_id_addr) {
                _ptrs.write_fragment_ids(&visbuffer, draw_id_addr, current_frame);
            }

            visbuffer::Shading shade(VisBuffer& visbuffer, VkImageView target) {
                return _ptrs.shade(&visbuffer, target, current_frame);
            }
        };

        struct TexturePoolServicePtrs {
            void (*bind)(const TexturePool* texture_pool, VkPipelineBindPoint bind_point, VkPipelineLayout layout);
        };

        class TexturePoolService {
          private:
            TexturePoolServicePtrs _ptrs;

          public:
            TexturePoolService(TexturePoolServicePtrs ptrs) : _ptrs(ptrs) {}
            TexturePoolService() {}
            operator TexturePoolServicePtrs() const {
                return _ptrs;
            }

            void bind(const TexturePool& texture_pool, VkPipelineBindPoint bind_point, VkPipelineLayout layout) const {
                _ptrs.bind(&texture_pool, bind_point, layout);
            }
        };

        VkImage target;
        VkImageView target_view;
        glm::uvec2 target_dimensions;

        DescriptorService descriptor;
        ComputeService compute;
        CullingService culling;
        RenderingService rendering;
        SynchronizationService synchronization;
        VisBufferService visbuffer;
        TexturePoolService texture_pool;
    };

    struct AssetPaths {
        const char* scenes;
        const char* materials;
        const char* models_reg;
        const char* models_dir;

        const char* textures_reg;
        const char* textures_dir;
    };

    using InitFn = void*(const EngineService*, uint32_t, char**);
    using DestroyFn = void(void*, const EngineService*);
    using ResizeFn = void(void*, EngineService*);
    using TickFn = void(void*, const TickService*, const EngineService*);
    using DrawImGuiFn = void(void*, const EngineService*);
    using RenderFn =
        uint32_t(void*, VkCommandBuffer, const FrameService*, const EngineService*, VkSemaphoreSubmitInfo*);

    struct GameFunctions {
        InitFn* init;
        DestroyFn* destroy;
        ResizeFn* resize;

        TickFn* tick;
        DrawImGuiFn* draw_imgui;
        RenderFn* render;
    };

    struct GameConfig {
        enum BlitStrategy {
            LetterBox,
            Stretch,
        };

        const char* name;
        uint32_t tps;
        bool fullscreen;
        VkImageUsageFlags target_usage;
        VkFormat target_format;
        VkImageLayout target_start_layout;
        VkImageLayout target_end_layout;
        VkPipelineStageFlags2 target_finish_stage;
        VkAccessFlags2 target_finish_access;
        // // if == {0, 0}, then it's the same as the window/viewport size, and ResizeFn has to be defined
        glm::uvec2 target_dimensions;
        BlitStrategy target_blit_strategy;
        glm::vec4 clear_color;

        uint32_t max_wait_count;

        GameFunctions funcs;
    };

    void make_engine_service(EngineService*);
    void make_frame_service(FrameService*);
    void make_tick_service(TickService*);

    void update_frame_service(FrameService*, uint32_t current_frame);

    void start(const GameConfig& config, const AssetPaths& asset_paths, uint32_t argc, char** argv);
}

#define GAME_INTERFACE_MAIN _goliath_main_
#define GAME_INTERFACE_MAIN_SYM XSTR(GAME_INTERFACE_MAIN)
