#include "goliath/rendering.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"
#include "goliath/texture_pool.hpp"
#include "texture_pool_.hpp"

#include <fstream>
#include <ios>
#include <variant>
#include <volk.h>
#include <vulkan/vulkan_core.h>

namespace engine::rendering {
    VkDescriptorSetLayout empty_set;

    void create_empty_set() {
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        vkCreateDescriptorSetLayout(device, &info, nullptr, &empty_set);
    }

    void destroy_empty_set() {
        vkDestroyDescriptorSetLayout(device, empty_set, nullptr);
    }
}

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

VkShaderEXT engine::Shader::create(Source source, uint32_t push_constant_size,
                                   std::span<VkDescriptorSetLayout> set_layouts) {
    std::size_t code_size = 0;
    uint8_t* code_ptr = nullptr;
    bool free = false;

    std::visit(
        [&code_size, &code_ptr, &free](const auto& src) {
            if constexpr (std::is_same_v<std::span<uint8_t>, std::decay_t<decltype(src)>>) {
                code_size = src.size();
                code_ptr = src.data();
            } else {
                std::ifstream file{src, std::ios::binary | std::ios::ate};

                code_size = (std::size_t)file.tellg();
                assert(code_size != (std::size_t)-1 && "file wasn't found");
                code_ptr = new uint8_t[code_size];
                free = true;

                file.seekg(0, std::ios::beg);
                file.read((char*)code_ptr, (std::streamsize)code_size);
            }
        },
        source);

    VkShaderEXT shader;

    VkPushConstantRange push_constant{};
    push_constant.offset = 0;
    push_constant.size = push_constant_size;
    push_constant.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
    VkShaderCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
    info.pNext = nullptr;
    info.flags = 0;
    info.stage = _stage;
    info.nextStage = _next_stage;
    info.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
    info.codeSize = code_size;
    info.pCode = code_ptr;
    info.pName = _main;
    info.setLayoutCount = (uint32_t)set_layouts.size();
    info.pSetLayouts = set_layouts.data();
    info.pushConstantRangeCount = push_constant_size == 0 ? 0 : 1;
    info.pPushConstantRanges = &push_constant;
    info.pSpecializationInfo = nullptr;

    vkCreateShadersEXT(device, 1, &info, nullptr, &shader);

    if (free) {
        delete[] code_ptr;
    }

    return shader;
}

void engine::destroy_shader(VkShaderEXT shader) {
    vkDestroyShaderEXT(device, shader, nullptr);
}

engine::Pipeline::Pipeline() {
    _set_layout[0] = rendering::empty_set;
    _set_layout[1] = rendering::empty_set;
    _set_layout[2] = rendering::empty_set;
    _set_layout[3] = texture_pool::set_layout;
    _push_constant_size = 0;

    _depth_test = false;
    _depth_write = false;
    _depth_bias = std::nullopt;
    _stencil_test = false;
    _cull_mode = CullMode::None;
    _fill_mode = FillMode::Fill;
    _sample_count = SampleCount::One;
    _topology = Topology::TriangleList;
    _front_face = FrontFace::CCW;

    _viewport.height = (float)swapchain_extent.height;
    _viewport.width = (float)swapchain_extent.width;
    _viewport.x = 0;
    _viewport.y = 0;
    _viewport.minDepth = 0;
    _viewport.maxDepth = 1;

    _scissor.extent = swapchain_extent;
    _scissor.offset = VkOffset2D{.x = 0, .y = 0};

    _color_components |= VK_COLOR_COMPONENT_R_BIT;
    _color_components |= VK_COLOR_COMPONENT_G_BIT;
    _color_components |= VK_COLOR_COMPONENT_B_BIT;
    _color_components |= VK_COLOR_COMPONENT_A_BIT;
}

engine::Pipeline&& engine::Pipeline::vertex(engine::Shader vert, engine::Shader::Source vert_source) {
    _vertex = vert.create(vert_source, _push_constant_size, {_set_layout, 4});
    return std::move(*this);
}

engine::Pipeline&& engine::Pipeline::fragment(engine::Shader frag, engine::Shader::Source frag_source) {
    _fragment = frag.create(frag_source, _push_constant_size, {_set_layout, 4});
    return std::move(*this);
}

engine::Pipeline&& engine::Pipeline::clear_descriptor(uint32_t index) {
    _set_layout[index] = rendering::empty_set;
    return std::move(*this);
}

void engine::Pipeline::destroy(std::array<bool, 3> sets_to_destroy) {
    for (std::size_t i = 0; i < 3; i++) {
        if (!sets_to_destroy[i]) continue;

        vkDestroyDescriptorSetLayout(device, _set_layout[i], nullptr);
    }

    vkDestroyPipelineLayout(device, _pipeline_layout, nullptr);
}

