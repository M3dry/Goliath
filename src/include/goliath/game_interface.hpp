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

    struct State {
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
        class SamplersService {
          private:
            uint32_t (*_add)(const Sampler* new_sampler);
            void (*_remove)(uint32_t ix);
            VkSampler (*_get)(uint32_t ix);

            friend void make_engine_service(EngineService*);

          public:
            void remove(uint32_t ix) {
                _remove(ix);
            }

            VkSampler get(uint32_t ix) {
                return get(ix);
            }

            uint32_t add(const Sampler& new_sampler) {
                return _add(&new_sampler);
            }
        };

        class TransportService {
          private:
            VkSemaphoreSubmitInfo (*_uploads_to_wait_on)(transport2::ticket* tickets, uint32_t count);
            bool (*_is_uploaded)(transport2::ticket ticket);
            transport2::ticket (*_upload_buffer)(bool priority, void* src, bool own, transport2::FreeFn* own_fn,
                                                 uint32_t size, VkBuffer dst, uint32_t dst_offset, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access);
            transport2::ticket (*_upload_image)(bool priority, VkFormat format, VkExtent3D dimension, void* src,
                                                bool own, transport2::FreeFn* own_fn, VkImage dst,
                                                VkImageSubresourceLayers dst_layers, VkOffset3D dst_offset,
                                                VkImageLayout current_layout, VkImageLayout dst_layout, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access);

            friend void make_engine_service(EngineService*);

          public:
            bool is_uploaded(transport2::ticket ticket) {
                return _is_uploaded(ticket);
            }

            VkSemaphoreSubmitInfo uploads_to_wait_on(std::span<transport2::ticket> tickets) {
                return _uploads_to_wait_on(tickets.data(), tickets.size());
            }

            transport2::ticket upload(bool priority, void* src, std::optional<transport2::FreeFn*> own, uint32_t size,
                                      VkBuffer dst, uint32_t dst_offset, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access) {
                return _upload_buffer(priority, src, own.has_value(), own ? *own : nullptr, size, dst, dst_offset, dst_stage, dst_access);
            }

            transport2::ticket upload(bool priority, VkFormat format, VkExtent3D dimension, void* src,
                                      std::optional<transport2::FreeFn*> own, VkImage dst,
                                      VkImageSubresourceLayers dst_layers, VkOffset3D dst_offset,
                                      VkImageLayout current_layout, VkImageLayout dst_layout, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access) {
                return _upload_image(priority, format, dimension, src, own.has_value(), own ? *own : nullptr, dst,
                                     dst_layers, dst_offset, current_layout, dst_layout, dst_stage, dst_access);
            }
        };

        class TexturesService {
          private:
            std::string* (*_name)(textures::gid gid, textures::Err* err, bool* erred);
            GPUImage (*_image)(textures::gid gi, textures::Err* err, bool* erred);
            VkImageView (*_image_view)(textures::gid gi, textures::Err* err, bool* erred);
            uint32_t (*_sampler)(textures::gid gi, textures::Err* err, bool* erred);

            void (*_acquire)(const textures::gid* gids, uint32_t count);
            void (*_release)(const textures::gid* gids, uint32_t count);

            const TexturePool* (*_texture_pool)();

            friend void make_engine_service(EngineService*);

          public:
            const TexturePool& texture_pool() {
                return *_texture_pool();
            }

            void acquire(const std::span<textures::gid>& gids) {
                _acquire(gids.data(), gids.size());
            }

            void release(const std::span<textures::gid>& gids) {
                _release(gids.data(), gids.size());
            }

            std::expected<std::string*, textures::Err> name(textures::gid gid) {
                bool erred = false;
                textures::Err err;
                auto res = _name(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<GPUImage, textures::Err> image(textures::gid gid) {
                bool erred = false;
                textures::Err err;
                auto res = _image(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<VkImageView, textures::Err> image_view(textures::gid gid) {
                bool erred = false;
                textures::Err err;
                auto res = _image_view(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<uint32_t, textures::Err> sampler(textures::gid gid) {
                bool erred = false;
                textures::Err err;
                auto res = _sampler(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }
        };

        class MaterialsService {
          private:
            const Material* (*_schema)(uint32_t mat_id);
            uint8_t* (*_instance_data)(uint32_t mat_id, uint32_t instance_ix, uint32_t* size);
            Buffer (*_buffer)();
            void (*_set_instance_data)(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data);

            friend void make_engine_service(EngineService*);

          public:
            Buffer buffer() {
                return _buffer();
            }

            const Material& schema(uint32_t mat_id) {
                return *_schema(mat_id);
            }

            std::span<uint8_t> instance_data(uint32_t mat_id, uint32_t instance_ix) {
                uint32_t size;
                auto data = _instance_data(mat_id, instance_ix, &size);
                return {data, size};
            }

            void set_instance_data(uint32_t mat_id, uint32_t instance_ix, uint8_t* new_data) {
                _set_instance_data(mat_id, instance_ix, new_data);
            }
        };

        class ModelsService {
          private:
            std::string* (*_name)(models::gid gid, models::Err* err, bool* erred);
            engine::Model* (*_cpu_model)(models::gid gid, models::Err* err, bool* erred);
            transport2::ticket (*_ticket)(models::gid gid, models::Err* err, bool* erred);
            engine::Buffer (*_draw_buffer)(models::gid gid, models::Err* err, bool* erred);
            engine::GPUModel (*_gpu_model)(models::gid gid, models::Err* err, bool* erred);
            engine::GPUGroup (*_gpu_group)(models::gid gid, models::Err* err, bool* erred);

            models::LoadState (*_is_loaded)(models::gid gid, models::Err* err, bool* erred);

            void (*_acquire)(const models::gid* gids, uint32_t count);
            void (*_release)(const models::gid* gids, uint32_t count);

            friend void make_engine_service(EngineService*);

          public:
            void acquire(const std::span<models::gid>& gids) {
                _acquire(gids.data(), gids.size());
            }

            void release(const std::span<models::gid>& gids) {
                _release(gids.data(), gids.size());
            }

            std::expected<std::string*, models::Err> name(models::gid gid) {
                bool erred = false;
                models::Err err;
                auto res = _name(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::Model*, models::Err> cpu_model(models::gid gid) {
                bool erred = false;
                models::Err err;
                auto res = _cpu_model(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<transport2::ticket, models::Err> ticket(models::gid gid) {
                bool erred = false;
                models::Err err;
                auto res = _ticket(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::Buffer, models::Err> draw_buffer(models::gid gid) {
                bool erred = false;
                models::Err err;
                auto res = _draw_buffer(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::GPUModel, models::Err> gpu_model(models::gid gid) {
                bool erred = false;
                models::Err err;
                auto res = _gpu_model(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<engine::GPUGroup, models::Err> gpu_group(models::gid gid) {
                bool erred = false;
                models::Err err;
                auto res = _gpu_group(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }

            std::expected<models::LoadState, models::Err> is_loaded(models::gid gid) {
                bool erred = false;
                models::Err err;
                auto res = _is_loaded(gid, &err, &erred);
                if (erred) return std::unexpected(err);

                return res;
            }
        };

        class ComputeService {
          private:
            ComputePipeline (*_create)(const ComputePipelineBuilder* builder);
            void (*_destroy)(ComputePipeline* pipeline);

            friend void make_engine_service(EngineService*);

          public:
            ComputePipeline create(const ComputePipelineBuilder& builder) {
                return _create(&builder);
            }

            void destroy(ComputePipeline& pipeline) {
                _destroy(&pipeline);
            }
        };

        class GPUGroupService {
          private:
            uint32_t (*_upload)(uint32_t texture_gid_count, uint32_t data_size,
                                void (*upload_ptr)(uint8_t*, uint32_t, uint32_t, textures::gid*, uint32_t, void*),
                                void* ctx);
            void (*_begin)();
            [[nodiscard]] GPUGroup (*_end)(bool priority, VkBufferUsageFlags usage_flags, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access);

            friend void make_engine_service(EngineService*);

          public:
            void begin() {
                _begin();
            }

            [[nodiscard]] GPUGroup end(bool priority, VkBufferUsageFlags usage_flags, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access) {
                return _end(priority, usage_flags, dst_stage, dst_access);
            }

            uint32_t upload(uint32_t texture_gid_count, uint32_t data_size,
                            void (*upload_ptr)(uint8_t*, uint32_t, uint32_t, textures::gid*, uint32_t, void*),
                            void* ctx = nullptr) {
                return _upload(texture_gid_count, data_size, upload_ptr, ctx);
            }
        };

        class ShaderService {
          private:
            VkShaderModule (*_create)(uint8_t* code, uint32_t code_size);
            void (*_destroy)(VkShaderModule shader);

            friend void make_engine_service(EngineService*);

          public:
            void destroy(VkShaderModule shader) {
                _destroy(shader);
            }

            VkShaderModule create(std::span<uint8_t> code) {
                return _create(code.data(), code.size());
            }
        };

        class RenderingService {
          private:
            GraphicsPipeline (*_create_pipeline)(const GraphicsPipelineBuilder* builder);
            void (*_destroy_pipeline)(GraphicsPipeline* pipeline);

            friend void make_engine_service(EngineService*);

          public:
            GraphicsPipeline create_pipeline(const GraphicsPipelineBuilder& builder) {
                return _create_pipeline(&builder);
            }

            void destroy_pipeline(GraphicsPipeline& pipeline) {
                _destroy_pipeline(&pipeline);
            }
        };

        struct VisBufferService {
            void (*resize)(glm::uvec2 new_dims, bool material_count_changed);
        };

        struct CullingService {
            void (*resize)(uint32_t max_draw_count);
        };

        class TextureService {
          private:
            GPUImage (*_upload1)(bool priority, bool own, const char* name, const Image* img, VkImageLayout new_layout, transport2::ticket* ticket, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access);
            GPUImage (*_upload2)(const char* name, const GPUImageInfo* builder, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access);

            VkImageView (*_create_view)(const GPUImageView* gpu_image_view);

            void (*destroy_image)(GPUImage* gpu_image);
            void (*destroy_view)(VkImageView view);

            friend void make_engine_service(EngineService*);

          public:
            void destroy(GPUImage& image) {
                destroy(image);
            }

            void destroy(VkImageView view) {
                destroy_view(view);
            }

            VkImageView create_view(const GPUImageView& gpu_image_view) {
                return _create_view(&gpu_image_view);
            }

            GPUImage upload(bool priority, bool own, const char* name, const Image& img,
                                                              VkImageLayout new_layout, transport2::ticket& ticket, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access) {
                auto gpu_img = _upload1(priority, own, name, &img, new_layout, &ticket, dst_stage, dst_access);
                return gpu_img;
            }

            GPUImage upload(const char* name, const GPUImageInfo& builder, VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access) {
                auto gpu_img = _upload2(name, &builder, dst_stage, dst_access);
                return gpu_img;
            }
        };

        struct BufferService {
          private:
            Buffer (*_create)(const char* name, uint32_t size, VkBufferUsageFlags usage, bool coherent, void** ptr,
                              bool* need_flush, VmaAllocationCreateFlags alloc_flags);
            void (*_destroy)(Buffer* buffer);

          public:
            Buffer create(const char* name, uint32_t size, VkBufferUsageFlags usage,
                          std::optional<std::pair<void**, bool*>> host, VmaAllocationCreateFlags alloc_flags = 0) {
                return _create(name, size, usage, host.has_value(), host.has_value() ? host->first : nullptr,
                               host.has_value() ? host->second : nullptr, alloc_flags);
            }

            void destroy(Buffer& buffer) {
                _destroy(&buffer);
            }
        };

        uint32_t frame_ix;
        uint32_t frames_in_flight;
        VkDescriptorSetLayout empty_set;
        State (*get_engine_state)();

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
    };

    struct TickService {
        bool (*is_held)(uint32_t code);
        bool (*was_released)(uint32_t code);

        glm::vec2 (*get_mouse_delta)();
        glm::vec2 (*get_mouse_absolute)();

        void (*update_tick)();
    };

    struct FrameService {
        class DescriptorService {
          private:
            uint64_t (*_new_set)(VkDescriptorSetLayout layout);
            void (*_bind_set)(uint64_t id, VkPipelineBindPoint bind_point, VkPipelineLayout layout, uint32_t set);
            void (*_update_set)(uint64_t id, VkWriteDescriptorSet* writes, uint32_t write_count);

            void (*_begin_update)(uint64_t id);
            void (*_end_update)();

            void (*_update_sampled_image)(uint32_t binding, VkImageLayout layout, VkImageView view, VkSampler sampler);
            void (*_update_storage_image)(uint32_t binding, VkImageLayout layout, VkImageView view);
            void (*_update_ubo)(uint32_t binding, uint8_t* ubo, uint32_t ubo_size);

            friend void make_frame_service(FrameService*);

          public:
            uint64_t new_set(VkDescriptorSetLayout layout) {
                return _new_set(layout);
            }

            void bind_set(uint64_t id, VkPipelineBindPoint bind_point, VkPipelineLayout layout, uint32_t set) {
                _bind_set(id, bind_point, layout, set);
            }

            void update_set(uint64_t id, VkWriteDescriptorSet* writes, uint32_t write_count) {
                _update_set(id, writes, write_count);
            }

            void begin_update(uint64_t id) {
                _begin_update(id);
            }

            void end_update() {
                _end_update();
            }

            void update_sampled_image(uint32_t binding, VkImageLayout layout, VkImageView view, VkSampler sampler) {
                update_sampled_image(binding, layout, view, sampler);
            }

            void update_storage_image(uint32_t binding, VkImageLayout layout, VkImageView view) {
                _update_storage_image(binding, layout, view);
            }

            void update_ubo(uint32_t binding, std::span<uint8_t> ubo) {
                _update_ubo(binding, ubo.data(), ubo.size());
            }
        };

        class ComputeService {
          private:
            void (*_bind)(const ComputePipeline* pipeline);
            void (*_dispatch)(const ComputePipeline* pipeline, const ComputePipeline::DispatchParams* params);
            void (*_dispatch_indirect)(const ComputePipeline* pipeline,
                                       const ComputePipeline::IndirectDispatchParams* params);

            friend void make_frame_service(FrameService*);

          public:
            void bind(const ComputePipeline& pipeline) {
                _bind(&pipeline);
            }

            void dispatch(const ComputePipeline& pipeline, const ComputePipeline::DispatchParams& params) {
                _dispatch(&pipeline, &params);
            }

            void dispatch_indirect(const ComputePipeline& pipeline,
                                   const ComputePipeline::IndirectDispatchParams& params) {
                _dispatch_indirect(&pipeline, &params);
            }
        };

        class CullingService {
          private:
            void (*_bind_flatten)();

            void (*_flatten)(uint64_t group_addr, uint32_t draw_count, uint64_t draw_buffer_addr,
                             uint64_t transforms_addr, uint32_t default_transform_offset);
            void (*_flatten_models)(models::gid gid, uint64_t transforms_addr, uint32_t default_transform_offset,
                                    models::Err* err, bool* erred);

            void (*_cull)(uint32_t max_draw_count, uint64_t draw_id_addr, uint64_t indirect_draw_addr);

            void (*_sync_for_draw)(Buffer* draw_id_buffer, Buffer* indirect_draw_buffer);

            void (*_clear_buffers)(Buffer* draw_ids, Buffer* indirect_draws, VkAccessFlags2 draw_ids_src_access,
                                   VkPipelineStageFlags2 draw_ids_src_stage, VkAccessFlags2 indirect_draws_access,
                                   VkPipelineStageFlags2 indirect_draws_stage);

            friend void make_frame_service(FrameService*);

          public:
            void bind_flatten() {
                _bind_flatten();
            }

            void cull(uint32_t max_draw_count, uint64_t draw_id_addr, uint64_t indirect_draw_addr) {
                _cull(max_draw_count, draw_id_addr, indirect_draw_addr);
            }

            void sync_for_draw(Buffer& draw_id_buffer, Buffer& indirect_draw_buffer) {
                return _sync_for_draw(&draw_id_buffer, &indirect_draw_buffer);
            }

            void flatten(uint64_t group_addr, uint32_t draw_count, uint64_t draw_buffer_addr, uint64_t transforms_addr,
                         uint32_t default_transform_offset) {
                return _flatten(group_addr, draw_count, draw_buffer_addr, transforms_addr, default_transform_offset);
            }

            std::expected<void, models::Err> flatten(models::gid gid, uint64_t transforms_addr,
                                                     uint32_t default_transform_offset) {
                bool erred = false;
                models::Err err;
                _flatten_models(gid, transforms_addr, default_transform_offset, &err, &erred);
                if (erred) return std::unexpected(err);

                return {};
            }

            void clear_buffers(Buffer& draw_ids, Buffer& indirect_draws,
                               VkAccessFlags2 draw_ids_src_access = VK_ACCESS_2_SHADER_READ_BIT,
                               VkPipelineStageFlags2 draw_ids_src_stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                               VkAccessFlags2 indirect_draws_access = VK_ACCESS_2_SHADER_READ_BIT |
                                                                      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                               VkPipelineStageFlags2 indirect_draws_stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                                            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT) {
                return _clear_buffers(&draw_ids, &indirect_draws, draw_ids_src_access, draw_ids_src_stage,
                                      indirect_draws_access, indirect_draws_stage);
            }
        };

        class RenderingService {
          private:
            void (*_begin)(VkRenderingInfo info);
            void (*_end)();

            void (*_bind)(const GraphicsPipeline* pipeline);
            void (*_draw)(const GraphicsPipeline* pipeline, const GraphicsPipeline::DrawParams* params);
            void (*_draw_indirect)(const GraphicsPipeline* pipeline,
                                   const GraphicsPipeline::DrawIndirectParams* params);
            void (*_draw_indirect_count)(const GraphicsPipeline* pipeline,
                                         const GraphicsPipeline::DrawIndirectCountParams* params);

            friend void make_frame_service(FrameService*);

          public:
            void end() {
                _end();
            }

            void begin(const RenderPass* attachment) {
                _begin(attachment->_info);
            }

            void bind(const GraphicsPipeline& pipeline) {
                _bind(&pipeline);
            }

            void draw(const GraphicsPipeline& pipeline, const GraphicsPipeline::DrawParams& params) {
                _draw(&pipeline, &params);
            }

            void draw_indirect(const GraphicsPipeline& pipeline, const GraphicsPipeline::DrawIndirectParams& params) {
                _draw_indirect(&pipeline, &params);
            }

            void draw_indirect_count(const GraphicsPipeline& pipeline,
                                     const GraphicsPipeline::DrawIndirectCountParams& params) {
                _draw_indirect_count(&pipeline, &params);
            }
        };

        class SynchronizationService {
          private:
            void (*_begin_barriers)();
            void (*_end_barriers)();

            void (*_apply_barrier_img)(VkImageMemoryBarrier2 barrier);
            void (*_apply_barrier_buf)(VkBufferMemoryBarrier2 barrier);
            void (*_apply_barrier)(VkMemoryBarrier2 barrier);

            friend void make_frame_service(FrameService*);

          public:
            void begin_barriers() {
                _begin_barriers();
            }

            void end_barriers() {
                _end_barriers();
            }

            void apply_barrier(VkImageMemoryBarrier2 barrier) {
                _apply_barrier_img(barrier);
            }

            void apply_barrier(VkBufferMemoryBarrier2 barrier) {
                _apply_barrier_buf(barrier);
            }

            void apply_barrier(VkMemoryBarrier2 barrier) {
                _apply_barrier(barrier);
            }
        };

        class VisBufferService {
          private:
            VkImageView (*_view)();
            VkImage (*_image)();

            VkImageMemoryBarrier2 (*_transition_to_attachement)();
            VkImageMemoryBarrier2 (*_transition_to_general)();

            RenderingAttachement (*_attach)();

            void (*_prepare_for_draw)();
            void (*_count_materials)(uint64_t draw_id_addr);
            void (*_get_offsets)();
            void (*_write_fragment_ids)(uint64_t draw_id_addr);
            visbuffer::Shading (*_shade)(VkImageView target);
            void (*_clear_buffers)();
            void (*_push_material)(uint16_t n);
            void (*_pop_material)(uint16_t n);

            friend void make_frame_service(FrameService*);

          public:
            VkImageView view() {
                return _view();
            }

            VkImage image() {
                return _image();
            }

            VkImageMemoryBarrier2 transition_to_attachement() {
                return _transition_to_attachement();
            }

            VkImageMemoryBarrier2 transition_to_general() {
                return transition_to_general();
            }

            RenderingAttachement attach() {
                return _attach();
            }

            void prepare_for_draw() {
                _prepare_for_draw();
            }

            void count_materials(uint64_t draw_id_addr) {
                _count_materials(draw_id_addr);
            }

            void get_offsets() {
                get_offsets();
            }

            void write_fragment_ids(uint64_t draw_id_addr) {
                _write_fragment_ids(draw_id_addr);
            }

            visbuffer::Shading shade(VkImageView target) {
                return _shade(target);
            }

            void clear_buffers() {
                _clear_buffers();
            }

            void push_material(uint16_t n = 1) {
                _push_material(n);
            }

            void pop_material(uint16_t n) {
                _pop_material(n);
            }
        };

        class TexturePoolService {
          private:
            void (*_bind)(const TexturePool* texture_pool, VkPipelineBindPoint bind_point, VkPipelineLayout layout);

            friend void make_frame_service(FrameService*);

          public:
            void bind(const TexturePool& texture_pool, VkPipelineBindPoint bind_point, VkPipelineLayout layout) {
                _bind(&texture_pool, bind_point, layout);
            }
        };

        VkImage target;
        VkImageView target_view;

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

    using InitFn = void*(const AssetPaths*, const EngineService*);
    using DestroyFn = void(void*, const EngineService*);
    using ResizeFn = void(void*, EngineService*);
    using TickFn = void(void*, const TickService*, const EngineService*);
    using DrawImGuiFn = void(void*, const EngineService*);
    using RenderFn =
        VkSemaphoreSubmitInfo*(void*, VkCommandBuffer, const FrameService*, const EngineService*, uint32_t*);

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
        VkFormat target_format;
        VkImageLayout target_start_layout;
        VkImageLayout target_end_layout;
        VkPipelineStageFlags2 target_create_stage;
        VkAccessFlags2 target_create_access;
        VkPipelineStageFlags2 target_finish_stage;
        VkAccessFlags2 target_finish_access;
        // if == {0, 0}, then it's the same as the window/viewport size, and ResizeFn has to be defined
        glm::uvec2 target_dimensions{0};
        BlitStrategy target_blit_strategy;
        glm::vec4 letterbox_clear_color;

        GameFunctions funcs;
    };

    void make_engine_service(EngineService*);
    void make_frame_service(FrameService*);
    void make_tick_service(TickService*);

    void start(const GameConfig& config, const AssetPaths& asset_paths);
}

#define GAME_INTERFACE_MAIN GameConfig _goliath_main_()
