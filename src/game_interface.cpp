#include "goliath/game_interface.hpp"
#include "GLFW/glfw3.h"
#include "engine_.hpp"
#include "goliath/compute.hpp"
#include "goliath/culling.hpp"
#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/event.hpp"
#include "goliath/gpu_group.hpp"
#include "goliath/imgui.hpp"
#include "goliath/materials.hpp"
#include "goliath/models.hpp"
#include "goliath/rendering.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/textures.hpp"
#include "goliath/transport2.hpp"
#include "goliath/visbuffer.hpp"
#include <unistd.h>
#include <vulkan/vulkan_core.h>

void engine::game_interface::make_engine_service(EngineService* serv) {
    EngineService::SamplersService samplers_service{};
    samplers_service._add = [](const Sampler* new_sampler) { return samplers::add(*new_sampler); };
    samplers_service._remove = samplers::remove;
    samplers_service._get = samplers::get;

    EngineService::TransportService transport_service{};
    transport_service._uploads_to_wait_on = [](transport2::ticket* tickets, uint32_t count) {
        return transport2::wait_on({tickets, count});
    };
    transport_service._is_uploaded = transport2::is_ready;
    transport_service._upload_buffer = [](bool priority, void* src, bool own, transport2::FreeFn* own_fn, uint32_t size,
                                          VkBuffer dst, uint32_t dst_offset, VkPipelineStageFlagBits2 dst_stage,
                                          VkAccessFlagBits2 dst_access) {
        return transport2::upload(priority, src, own ? std::make_optional(own_fn) : std::nullopt, size, dst, dst_offset,
                                  dst_stage, dst_access);
    };
    transport_service._upload_image = [](bool priority, VkFormat format, VkExtent3D dimension, void* src, bool own,
                                         transport2::FreeFn* own_fn, VkImage dst, VkImageSubresourceLayers dst_layers,
                                         VkOffset3D dst_offset, VkImageLayout current_layout, VkImageLayout dst_layout,
                                         VkPipelineStageFlagBits2 dst_stage, VkAccessFlagBits2 dst_access) {
        return transport2::upload(priority, format, dimension, src, own ? std::make_optional(own_fn) : std::nullopt,
                                  dst, dst_layers, dst_offset, current_layout, dst_layout, dst_stage, dst_access);
    };

    EngineService::TexturesService textures_service{};
    textures_service._name = [](textures::gid gid, textures::Err* err, bool* erred) {
        auto res = textures::get_name(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    textures_service._image = [](textures::gid gid, textures::Err* err, bool* erred) {
        auto res = textures::get_image(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    textures_service._image_view = [](textures::gid gid, textures::Err* err, bool* erred) {
        auto res = textures::get_image_view(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    textures_service._sampler = [](textures::gid gid, textures::Err* err, bool* erred) {
        auto res = textures::get_sampler(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    textures_service._acquire = textures::acquire;
    textures_service._release = textures::release;
    textures_service._texture_pool = []() { return &textures::get_texture_pool(); };

    EngineService::MaterialsService materials_service{};
    materials_service._buffer = materials::get_buffer,
    materials_service._schema = [](uint32_t mat_id) { return &materials::get_schema(mat_id); };
    materials_service._instance_data = [](uint32_t mat_id, uint32_t instance_ix, uint32_t* size) {
        auto data = materials::get_instance_data(mat_id, instance_ix);
        *size = data.size();
        return data.data();
    };
    materials_service._set_instance_data = materials::update_instance_data;

    EngineService::ModelsService models_service{};
    models_service._name = [](models::gid gid, models::Err* err, bool* erred) {
        auto res = models::get_name(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    models_service._cpu_model = [](models::gid gid, models::Err* err, bool* erred) {
        auto res = models::get_cpu_model(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    models_service._ticket = [](models::gid gid, models::Err* err, bool* erred) {
        auto res = models::get_ticket(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    models_service._draw_buffer = [](models::gid gid, models::Err* err, bool* erred) {
        auto res = models::get_draw_buffer(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    models_service._gpu_model = [](models::gid gid, models::Err* err, bool* erred) {
        auto res = models::get_gpu_model(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    models_service._gpu_group = [](models::gid gid, models::Err* err, bool* erred) {
        auto res = models::get_gpu_group(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    models_service._is_loaded = [](models::gid gid, models::Err* err, bool* erred) {
        auto res = models::is_loaded(gid);

        *erred = !res.has_value();
        *err = res.error();

        return res.value();
    };
    models_service._acquire = models::acquire;
    models_service._release = models::release;

    EngineService::ComputeService engine_compute_service{};
    engine_compute_service._create = [](const ComputePipelineBuilder* builder) { return compute::create(*builder); };
    engine_compute_service._destroy = [](ComputePipeline* pipeline) { return compute::destroy(*pipeline); };

    EngineService::GPUGroupService gpu_group_service{};
    gpu_group_service._upload = gpu_group::upload;
    gpu_group_service._begin = gpu_group::begin;
    gpu_group_service._end = gpu_group::end;

    EngineService::ShaderService shader_service{};
    shader_service._create = [](uint8_t* code, uint32_t code_size) { return shader::create({code, code_size}); };
    shader_service._destroy = shader::destroy;

    EngineService::RenderingService rendering_service{};
    rendering_service._create_pipeline = [](const GraphicsPipelineBuilder* builder) {
        return rendering::create_pipeline(*builder);
    };
    rendering_service._destroy_pipeline = [](GraphicsPipeline* pipeline) {
        return rendering::destroy_pipeline(*pipeline);
    };

    EngineService::VisBufferService visbuffer_service{};
    visbuffer_service.resize = visbuffer::resize;

    EngineService::CullingService culling_service{};
    culling_service.resize = culling::resize;

    EngineService::TextureService texture_service{};
    texture_service._upload1 = [](bool priority, bool own, const char* name, const Image* img, VkImageLayout new_layout,
                                  transport2::ticket* ticket, VkPipelineStageFlagBits2 dst_stage,
                                  VkAccessFlagBits2 dst_access) {
        return gpu_image::upload(priority, own, name, *img, new_layout, *ticket, dst_stage, dst_access);
    };
    texture_service._upload2 = [](const char* name, const GPUImageInfo* builder, VkPipelineStageFlagBits2 dst_stage,
                                  VkAccessFlagBits2 dst_access) {
        return gpu_image::upload(name, *builder, dst_stage, dst_access);
    };
    texture_service._create_view = [](const GPUImageView* gpu_image_view) {
        return gpu_image_view::create(*gpu_image_view);
    };
    texture_service.destroy_image = [](GPUImage* gpu_image) { gpu_image::destroy(*gpu_image); };
    texture_service.destroy_view = gpu_image_view::destroy;

    serv->frame_ix = 0;
    serv->frames_in_flight = frames_in_flight;
    serv->empty_set = descriptor::empty_set;
    serv->get_engine_state = []() {
        return State{
            .window = &window,
            .instance = &instance,
            .physical_device = &physical_device,
            .physical_device_properties = &physical_device_properties,
            .device = &device,
            .allocator = &allocator,
            .surface = &surface,
            .swapchain_format = &swapchain_format,
            .swapchain_extent = &swapchain_extent,
            .swapchain_images =
                [](uint32_t* size) {
                    *size = swapchain_images.size();
                    return swapchain_images.data();
                },
            .swapchain_image_views =
                [](uint32_t* size) {
                    *size = swapchain_image_views.size();
                    return swapchain_image_views.data();
                },

            .graphics_queue = &graphics_queue,
            .graphics_queue_family = &graphics_queue_family,
            .transport_queue = &transport_queue,
            .transport_queue_family = &transport_queue_family,
        };
    };
    serv->samplers = samplers_service;
    serv->transport = transport_service;
    serv->textures = textures_service;
    serv->materials = materials_service;
    serv->models = models_service;
    serv->compute = engine_compute_service;
    serv->gpu_group = gpu_group_service;
    serv->shader = shader_service;
    serv->rendering = rendering_service;
    serv->visbuffer = visbuffer_service;
    serv->culling = culling_service;
    serv->texture = texture_service;
}

void engine::game_interface::make_frame_service(FrameService* serv) {
    FrameService::DescriptorService descriptor_service{};
    descriptor_service._update_ubo = [](uint32_t binding, uint8_t* ubo, uint32_t ubo_size) {
        get_frame_descriptor_pool().update_ubo(binding, {ubo, ubo_size});
    };
    descriptor_service._new_set = [](VkDescriptorSetLayout layout) {
        return get_frame_descriptor_pool().new_set(layout);
    };
    descriptor_service._bind_set = [](uint64_t id, VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                                      uint32_t set) {
        get_frame_descriptor_pool().bind_set(id, get_cmd_buf(), bind_point, layout, set);
    };
    descriptor_service._update_set = [](uint64_t id, VkWriteDescriptorSet* writes, uint32_t write_count) {
        get_frame_descriptor_pool().update_set(id, {writes, write_count});
    };
    descriptor_service._begin_update = [](uint64_t id) { get_frame_descriptor_pool().begin_update(id); };
    descriptor_service._end_update = []() { get_frame_descriptor_pool().end_update(); };
    descriptor_service._update_sampled_image = [](uint32_t binding, VkImageLayout layout, VkImageView view,
                                                  VkSampler sampler) {
        get_frame_descriptor_pool().update_sampled_image(binding, layout, view, sampler);
    };
    descriptor_service._update_storage_image = [](uint32_t binding, VkImageLayout layout, VkImageView view) {
        get_frame_descriptor_pool().update_storage_image(binding, layout, view);
    };

    FrameService::ComputeService compute_service{};
    compute_service._bind = [](const ComputePipeline* pipeline) { pipeline->bind(); };
    compute_service._dispatch = [](const ComputePipeline* pipeline, const ComputePipeline::DispatchParams* params) {
        pipeline->dispatch(*params);
    };
    compute_service._dispatch_indirect = [](const ComputePipeline* pipeline,
                                            const ComputePipeline::IndirectDispatchParams* params) {
        pipeline->dispatch_indirect(*params);
    };

    FrameService::CullingService culling_service{};
    culling_service._flatten = culling::flatten;
    culling_service._flatten_models = [](models::gid gid, uint64_t transforms_addr, uint32_t default_transform_offset,
                                         models::Err* err, bool* erred) {
        auto res = culling::flatten(gid, transforms_addr, default_transform_offset);
        *erred = !res.has_value();
        *err = res.error();
        return res.value();
    };
    culling_service._sync_for_draw = [](Buffer* draw_id_buffer, Buffer* indirect_draw_buffer) {
        culling::sync_for_draw(*draw_id_buffer, *indirect_draw_buffer);
    };
    culling_service._clear_buffers = [](Buffer* draw_ids, Buffer* indirect_draws, VkAccessFlags2 draw_ids_src_access,
                                        VkPipelineStageFlags2 draw_ids_src_stage, VkAccessFlags2 indirect_draws_access,
                                        VkPipelineStageFlags2 indirect_draws_stage) {
        culling::clear_buffers(*draw_ids, *indirect_draws, draw_ids_src_access, draw_ids_src_stage,
                               indirect_draws_access, indirect_draws_stage);
    };
    culling_service._bind_flatten = culling::bind_flatten;
    culling_service._cull = culling::cull;

    FrameService::RenderingService rendering_service;
    rendering_service._begin = [](VkRenderingInfo info) {
        RenderPass renderpass{};
        renderpass._info = info;
        rendering::begin(renderpass);
    };
    rendering_service._bind = [](const GraphicsPipeline* pipeline) { pipeline->bind(); };
    rendering_service._draw = [](const GraphicsPipeline* pipeline, const GraphicsPipeline::DrawParams* params) {
        pipeline->draw(*params);
    };
    rendering_service._draw_indirect = [](const GraphicsPipeline* pipeline,
                                          const GraphicsPipeline::DrawIndirectParams* params) {
        pipeline->draw_indirect(*params);
    };
    rendering_service._draw_indirect_count = [](const GraphicsPipeline* pipeline,
                                                const GraphicsPipeline::DrawIndirectCountParams* params) {
        pipeline->draw_indirect_count(*params);
    };
    rendering_service._end = rendering::end;

    FrameService::SynchronizationService synchronization_service{};
    synchronization_service._apply_barrier_img = [](VkImageMemoryBarrier2 barrier) {
        synchronization::apply_barrier(barrier);
    };
    synchronization_service._apply_barrier_buf = [](VkBufferMemoryBarrier2 barrier) {
        synchronization::apply_barrier(barrier);
    };
    synchronization_service._apply_barrier = [](VkMemoryBarrier2 barrier) { synchronization::apply_barrier(barrier); };
    synchronization_service._begin_barriers = synchronization::begin_barriers;
    synchronization_service._end_barriers = synchronization::end_barriers;

    FrameService::VisBufferService visbuffer_service{};
    visbuffer_service._push_material = visbuffer::push_material;
    visbuffer_service._pop_material = visbuffer::pop_material;
    visbuffer_service._view = []() { return visbuffer::get_view(); };
    visbuffer_service._image = []() { return visbuffer::get_image(); };
    visbuffer_service._transition_to_attachement = visbuffer::transition_to_attachement;
    visbuffer_service._transition_to_general = visbuffer::transition_to_general;
    visbuffer_service._attach = visbuffer::attach;
    visbuffer_service._prepare_for_draw = visbuffer::prepare_for_draw;
    visbuffer_service._count_materials = visbuffer::count_materials;
    visbuffer_service._get_offsets = visbuffer::get_offsets;
    visbuffer_service._write_fragment_ids = visbuffer::write_fragment_ids;
    visbuffer_service._shade = visbuffer::shade;
    visbuffer_service._clear_buffers = visbuffer::clear_buffers;

    FrameService::TexturePoolService texture_pool_service{};
    texture_pool_service._bind = [](const TexturePool* texture_pool, VkPipelineBindPoint bind_point,
                                    VkPipelineLayout layout) { texture_pool->bind(bind_point, layout); };

    serv->target = nullptr;
    serv->target_view = nullptr;

    serv->descriptor = descriptor_service;
    serv->compute = compute_service;
    serv->culling = culling_service;
    serv->rendering = rendering_service;
    serv->synchronization = synchronization_service;
    serv->visbuffer = visbuffer_service;
    serv->texture_pool = texture_pool_service;
}

void engine::game_interface::make_tick_service(TickService* serv) {
    serv->is_held = event::is_held;
    serv->was_released = event::was_released;
    serv->get_mouse_delta = event::get_mouse_delta;
    serv->get_mouse_absolute = event::get_mouse_absolute;
    serv->update_tick = event::update_tick;
}

void engine::game_interface::start(const GameConfig& config, const AssetPaths& asset_paths) {
    init(Init{
        .window_name = config.name,
        .fullscreen = config.fullscreen,
        .textures_directory = asset_paths.textures_dir,
        .models_directory = asset_paths.models_dir,
    });

    EngineService engine_service;
    make_engine_service(&engine_service);

    FrameService frame_service;
    make_frame_service(&frame_service);

    TickService tick_service;
    make_tick_service(&tick_service);

    glm::ivec2 target_dimension{0};
    GPUImage* targets = (GPUImage*)malloc(sizeof(GPUImage) * frames_in_flight);
    VkImageView* target_views = (VkImageView*)malloc(sizeof(VkImageView) * frames_in_flight);
    auto update_targets = [&config](GPUImage* targets, VkImageView* target_views, glm::ivec2& target_dimensions) {
        target_dimensions.x =
            config.target_dimensions.x == 0 ? engine::swapchain_extent.width : config.target_dimensions.x;
        target_dimensions.y =
            config.target_dimensions.y == 0 ? engine::swapchain_extent.height : config.target_dimensions.y;
        for (size_t i = 0; i < frames_in_flight; i++) {
            targets[i] =
                gpu_image::upload(std::format("Target image #{}", i).c_str(),
                                  GPUImageInfo{}

                                      .format(config.target_format)
                                      .width(target_dimensions.x)
                                      .height(target_dimensions.y)
                                      .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                      .new_layout(config.target_start_layout)
                                      .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
                                  config.target_create_stage, config.target_create_access);

            target_views[i] =
                engine::gpu_image_view::create(engine::GPUImageView{targets[i]}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
        }
    };

    update_targets(targets, target_views, target_dimension);

    auto user_data = config.funcs.init(&asset_paths, &engine_service);

    double accum = 0;
    double last_time = glfwGetTime();
    double dt = (1000.0 / config.tps) / 1000.0;

    bool done = false;
    while (!glfwWindowShouldClose(window) && !done) {
        double time = glfwGetTime();
        double frame_time = time - last_time;
        last_time = time;
        accum += frame_time;

        auto state = engine::event::poll();
        if (state == engine::event::Minimized) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }

        engine_service.frame_ix = get_current_frame();

        while (accum >= dt) {
            accum -= dt;

            config.funcs.tick(user_data, &tick_service, &engine_service);

            engine::event::update_tick();
        }

        if (engine::prepare_frame()) {
            if (config.target_dimensions == glm::uvec2{0, 0}) update_targets(targets, target_views, target_dimension);
            if (config.funcs.resize != nullptr) config.funcs.resize(user_data, &engine_service);
        }

        imgui::begin();
        config.funcs.draw_imgui(user_data, &engine_service);
        imgui::end();

        prepare_draw();

        uint32_t wait_count;
        frame_service.target = targets[engine_service.frame_ix].image;
        frame_service.target_view = target_views[engine_service.frame_ix];
        auto wait_infos = config.funcs.render(user_data, get_cmd_buf(), &frame_service, &engine_service, &wait_count);

        VkImageMemoryBarrier2 swapchain_barrier{};
        swapchain_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        swapchain_barrier.pNext = nullptr;
        swapchain_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        swapchain_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        swapchain_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        swapchain_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapchain_barrier.image = get_swapchain();
        swapchain_barrier.subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkImageMemoryBarrier2 target_barrier{};
        target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        target_barrier.pNext = nullptr;
        target_barrier.srcStageMask = config.target_finish_stage;
        target_barrier.srcAccessMask = config.target_finish_access;
        target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        target_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        target_barrier.oldLayout = config.target_start_layout;
        target_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        target_barrier.image = frame_service.target;
        target_barrier.subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        synchronization::begin_barriers();
        synchronization::apply_barrier(swapchain_barrier);
        synchronization::apply_barrier(target_barrier);
        synchronization::end_barriers();

        if (config.target_blit_strategy == GameConfig::LetterBox) {
            VkClearColorValue clear{};
            clear.float32[0] = config.letterbox_clear_color.x / 255.0f;
            clear.float32[1] = config.letterbox_clear_color.y / 255.0f;
            clear.float32[2] = config.letterbox_clear_color.z / 255.0f;
            clear.float32[3] = config.letterbox_clear_color.w / 255.0f;

            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseArrayLayer = 0;
            range.layerCount = 1;
            range.levelCount = 1;
            range.baseMipLevel = 0;

            vkCmdClearColorImage(get_cmd_buf(), get_swapchain(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                                 &range);

            swapchain_barrier.srcStageMask = swapchain_barrier.dstStageMask;
            swapchain_barrier.srcAccessMask = swapchain_barrier.dstAccessMask;
            swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            swapchain_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

            synchronization::begin_barriers();
            synchronization::apply_barrier(swapchain_barrier);
            synchronization::end_barriers();

            glm::vec2 dims = target_dimension;
            glm::vec2 dims2 = {swapchain_extent.width, swapchain_extent.height};
            float scale = std::min(dims2.x / dims.x, dims2.y / dims.y);
            if (scale > 1.0f) {
                scale = floor(scale);
            }

            dims *= scale;
            glm::vec2 offset = (dims2 - dims) * 0.5f;

            VkImageBlit2 blit_region{
                .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                .pNext = nullptr,
                .srcSubresource =
                    VkImageSubresourceLayers{
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .srcOffsets =
                    {
                        VkOffset3D{
                            .x = 0,
                            .y = 0,
                            .z = 0,
                        },
                        VkOffset3D{
                            .x = target_dimension.x,
                            .y = target_dimension.y,
                            .z = 1,
                        },
                    },
                .dstOffsets =
                    {
                        VkOffset3D{
                            .x = (int32_t)offset.x,
                            .y = (int32_t)offset.y,
                            .z = 0,
                        },
                        VkOffset3D{
                            .x = (int32_t)(offset.x + dims.x),
                            .y = (int32_t)(offset.y + dims.y),
                            .z = 1,
                        },
                    },
            };

            VkBlitImageInfo2 blit_info{
                .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                .pNext = nullptr,
                .srcImage = frame_service.target,
                .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .dstImage = swapchain_barrier.image,
                .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .regionCount = 1,
                .pRegions = &blit_region,
                .filter = VK_FILTER_NEAREST,
            };
            vkCmdBlitImage2(get_cmd_buf(), &blit_info);

            swapchain_barrier.srcStageMask = swapchain_barrier.dstStageMask;
            swapchain_barrier.srcAccessMask = swapchain_barrier.dstAccessMask;
            swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            swapchain_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            swapchain_barrier.oldLayout = swapchain_barrier.newLayout;
            swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            target_barrier.srcStageMask = target_barrier.dstStageMask;
            target_barrier.srcAccessMask = target_barrier.dstAccessMask;
            target_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            target_barrier.dstAccessMask = VK_ACCESS_2_NONE;
            target_barrier.oldLayout = target_barrier.newLayout;
            target_barrier.newLayout = config.target_end_layout;

            synchronization::begin_barriers();
            synchronization::apply_barrier(swapchain_barrier);
            synchronization::apply_barrier(target_barrier);
            synchronization::end_barriers();
        }

        if (engine::next_frame({wait_infos, wait_count})) {
            if (config.target_dimensions == glm::uvec2{0, 0}) update_targets(targets, target_views, target_dimension);
            if (config.funcs.resize != nullptr) config.funcs.resize(user_data, &engine_service);
            increment_frame();
        }
    }

    vkDeviceWaitIdle(device);

    config.funcs.destroy(user_data, &engine_service);

    engine::destroy();
}
