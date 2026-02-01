#include "goliath/rendering.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"

#include <volk.h>
#include <vulkan/vulkan_core.h>

engine::RenderPass::RenderPass() {
    _info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    _info.pNext = nullptr;
    _info.colorAttachmentCount = 0;
    _info.pColorAttachments = nullptr;
    _info.layerCount = 1;
    _info.pDepthAttachment = nullptr;
    _info.pStencilAttachment = nullptr;
    _info.renderArea = VkRect2D{
        .offset = VkOffset2D{.x = 0, .y = 0},
        .extent = swapchain_extent,
    };
    _info.flags = 0;
    _info.viewMask = 0;
}

engine::GraphicsPipelineBuilder::GraphicsPipelineBuilder() {
    _set_layout[0] = descriptor::empty_set;
    _set_layout[1] = descriptor::empty_set;
    _set_layout[2] = descriptor::empty_set;
    _set_layout[3] = descriptor::empty_set;
}

engine::GraphicsPipelineBuilder&& engine::GraphicsPipelineBuilder::clear_descriptor(uint32_t index) {
    _set_layout[index] = descriptor::empty_set;
    return std::move(*this);
}

void engine::GraphicsPipeline::bind() const {
    auto cmd_buf = get_cmd_buf();

    vkCmdSetPrimitiveTopologyEXT(cmd_buf, static_cast<VkPrimitiveTopology>(_topology));
    vkCmdSetPrimitiveRestartEnable(cmd_buf, _primitive_restart_enable);

    vkCmdSetViewport(cmd_buf, 0, 1, &_viewport);
    vkCmdSetScissor(cmd_buf, 0, 1, &_scissor);

    vkCmdSetCullMode(cmd_buf, static_cast<VkCullModeFlags>(_cull_mode));
    vkCmdSetFrontFace(cmd_buf, static_cast<VkFrontFace>(_front_face));
    vkCmdSetLineWidth(cmd_buf, _line_width);
    vkCmdSetStencilTestEnable(cmd_buf, _stencil_test);
    if (_stencil_test) {
        vkCmdSetStencilOp(cmd_buf, _stencil_face_flag, _stencil_fail_op, _stencil_pass_op, _stencil_depth_fail_op,
                          static_cast<VkCompareOp>(_stencil_compare_op));
        vkCmdSetStencilCompareMask(cmd_buf, _stencil_face_flag, _stencil_compare_mask);
        vkCmdSetStencilWriteMask(cmd_buf, _stencil_face_flag, _stencil_write_mask);
    }

    vkCmdSetDepthTestEnable(cmd_buf, _depth_test);
    if (_depth_test) {
        vkCmdSetDepthCompareOp(cmd_buf, static_cast<VkCompareOp>(_depth_compare_op));
    }
    vkCmdSetDepthWriteEnable(cmd_buf, _depth_write);

    vkCmdSetDepthBiasEnableEXT(cmd_buf, _depth_bias.has_value());
    if (_depth_bias) {
        vkCmdSetDepthBias2EXT(cmd_buf, &*_depth_bias);
    }

    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
}

void engine::GraphicsPipeline::draw(const DrawParams& params) const {
    auto cmd_buf = get_cmd_buf();
    auto& descriptor_pool = get_frame_descriptor_pool();

    if (_push_constant_size != 0) {
        vkCmdPushConstants(cmd_buf, _pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, _push_constant_size,
                           params.push_constant);
    }

    for (uint32_t i = 0; i < params.descriptor_indexes.size(); i++) {
        if (std::holds_alternative<uint64_t>(params.descriptor_indexes[i])) {
            auto ix = std::get<uint64_t>(params.descriptor_indexes[i]);
            if (ix == (uint64_t)-1) continue;

            descriptor_pool.bind_set(std::get<uint64_t>(params.descriptor_indexes[i]), cmd_buf,
                                     VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout, i);
        } else {
            auto set = std::get<VkDescriptorSet>(params.descriptor_indexes[i]);
            vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout, i, 1, &set, 0, nullptr);
        }
    }

    vkCmdDraw(cmd_buf, params.vertex_count, params.instance_count, params.first_vertex_id, params.first_instance_id);
}

