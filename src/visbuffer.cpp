#include "goliath/visbuffer.hpp"
#include "engine_.hpp"
#include "goliath/buffer.hpp"
#include "goliath/compute.hpp"
#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/push_constant.hpp"
#include "goliath/rendering.hpp"
#include "goliath/synchronization.hpp"
#include "goliath/texture.hpp"
#include "goliath/util.hpp"

#include <cstdlib>
#include <volk.h>
#include <vulkan/vulkan_core.h>

namespace engine::visbuffer {
    VkDescriptorSetLayout storage_image_layout;
    VkDescriptorSetLayout shading_layout;

    using MaterialCount = PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t>;
    ComputePipeline material_count_pipeline;

    using Offsets = PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t, uint64_t, uint32_t, uint32_t>;
    ComputePipeline offsets_pipeline;

    using FragmentID = PushConstant<glm::vec<2, uint32_t>, uint64_t, uint64_t, uint64_t, uint32_t>;
    ComputePipeline fragment_id_pipeline;

    void init() {
        storage_image_layout = descriptor::create_layout(DescriptorSet<descriptor::Binding{
                                                             .count = 1,
                                                             .type = descriptor::Binding::StorageImage,
                                                             .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                         }>{});

        shading_layout = descriptor::create_layout(DescriptorSet<descriptor::Binding{
                                                                     .count = 1,
                                                                     .type = descriptor::Binding::StorageImage,
                                                                     .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                                 },
                                                                 descriptor::Binding{
                                                                     .count = 1,
                                                                     .type = descriptor::Binding::StorageImage,
                                                                     .stages = VK_SHADER_STAGE_COMPUTE_BIT,
                                                                 }>{});

        uint32_t material_count_size;
        auto* material_count_spv = util::read_file("./material_count.spv", &material_count_size);
        auto material_count_module = shader::create({material_count_spv, material_count_size});
        material_count_pipeline = compute::create(ComputePipelineBuilder{}
                                                      .shader(material_count_module)
                                                      .descriptor_layout(0, storage_image_layout)
                                                      .push_constant(MaterialCount::size));
        shader::destroy(material_count_module);
        free(material_count_spv);

        uint32_t offsets_size;
        auto* offsets_spv = util::read_file("./offsets.spv", &offsets_size);
        auto offsets_module = shader::create({offsets_spv, offsets_size});
        offsets_pipeline =
            compute::create(ComputePipelineBuilder{}.shader(offsets_module).push_constant(Offsets::size));
        shader::destroy(offsets_module);
        free(offsets_spv);

        uint32_t fragment_id_size;
        auto* fragment_id_spv = util::read_file("./fragment_id.spv", &fragment_id_size);
        auto fragment_id_module = shader::create({fragment_id_spv, fragment_id_size});
        fragment_id_pipeline = compute::create(ComputePipelineBuilder{}
                                                   .shader(fragment_id_module)
                                                   .descriptor_layout(0, storage_image_layout)
                                                   .push_constant(FragmentID::size));
        shader::destroy(fragment_id_module);
        free(fragment_id_spv);
    }

    void destroy() {
        descriptor::destroy_layout(storage_image_layout);
        descriptor::destroy_layout(shading_layout);

        compute::destroy(material_count_pipeline);
        compute::destroy(offsets_pipeline);
        compute::destroy(fragment_id_pipeline);
    }

