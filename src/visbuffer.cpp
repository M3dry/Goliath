#include "goliath/visbuffer.hpp"
#include "goliath/buffer.hpp"
#include "goliath/compute.hpp"
#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/util.hpp"

#include <cstdlib>
#include <volk.h>
#include <vulkan/vulkan_core.h>

namespace engine::visbuffer {
    GPUImage* vis_buffers;
    VkImageView* vis_buffer_views;

    Buffer stages;
    uint32_t material_count_buffer_size;
    std::array<uint32_t, frames_in_flight> material_count_buffer_offsets;
    uint32_t offsets_buffer_size;
    std::array<uint32_t, frames_in_flight> offsets_buffer_offsets;
    uint32_t shading_dispatch_buffer_size;
    std::array<uint32_t, frames_in_flight> shading_dispatch_buffer_offsets;
    uint32_t fragment_id_buffer_size;
    std::array<uint32_t, frames_in_flight> fragment_id_buffer_offsets;

    VkDescriptorSetLayout storage_image_set_layout;

    using MaterialCount = engine::PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t>;
    ComputePipeline material_count_pipeline;

    using Offsets = engine::PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t, uint64_t, uint32_t, uint32_t>;
    ComputePipeline offsets_pipeline;

    using FragmentID = engine::PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t, uint64_t, uint32_t>;
    ComputePipeline fragment_id_pipeline;

    VkDescriptorSetLayout shading_set_layout;

    uint32_t max_material_id = 0;

    void _resize(VkImageMemoryBarrier2* img_barriers, bool swapchain_changed, bool material_count_changed) {
        if (swapchain_changed) {
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                auto [img1, barrier1] =
                    GPUImage::upload(std::format("Visbuffer #{}", i).c_str(),
                                     GPUImageInfo{}
                                         .new_layout(VK_IMAGE_LAYOUT_GENERAL)
                                         .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                         .width(swapchain_extent.width)
                                         .height(swapchain_extent.height)
                                         .format(format)
                                         .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

                vis_buffers[i] = img1;
                vis_buffer_views[i] = GPUImageView{img1}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).create();
                img_barriers[i] = barrier1;
            }
        }