void engine::GraphicsPipeline::draw_indirect(const DrawIndirectParams& params) const {
    auto cmd_buf = get_cmd_buf();
    auto& descriptor_pool = get_frame_descriptor_pool();

    if (_push_constant_size != 0) {
        vkCmdPushConstants(cmd_buf, _pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, _push_constant_size,
                           params.push_constant);
    }

    for (uint32_t i = 0; i < params.descriptor_indexes.size(); i++) {
        if (std::holds_alternative<uint64_t>(params.descriptor_indexes[i])) {
            auto ix = std::get<uint64_t>(params.descriptor_indexes[i]);
            if (ix == (uint64_t)-1) continue;

            descriptor_pool.bind_set(std::get<uint64_t>(params.descriptor_indexes[i]), cmd_buf,
                                     VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout, i);
        } else {
            auto set = std::get<VkDescriptorSet>(params.descriptor_indexes[i]);
            vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout, i, 1, &set, 0, nullptr);
        }
    }

    vkCmdDrawIndirect(cmd_buf, params.draw_buffer, params.start_offset, params.draw_count, params.stride);
}

void engine::GraphicsPipeline::draw_indirect_count(const DrawIndirectCountParams& params) const {
    auto cmd_buf = get_cmd_buf();
    auto& descriptor_pool = get_frame_descriptor_pool();

    if (_push_constant_size != 0) {
        vkCmdPushConstants(cmd_buf, _pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, _push_constant_size,
                           params.push_constant);
    }

    for (uint32_t i = 0; i < params.descriptor_indexes.size(); i++) {
        if (std::holds_alternative<uint64_t>(params.descriptor_indexes[i])) {
            auto ix = std::get<uint64_t>(params.descriptor_indexes[i]);
            if (ix == (uint64_t)-1) continue;

            descriptor_pool.bind_set(std::get<uint64_t>(params.descriptor_indexes[i]), cmd_buf,
                                     VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout, i);
        } else {
            auto set = std::get<VkDescriptorSet>(params.descriptor_indexes[i]);
            vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout, i, 1, &set, 0, nullptr);
        }
    }

    vkCmdDrawIndirectCount(cmd_buf, params.draw_buffer, params.draw_offset, params.count_buffer, params.count_offset,
                           params.max_draw_count, params.stride);
}

namespace engine::rendering {
    void begin(const RenderPass& render_pass) {
        vkCmdBeginRendering(get_cmd_buf(), &render_pass._info);
    }

    void end() {
        vkCmdEndRendering(get_cmd_buf());
    }

    GraphicsPipeline create_pipeline(const GraphicsPipelineBuilder& builder) {
        GraphicsPipeline res{};
        res._push_constant_size = builder._push_constant_size;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = builder._vertex;
        stages[0].pName = "main";

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = builder._fragment;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vert_input{};
        vert_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        VkPipelineColorBlendStateCreateInfo blend_state{};
        blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend_state.attachmentCount = (uint32_t)builder._color_blend_attachments.size();
        blend_state.pAttachments = builder._color_blend_attachments.data();

        VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
            VK_DYNAMIC_STATE_CULL_MODE,
            VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
            VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
            VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
            VK_DYNAMIC_STATE_STENCIL_OP,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        };
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = sizeof(dynamic_states) / sizeof(VkDynamicState);
        dynamic_state.pDynamicStates = dynamic_states;

        VkPushConstantRange push_constant_range{};
        push_constant_range.size = builder._push_constant_size;
        push_constant_range.offset = 0;
        push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 4;
        layout_info.pSetLayouts = builder._set_layout;
        layout_info.pushConstantRangeCount = builder._push_constant_size == 0 ? 0 : 1;
        layout_info.pPushConstantRanges = &push_constant_range;
        VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &res._pipeline_layout));

        VkPipelineRenderingCreateInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount = (uint32_t)builder._color_format_attachments.size();
        rendering.pColorAttachmentFormats = builder._color_format_attachments.data();
        rendering.depthAttachmentFormat = builder._depth_format;
        rendering.stencilAttachmentFormat = builder._stencil_format;
        rendering.viewMask = 0;

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vert_input;
        info.pInputAssemblyState = &input_assembly;
        info.pViewportState = &viewport_state;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisampling;
        info.pDepthStencilState = &depth_stencil;
        info.pColorBlendState = &blend_state;
        info.pDynamicState = &dynamic_state;
        info.layout = res._pipeline_layout;

        VK_CHECK(vkCreateGraphicsPipelines(device, nullptr, 1, &info, nullptr, &res._pipeline));

        res.update_viewport_to_swapchain();
        res.update_scissor_to_viewport();

        return res;
    }

    void destroy_pipeline(const GraphicsPipeline& pipeline) {
        vkDestroyPipeline(device, pipeline._pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipeline._pipeline_layout, nullptr);
    }
}

namespace engine::shader {
    VkShaderModule create(std::span<uint8_t> code) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = (uint32_t*)code.data();

        VkShaderModule out;
        vkCreateShaderModule(device, &info, nullptr, &out);
        return out;
    }

    void destroy(VkShaderModule shader) {
        vkDestroyShaderModule(device, shader, nullptr);
    }
}