    void _resize(VisBuffer& visbuffer, bool dims_changed) {
        if (dims_changed) {
            for (size_t i = 0; i < frames_in_flight; i++) {
                visbuffer.images[i] =
                    gpu_image::upload(std::format("Visbuffer #{}", i).c_str(),
                                      GPUImageInfo{}
                                          .new_layout(VK_IMAGE_LAYOUT_GENERAL)
                                          .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                          .width(visbuffer.dimensions.x)
                                          .height(visbuffer.dimensions.y)
                                          .format(format)
                                          .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
                                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
                visbuffer.image_views[i] =
                    gpu_image_view::create(GPUImageView{visbuffer.images[i]}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT));
            }
        }

        if (visbuffer.material_count_changed || dims_changed) {
            auto alignment = state->physical_device_properties.limits.minStorageBufferOffsetAlignment;
            visbuffer.material_count_buffer_size =
                util::align_up(alignment, sizeof(uint32_t) * (visbuffer.max_material_id + 1));
            visbuffer.offsets_buffer_size =
                util::align_up(alignment, sizeof(uint32_t) * (visbuffer.max_material_id + 1));
            visbuffer.shading_dispatch_buffer_size =
                util::align_up(alignment, (sizeof(VkDispatchIndirectCommand) + 2 * sizeof(uint32_t)) *
                                              (visbuffer.max_material_id + 1));
            visbuffer.fragment_id_buffer_size =
                util::align_up(alignment, visbuffer.dimensions.x * visbuffer.dimensions.y * sizeof(uint32_t));

            visbuffer.stages = Buffer::create(
                "visbuffer stages buffer",
                frames_in_flight * (visbuffer.material_count_buffer_size + visbuffer.offsets_buffer_size +
                                    visbuffer.shading_dispatch_buffer_size + visbuffer.fragment_id_buffer_size),
                VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT |
                    VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT,
                std::nullopt);

            uint32_t offset = 0;
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                visbuffer.material_count_buffer_offsets[i] = offset;
                offset += visbuffer.material_count_buffer_size;
            }
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                visbuffer.offsets_buffer_offsets[i] = offset;
                offset += visbuffer.offsets_buffer_size;
            }
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                visbuffer.shading_dispatch_buffer_offsets[i] = offset;
                offset += visbuffer.shading_dispatch_buffer_size;
            }
            for (std::size_t i = 0; i < frames_in_flight; i++) {
                visbuffer.fragment_id_buffer_offsets[i] = offset;
                offset += visbuffer.fragment_id_buffer_size;
            }
        }

        visbuffer.material_count_changed = false;
    }

    VisBuffer create(glm::uvec2 dimensions) {
        VisBuffer visbuffer{
            .max_material_id = 0,
            .material_count_changed = false,
            .dimensions = dimensions,
        };

        _resize(visbuffer, true);

        return visbuffer;
    }

    void resize(VisBuffer& visbuffer, glm::uvec2 new_dimensions) {
        bool dims_changed = visbuffer.dimensions != new_dimensions;
        if (dims_changed) {
            visbuffer.dimensions = new_dimensions;
            for (size_t i = 0; i < frames_in_flight; i++) {
                gpu_image_view::destroy(visbuffer.image_views[i]);
                gpu_image::destroy(visbuffer.images[i]);
            }
        }

        if (dims_changed || visbuffer.material_count_changed) visbuffer.stages.destroy();

        _resize(visbuffer, dims_changed);
    }

    void destroy(VisBuffer& visbuffer) {
        for (size_t i = 0; i < frames_in_flight; i++) {
            gpu_image_view::destroy(visbuffer.image_views[i]);
            gpu_image::destroy(visbuffer.images[i]);
        }

        visbuffer.stages.destroy();
    }

    void push_material(VisBuffer& visbuffer, uint16_t n) {
        visbuffer.max_material_id += n;
        visbuffer.material_count_changed = true;
    }

    void pop_material(VisBuffer& visbuffer, uint16_t n) {
        visbuffer.max_material_id -= n;
        visbuffer.material_count_changed = true;
    }

    VkImageMemoryBarrier2 transition(VisBuffer& visbuffer, uint32_t current_frame, std::optional<VkImageLayout> new_layout,
                                     VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        return visbuffer.images[current_frame % frames_in_flight].transition(new_layout, dst_stage, dst_access);
    }

    void clear_buffers(VisBuffer& visbuffer, uint32_t current_frame) {
        current_frame = current_frame % frames_in_flight;

        VkBufferMemoryBarrier2 stages_barrier{};
        stages_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        stages_barrier.buffer = visbuffer.stages.data();
        stages_barrier.offset = 0;
        stages_barrier.size = visbuffer.stages.size();
        stages_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        stages_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        stages_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        stages_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        stages_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        stages_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        VkBufferMemoryBarrier2 materials_barrier = stages_barrier;
        materials_barrier.offset = visbuffer.material_count_buffer_offsets[current_frame];
        materials_barrier.size = visbuffer.material_count_buffer_size;

        VkBufferMemoryBarrier2 offsets_barrier = stages_barrier;
        materials_barrier.offset = visbuffer.offsets_buffer_offsets[current_frame];
        materials_barrier.size = visbuffer.offsets_buffer_size;

        VkBufferMemoryBarrier2 shading_barrier = stages_barrier;
        materials_barrier.offset = visbuffer.shading_dispatch_buffer_offsets[current_frame];
        materials_barrier.size = visbuffer.shading_dispatch_buffer_size;

        VkBufferMemoryBarrier2 fragment_id_barrier = stages_barrier;
        materials_barrier.offset = visbuffer.fragment_id_buffer_offsets[current_frame];
        materials_barrier.size = visbuffer.fragment_id_buffer_size;

        synchronization::begin_barriers();
        synchronization::apply_barrier(materials_barrier);
        synchronization::apply_barrier(offsets_barrier);
        synchronization::apply_barrier(shading_barrier);
        synchronization::apply_barrier(fragment_id_barrier);
        synchronization::end_barriers();

        vkCmdFillBuffer(get_cmd_buf(), visbuffer.stages.data(), visbuffer.material_count_buffer_offsets[current_frame],
                        visbuffer.material_count_buffer_size, 0);
        vkCmdFillBuffer(get_cmd_buf(), visbuffer.stages.data(), visbuffer.offsets_buffer_offsets[current_frame],
                        visbuffer.offsets_buffer_size, 0);
        vkCmdFillBuffer(get_cmd_buf(), visbuffer.stages.data(),
                        visbuffer.shading_dispatch_buffer_offsets[current_frame],
                        visbuffer.shading_dispatch_buffer_size, 0);
        vkCmdFillBuffer(get_cmd_buf(), visbuffer.stages.data(), visbuffer.fragment_id_buffer_offsets[current_frame],
                        visbuffer.fragment_id_buffer_size, 0);
    }

    void prepare_for_draw(VisBuffer& visbuffer, uint32_t current_frame) {
        synchronization::begin_barriers();
        synchronization::apply_barrier(transition(visbuffer, current_frame, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));
        synchronization::end_barriers();
    }

    void count_materials(VisBuffer& visbuffer, uint64_t draw_id_addr, uint32_t current_frame) {
        auto current_material_count_offset = visbuffer.material_count_buffer_offsets[current_frame % frames_in_flight];

        VkBufferMemoryBarrier2 material_count_barrier{};
        material_count_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        material_count_barrier.buffer = visbuffer.stages.data();
        material_count_barrier.offset = current_material_count_offset;
        material_count_barrier.size = visbuffer.material_count_buffer_size;
        material_count_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        material_count_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        material_count_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        material_count_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        material_count_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        material_count_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        synchronization::begin_barriers();
        synchronization::apply_barrier(transition(visbuffer, current_frame, VK_IMAGE_LAYOUT_GENERAL,
                                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT));
        synchronization::apply_barrier(material_count_barrier);
        synchronization::end_barriers();

        auto material_count_set = descriptor::new_set(storage_image_layout);
        descriptor::begin_update(material_count_set);
        descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL, visbuffer.image_views[current_frame]);
        descriptor::end_update();

        uint8_t mat_count_pc[MaterialCount::size]{};
        MaterialCount::write(mat_count_pc, visbuffer.dimensions, visbuffer.stages.address() + current_material_count_offset,
                             draw_id_addr);

        material_count_pipeline.bind();
        material_count_pipeline.dispatch(ComputePipeline::DispatchParams{
            .push_constant = mat_count_pc,
            .descriptor_indexes =
                {
                    material_count_set,
                    descriptor::null_set,
                    descriptor::null_set,
                    descriptor::null_set,
                },
            .group_count_x = (uint32_t)std::ceil(visbuffer.dimensions.x / 16.0f),
            .group_count_y = (uint32_t)std::ceil(visbuffer.dimensions.y / 16.0f),
            .group_count_z = 1,
        });
    }

    void get_offsets(VisBuffer& visbuffer, uint32_t current_frame) {
        current_frame = current_frame % frames_in_flight;

        auto current_material_count_offset = visbuffer.material_count_buffer_offsets[current_frame];
        VkBufferMemoryBarrier2 material_count_barrier{};
        material_count_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        material_count_barrier.buffer = visbuffer.stages.data();
        material_count_barrier.offset = current_material_count_offset;
        material_count_barrier.size = visbuffer.material_count_buffer_size;
        material_count_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        material_count_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        material_count_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        material_count_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        material_count_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        material_count_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        auto current_offsets_offset = visbuffer.offsets_buffer_offsets[current_frame];
        VkBufferMemoryBarrier2 offsets_barrier{};
        offsets_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        offsets_barrier.buffer = visbuffer.stages.data();
        offsets_barrier.offset = current_offsets_offset;
        offsets_barrier.size = visbuffer.offsets_buffer_size;
        offsets_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        offsets_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        offsets_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        offsets_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        offsets_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        offsets_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        auto current_shading_dispatch_offset = visbuffer.shading_dispatch_buffer_offsets[current_frame];
        VkBufferMemoryBarrier2 shading_dispatch_barrier{};
        shading_dispatch_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        shading_dispatch_barrier.buffer = visbuffer.stages.data();
        shading_dispatch_barrier.offset = current_shading_dispatch_offset;
        shading_dispatch_barrier.size = visbuffer.shading_dispatch_buffer_size;
        shading_dispatch_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shading_dispatch_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shading_dispatch_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        shading_dispatch_barrier.srcStageMask =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR;
        shading_dispatch_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        shading_dispatch_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        synchronization::begin_barriers();
        synchronization::apply_barrier(material_count_barrier);
        synchronization::apply_barrier(offsets_barrier);
        synchronization::apply_barrier(shading_dispatch_barrier);
        synchronization::end_barriers();

        uint8_t offsets_pc[Offsets::size]{};
        auto stages_addr = visbuffer.stages.address();
        Offsets::write(offsets_pc, visbuffer.dimensions, stages_addr + current_material_count_offset,
                       stages_addr + current_offsets_offset, stages_addr + current_shading_dispatch_offset,
                       visbuffer.max_material_id + 1, 1 + visbuffer.max_material_id / 256);

        offsets_pipeline.bind();
        offsets_pipeline.dispatch(ComputePipeline::DispatchParams{
            .push_constant = offsets_pc,
            .group_count_x = (uint32_t)std::ceil((visbuffer.max_material_id + 1) / 256.0f),
            .group_count_y = 1,
            .group_count_z = 1,
        });
    }

    void write_fragment_ids(VisBuffer& visbuffer, uint64_t draw_id_addr, uint32_t current_frame) {
        current_frame = current_frame % frames_in_flight;

        auto current_offsets_offset = visbuffer.offsets_buffer_offsets[current_frame];
        VkBufferMemoryBarrier2 offsets_barrier{};
        offsets_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        offsets_barrier.buffer = visbuffer.stages.data();
        offsets_barrier.offset = current_offsets_offset;
        offsets_barrier.size = visbuffer.offsets_buffer_size;
        offsets_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        offsets_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        offsets_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        offsets_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        offsets_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        offsets_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        auto current_fragment_id_offset = visbuffer.fragment_id_buffer_offsets[current_frame];
        VkBufferMemoryBarrier2 fragment_id_barrier{};
        fragment_id_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        fragment_id_barrier.buffer = visbuffer.stages.data();
        fragment_id_barrier.offset = current_fragment_id_offset;
        fragment_id_barrier.size = visbuffer.fragment_id_buffer_size;
        fragment_id_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fragment_id_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fragment_id_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        fragment_id_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        fragment_id_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        fragment_id_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        synchronization::begin_barriers();
        synchronization::apply_barrier(offsets_barrier);
        synchronization::apply_barrier(fragment_id_barrier);
        synchronization::end_barriers();

        uint8_t fragment_id_pc[FragmentID::size]{};
        auto stages_addr = visbuffer.stages.address();
        FragmentID::write(fragment_id_pc, visbuffer.dimensions, stages_addr + current_offsets_offset,
                          stages_addr + current_fragment_id_offset, draw_id_addr, visbuffer.max_material_id);

        auto frag_id_set = descriptor::new_set(storage_image_layout);
        descriptor::begin_update(frag_id_set);
        descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL, visbuffer.image_views[current_frame]);
        descriptor::end_update();

        fragment_id_pipeline.bind();
        fragment_id_pipeline.dispatch(ComputePipeline::DispatchParams{
            .push_constant = fragment_id_pc,
            .descriptor_indexes =
                {
                    frag_id_set,
                    descriptor::null_set,
                    descriptor::null_set,
                    descriptor::null_set,
                },
            .group_count_x = (uint32_t)std::ceil(visbuffer.dimensions.x / 16.0f),
            .group_count_y = (uint32_t)std::ceil(visbuffer.dimensions.y / 16.0f),
            .group_count_z = 1,
        });
    }

    // `target` is expected to be already synced and it's layout to be GENERAL
    Shading shade(VisBuffer& visbuffer, VkImageView target, uint32_t current_frame) {
        current_frame = current_frame % frames_in_flight;

        auto current_fragment_id_offset = visbuffer.fragment_id_buffer_offsets[current_frame];
        VkBufferMemoryBarrier2 fragment_id_barrier{};
        fragment_id_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        fragment_id_barrier.buffer = visbuffer.stages.data();
        fragment_id_barrier.offset = current_fragment_id_offset;
        fragment_id_barrier.size = visbuffer.fragment_id_buffer_size;
        fragment_id_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fragment_id_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fragment_id_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        fragment_id_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        fragment_id_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        fragment_id_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        auto current_shading_dispatch_offset = visbuffer.shading_dispatch_buffer_offsets[current_frame];
        VkBufferMemoryBarrier2 shading_dispatch_barrier{};
        shading_dispatch_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        shading_dispatch_barrier.buffer = visbuffer.stages.data();
        shading_dispatch_barrier.offset = current_shading_dispatch_offset;
        shading_dispatch_barrier.size = visbuffer.shading_dispatch_buffer_size;
        shading_dispatch_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shading_dispatch_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shading_dispatch_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        shading_dispatch_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        shading_dispatch_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        shading_dispatch_barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR;

        synchronization::begin_barriers();
        synchronization::apply_barrier(fragment_id_barrier);
        synchronization::apply_barrier(shading_dispatch_barrier);
        synchronization::end_barriers();

        auto shading_set = descriptor::new_set(shading_layout);
        descriptor::begin_update(shading_set);
        descriptor::update_storage_image(0, VK_IMAGE_LAYOUT_GENERAL, target);
        descriptor::update_storage_image(1, VK_IMAGE_LAYOUT_GENERAL, visbuffer.image_views[current_frame]);
        descriptor::end_update();

        return Shading{
            .indirect_buffer_offset = current_shading_dispatch_offset,
            .fragment_id_buffer_offset = current_fragment_id_offset,
            .vis_and_target_set = shading_set,
            .material_id_count = (uint16_t)(visbuffer.max_material_id + 1),
        };
    }
}