        if (material_count_changed || swapchain_changed) {
            auto alignment = physical_device_properties.limits.minStorageBufferOffsetAlignment;
            material_count_buffer_size = util::align_up(alignment, sizeof(uint32_t) * (max_material_id + 1));
            offsets_buffer_size = util::align_up(alignment, sizeof(uint32_t) * (max_material_id + 1));
            shading_dispatch_buffer_size = util::align_up(
                alignment, (sizeof(VkDispatchIndirectCommand) + 2 * sizeof(uint32_t)) * (max_material_id + 1));
            fragment_id_buffer_size =
                util::align_up(alignment, swapchain_extent.width * swapchain_extent.height * sizeof(uint32_t));

            stages = Buffer::create("visbuffer stages buffer",
                                    frames_in_flight * (material_count_buffer_size + offsets_buffer_size +
                                                        shading_dispatch_buffer_size + fragment_id_buffer_size),
                                    VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
                                        VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT,
                                    std::nullopt);

            uint32_t offset = 0;
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                material_count_buffer_offsets[i] = offset;
                offset += material_count_buffer_size;
            }
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                offsets_buffer_offsets[i] = offset;
                offset += offsets_buffer_size;
            }
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                shading_dispatch_buffer_offsets[i] = offset;
                offset += shading_dispatch_buffer_size;
            }
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                fragment_id_buffer_offsets[i] = offset;
                offset += fragment_id_buffer_size;
            }
        }
    }

    void init(VkImageMemoryBarrier2* img_barriers) {
        vis_buffers = (GPUImage*)malloc(sizeof(GPUImage) * frames_in_flight);
        vis_buffer_views = (VkImageView*)malloc(sizeof(VkImageView) * frames_in_flight);

        _resize(img_barriers, true, true);

        storage_image_set_layout = engine::DescriptorSet<engine::descriptor::Binding{
            .count = 1,
            .type = engine::descriptor::Binding::StorageImage,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
        }>::create();

        uint32_t material_count_size;
        auto* material_count_spv = util::read_file("./material_count.spv", &material_count_size);
        auto material_count_module = engine::create_shader({material_count_spv, material_count_size});
        material_count_pipeline = ComputePipeline{ComputePipelineBuilder{}
                                                      .shader(material_count_module)
                                                      .descriptor_layout(0, storage_image_set_layout)
                                                      .push_constant(MaterialCount::size)};
        engine::destroy_shader(material_count_module);
        free(material_count_spv);

        uint32_t offsets_size;
        auto* offsets_spv = util::read_file("./offsets.spv", &offsets_size);
        auto offsets_module = engine::create_shader({offsets_spv, offsets_size});
        offsets_pipeline = engine::ComputePipeline(
            engine::ComputePipelineBuilder{}.shader(offsets_module).push_constant(Offsets::size));
        engine::destroy_shader(offsets_module);
        free(offsets_spv);

        uint32_t fragment_id_size;
        auto* fragment_id_spv = util::read_file("./fragment_id.spv", &fragment_id_size);
        auto fragment_id_module = engine::create_shader({fragment_id_spv, fragment_id_size});
        fragment_id_pipeline = engine::ComputePipeline(engine::ComputePipelineBuilder{}
                                                           .shader(fragment_id_module)
                                                           .descriptor_layout(0, storage_image_set_layout)
                                                           .push_constant(FragmentID::size));
        engine::destroy_shader(fragment_id_module);
        free(fragment_id_spv);

        shading_set_layout = engine::DescriptorSet<engine::descriptor::Binding{
                                                       .count = 1,
                                                       .type = engine::descriptor::Binding::StorageImage,
                                                       .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                   },
                                                   engine::descriptor::Binding{
                                                       .count = 1,
                                                       .type = engine::descriptor::Binding::StorageImage,
                                                       .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                   }>::create();
    }

    void resize(VkImageMemoryBarrier2* img_barriers, bool swapchain_changed, bool material_count_changed) {
        if (swapchain_changed) {
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                vis_buffers[i].destroy();
                GPUImageView::destroy(vis_buffer_views[i]);
            }
        }

        if (swapchain_changed || material_count_changed) stages.destroy();

        _resize(img_barriers, swapchain_changed, material_count_changed);
    }

    void destroy() {
        for (std::size_t i = 0; i < frames_in_flight; i++) {
            vis_buffers[i].destroy();
            GPUImageView::destroy(vis_buffer_views[i]);
        }
        stages.destroy();

        engine::destroy_descriptor_set_layout(storage_image_set_layout);

        material_count_pipeline.destroy();
        offsets_pipeline.destroy();
        fragment_id_pipeline.destroy();

        engine::destroy_descriptor_set_layout(shading_set_layout);

        free(vis_buffers);
        free(vis_buffer_views);
    }

    void push_material(uint16_t n) {
        max_material_id += n;
    }

    void pop_material(uint16_t n) {
        max_material_id -= n;
    }

    VkImageView get_view() {
        return vis_buffer_views[engine::get_current_frame()];
    }

    VkImage get_image() {
        return vis_buffers[engine::get_current_frame()].image;
    }

    VkImageMemoryBarrier2 transition_to_attachement() {
        return VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = get_image(),
            .subresourceRange =
                VkImageSubresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
    }

    VkImageMemoryBarrier2 transition_to_general() {
        return VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = get_image(),
            .subresourceRange =
                VkImageSubresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
    }

    RenderingAttachement attach() {
        return RenderingAttachement{}
            .set_image(get_view(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL)
            .set_clear_color(glm::vec4{0.0f})
            .set_load_op(LoadOp::Clear)
            .set_store_op(StoreOp::Store);
    }

    void prepare_for_draw() {
        auto visbuffer_barrier = transition_to_attachement();
        visbuffer_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        visbuffer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        visbuffer_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        visbuffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        synchronization::begin_barriers();
        synchronization::apply_barrier(visbuffer_barrier);
        synchronization::end_barriers();
    }

    void count_materials(uint64_t draw_id_addr) {
        auto current_material_count_offset = material_count_buffer_offsets[get_current_frame()];

        VkImageMemoryBarrier2 visbuffer_barrier = transition_to_general();
        visbuffer_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        visbuffer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        visbuffer_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        visbuffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        VkBufferMemoryBarrier2 material_count_barrier{};
        material_count_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        material_count_barrier.buffer = stages.data();
        material_count_barrier.offset = current_material_count_offset;
        material_count_barrier.size = material_count_buffer_size;
        material_count_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        material_count_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        material_count_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        material_count_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        material_count_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        material_count_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        engine::synchronization::begin_barriers();
        engine::synchronization::apply_barrier(material_count_barrier);
        engine::synchronization::apply_barrier(visbuffer_barrier);
        engine::synchronization::end_barriers();

        auto material_count_set = engine::descriptor::new_set(storage_image_set_layout);
        engine::descriptor::begin_update(material_count_set);
        engine::descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL, engine::visbuffer::get_view());
        engine::descriptor::end_update();

        uint8_t mat_count_pc[MaterialCount::size]{};
        MaterialCount::write(mat_count_pc,
                             glm::vec<2, uint32_t>{engine::swapchain_extent.width, engine::swapchain_extent.height},
                             stages.address() + current_material_count_offset, draw_id_addr);

        material_count_pipeline.bind();
        material_count_pipeline.dispatch(ComputePipeline::DispatchParams{
            .push_constant = mat_count_pc,
            .descriptor_indexes =
                {
                    material_count_set,
                    engine::descriptor::null_set,
                    engine::descriptor::null_set,
                    engine::descriptor::null_set,
                },
            .group_count_x = (uint32_t)std::ceil(swapchain_extent.width / 16.0f),
            .group_count_y = (uint32_t)std::ceil(swapchain_extent.width / 16.0f),
            .group_count_z = 1,
        });
    }

    void get_offsets() {
        auto current_material_count_offset = material_count_buffer_offsets[get_current_frame()];
        VkBufferMemoryBarrier2 material_count_barrier{};
        material_count_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        material_count_barrier.buffer = stages.data();
        material_count_barrier.offset = current_material_count_offset;
        material_count_barrier.size = material_count_buffer_size;
        material_count_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        material_count_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        material_count_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        material_count_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        material_count_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        material_count_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        auto current_offsets_offset = offsets_buffer_offsets[get_current_frame()];
        VkBufferMemoryBarrier2 offsets_barrier{};
        offsets_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        offsets_barrier.buffer = stages.data();
        offsets_barrier.offset = current_offsets_offset;
        offsets_barrier.size = offsets_buffer_size;
        offsets_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        offsets_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        offsets_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        offsets_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        offsets_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        offsets_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        auto current_shading_dispatch_offset = shading_dispatch_buffer_offsets[get_current_frame()];
        VkBufferMemoryBarrier2 shading_dispatch_barrier{};
        shading_dispatch_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        shading_dispatch_barrier.buffer = stages.data();
        shading_dispatch_barrier.offset = current_shading_dispatch_offset;
        shading_dispatch_barrier.size = shading_dispatch_buffer_size;
        shading_dispatch_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shading_dispatch_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shading_dispatch_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        shading_dispatch_barrier.srcStageMask =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR;
        shading_dispatch_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        shading_dispatch_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        engine::synchronization::begin_barriers();
        engine::synchronization::apply_barrier(material_count_barrier);
        engine::synchronization::apply_barrier(offsets_barrier);
        engine::synchronization::apply_barrier(shading_dispatch_barrier);
        engine::synchronization::end_barriers();

        uint8_t offsets_pc[Offsets::size]{};
        auto stages_addr = stages.address();
        Offsets::write(offsets_pc,
                       glm::vec<2, uint32_t>{engine::swapchain_extent.width, engine::swapchain_extent.height},
                       stages_addr + current_material_count_offset, stages_addr + current_offsets_offset,
                       stages_addr + current_shading_dispatch_offset, max_material_id + 1, 1 + max_material_id / 256);

        offsets_pipeline.bind();
        offsets_pipeline.dispatch(engine::ComputePipeline::DispatchParams{
            .push_constant = offsets_pc,
            .group_count_x = (uint32_t)std::ceil((max_material_id + 1) / 256.0f),
            .group_count_y = 1,
            .group_count_z = 1,
        });
    }

    void write_fragment_ids(uint64_t draw_id_addr) {
        auto current_offsets_offset = offsets_buffer_offsets[get_current_frame()];
        VkBufferMemoryBarrier2 offsets_barrier{};
        offsets_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        offsets_barrier.buffer = stages.data();
        offsets_barrier.offset = current_offsets_offset;
        offsets_barrier.size = offsets_buffer_size;
        offsets_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        offsets_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        offsets_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        offsets_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        offsets_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        offsets_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        auto current_fragment_id_offset = fragment_id_buffer_offsets[get_current_frame()];
        VkBufferMemoryBarrier2 fragment_id_barrier{};
        fragment_id_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        fragment_id_barrier.buffer = stages.data();
        fragment_id_barrier.offset = current_fragment_id_offset;
        fragment_id_barrier.size = fragment_id_buffer_size;
        fragment_id_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fragment_id_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fragment_id_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        fragment_id_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        fragment_id_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        fragment_id_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        engine::synchronization::begin_barriers();
        engine::synchronization::apply_barrier(offsets_barrier);
        engine::synchronization::apply_barrier(fragment_id_barrier);
        engine::synchronization::end_barriers();

        uint8_t fragment_id_pc[FragmentID::size]{};
        auto stages_addr = stages.address();
        FragmentID::write(fragment_id_pc,
                          glm::vec<2, uint32_t>{engine::swapchain_extent.width, engine::swapchain_extent.height},
                          stages_addr + current_offsets_offset, stages_addr + current_fragment_id_offset, draw_id_addr,
                          max_material_id);

        auto frag_id_set = engine::descriptor::new_set(storage_image_set_layout);
        engine::descriptor::begin_update(frag_id_set);
        engine::descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL, engine::visbuffer::get_view());
        engine::descriptor::end_update();

        fragment_id_pipeline.bind();
        fragment_id_pipeline.dispatch(engine::ComputePipeline::DispatchParams{
            .push_constant = fragment_id_pc,
            .descriptor_indexes =
                {
                    frag_id_set,
                    engine::descriptor::null_set,
                    engine::descriptor::null_set,
                    engine::descriptor::null_set,
                },
            .group_count_x = (uint32_t)std::ceil(engine::swapchain_extent.width / 16.0f),
            .group_count_y = (uint32_t)std::ceil(engine::swapchain_extent.height / 16.0f),
            .group_count_z = 1,
        });
    }

    Shading shade(VkImageView target) {
        auto current_fragment_id_offset = fragment_id_buffer_offsets[get_current_frame()];
        VkBufferMemoryBarrier2 fragment_id_barrier{};
        fragment_id_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        fragment_id_barrier.buffer = stages.data();
        fragment_id_barrier.offset = current_fragment_id_offset;
        fragment_id_barrier.size = fragment_id_buffer_size;
        fragment_id_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fragment_id_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fragment_id_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        fragment_id_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        fragment_id_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        fragment_id_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        auto current_shading_dispatch_offset = shading_dispatch_buffer_offsets[get_current_frame()];
        VkBufferMemoryBarrier2 shading_dispatch_barrier{};
        shading_dispatch_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        shading_dispatch_barrier.buffer = stages.data();
        shading_dispatch_barrier.offset = current_shading_dispatch_offset;
        shading_dispatch_barrier.size = shading_dispatch_buffer_size;
        shading_dispatch_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shading_dispatch_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shading_dispatch_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        shading_dispatch_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        shading_dispatch_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        shading_dispatch_barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR;

        engine::synchronization::begin_barriers();
        engine::synchronization::apply_barrier(fragment_id_barrier);
        engine::synchronization::apply_barrier(shading_dispatch_barrier);
        engine::synchronization::end_barriers();

        auto shading_set = engine::descriptor::new_set(shading_set_layout);
        engine::descriptor::begin_update(shading_set);
        engine::descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL, target);
        engine::descriptor::update_storage_image(1, VK_IMAGE_LAYOUT_GENERAL, engine::visbuffer::get_view());
        engine::descriptor::end_update();

        return Shading{
            .indirect_buffer_offset = current_shading_dispatch_offset,
            .fragment_id_buffer_offset = current_fragment_id_offset,
            .vis_and_target_set = shading_set,
            .material_id_count = (uint16_t)(max_material_id + 1),
        };
    }

    void clear_buffers() {
        VkBufferMemoryBarrier2 stages_barrier{};
        stages_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        stages_barrier.buffer = stages.data();
        stages_barrier.offset = 0;
        stages_barrier.size = stages.size();
        stages_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        stages_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        stages_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        stages_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        stages_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        stages_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        synchronization::begin_barriers();
        synchronization::apply_barrier(stages_barrier);
        synchronization::end_barriers();

        vkCmdFillBuffer(get_cmd_buf(), stages.data(), 0, stages.size(), 0);
    }
}