engine::Pipeline&& engine::Pipeline::update_layout() {
    VkPushConstantRange push_constant_range;
    push_constant_range.offset = 0;
    push_constant_range.size = _push_constant_size;
    push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;
    info.setLayoutCount = 4;
    info.pSetLayouts = _set_layout;
    info.pushConstantRangeCount = _push_constant_size == 0 ? 0 : 1;
    info.pPushConstantRanges = &push_constant_range;

    vkCreatePipelineLayout(device, &info, nullptr, &_pipeline_layout);

    return std::move(*this);
}

void engine::Pipeline::draw(const DrawParams& params) {
    auto cmd_buf = get_cmd_buf();
    auto& descriptor_pool = get_frame_descriptor_pool();

    vkCmdSetCullModeEXT(cmd_buf, static_cast<VkCullModeFlags>(_cull_mode));
    vkCmdSetDepthTestEnableEXT(cmd_buf, _depth_test);
    if (_depth_test) {
        vkCmdSetDepthWriteEnableEXT(cmd_buf,_depth_write);
        vkCmdSetDepthCompareOpEXT(cmd_buf, _depth_cmp_op);
    }

    vkCmdSetDepthBiasEnableEXT(cmd_buf, _depth_bias ? 1 : 0);
    if (_depth_bias) {
        vkCmdSetDepthBias2EXT(cmd_buf, &*_depth_bias);
    }

    vkCmdSetRasterizerDiscardEnableEXT(cmd_buf, false);
    vkCmdSetPrimitiveRestartEnableEXT(cmd_buf, false);
    vkCmdSetStencilTestEnableEXT(cmd_buf, _stencil_test);
    vkCmdSetPolygonModeEXT(cmd_buf, static_cast<VkPolygonMode>(_fill_mode));
    vkCmdSetRasterizationSamplesEXT(cmd_buf, static_cast<VkSampleCountFlagBits>(_sample_count));
    vkCmdSetVertexInputEXT(cmd_buf, 0, nullptr, 0, nullptr);
    vkCmdSetPrimitiveTopologyEXT(cmd_buf, static_cast<VkPrimitiveTopology>(_topology));

    uint32_t enable_blend = false;
    vkCmdSetColorBlendEnableEXT(cmd_buf, 0, 1, &enable_blend);

    VkSampleMask sample_mask = 0xFFFFFFFF;
    vkCmdSetSampleMaskEXT(cmd_buf, static_cast<VkSampleCountFlagBits>(_sample_count), &sample_mask);

    vkCmdSetFrontFaceEXT(cmd_buf, static_cast<VkFrontFace>(_front_face));
    vkCmdSetAlphaToCoverageEnableEXT(cmd_buf, false);
    vkCmdSetViewportWithCountEXT(cmd_buf, 1, &_viewport);
    vkCmdSetScissorWithCountEXT(cmd_buf, 1, &_scissor);

    vkCmdSetColorWriteMaskEXT(cmd_buf, 0, 1, &_color_components);

    VkShaderStageFlagBits stages[2] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    VkShaderEXT shaders[2] = {_vertex, _fragment};
    vkCmdBindShadersEXT(cmd_buf, 2, stages, shaders);

    if (_push_constant_size != 0) {
        vkCmdPushConstants(cmd_buf, _pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, _push_constant_size,
                           params.push_constant);
    }

    for (uint32_t i = 0; i < 3; i++) {
        if (params.descriptor_indexes[i] == (uint64_t)-1) continue;

        descriptor_pool.bind_set(params.descriptor_indexes[i], cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 _pipeline_layout, i);
    }

    texture_pool::bind(VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline_layout);

    vkCmdDraw(cmd_buf, params.vertex_count, params.instance_count, params.first_vertex_id, params.first_instance_id);
}

engine::Pipeline2::Pipeline2(const engine::PipelineBuilder& builder) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = builder._vertex;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[1].module = builder._fragment;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vert_input{};
    vert_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = false;

    VkPipelineColorBlendStateCreateInfo blend_state{};
    blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_state.attachmentCount = 1;
    blend_state.pAttachments = &color_blend_attachment;

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        // VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = sizeof(dynamic_states)/sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dynamic_states;

    VkPushConstantRange push_constant_range{};
    push_constant_range.size = builder._push_constant_size;
    push_constant_range.offset = 0;
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 4;
    layout_info.pSetLayouts = builder._set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;
    vkCreatePipelineLayout(device, &layout_info, nullptr, &_pipeline_layout);

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
    info.layout = _pipeline_layout;

    vkCreateGraphicsPipelines(device, nullptr, 1, &info, nullptr, &_pipeline);
}

namespace engine::rendering {
    void begin(const RenderPass& render_pass) {
        vkCmdBeginRendering(get_cmd_buf(), &render_pass._info);
    }

    void end() {
        vkCmdEndRendering(get_cmd_buf());
    }
}
