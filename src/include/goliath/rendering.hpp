#pragma once

#include "goliath/descriptor_pool.hpp"
#include "goliath/engine.hpp"
#include "goliath/util.hpp"

#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <variant>
#include <vector>
#include <volk.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/vec4.hpp>

namespace engine {
    enum struct LoadOp {
        Load = VK_ATTACHMENT_LOAD_OP_LOAD,
        Clear = VK_ATTACHMENT_LOAD_OP_CLEAR,
        DontCare = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };

    enum struct StoreOp {
        NoStore = VK_ATTACHMENT_STORE_OP_NONE,
        Store = VK_ATTACHMENT_STORE_OP_STORE,
        DontCare = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    };

    struct RenderingAttachement {
        VkRenderingAttachmentInfo _info{};

        RenderingAttachement() {
            _info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            _info.pNext = nullptr;
        }

        operator VkRenderingAttachmentInfo() const {
            return _info;
        }

        RenderingAttachement&& set_image(VkImageView view, VkImageLayout layout) {
            _info.imageView = view;
            _info.imageLayout = layout;
            return std::move(*this);
        }

        RenderingAttachement&& set_resolve_image(VkImageView view, VkImageLayout layout, VkResolveModeFlagBits mode) {
            _info.resolveImageView = view;
            _info.resolveImageLayout = layout;
            _info.resolveMode = mode;
            return std::move(*this);
        }

        RenderingAttachement&& set_load_op(LoadOp load_op) {
            _info.loadOp = static_cast<VkAttachmentLoadOp>(load_op);
            return std::move(*this);
        }

        RenderingAttachement&& set_store_op(StoreOp store_op) {
            _info.storeOp = static_cast<VkAttachmentStoreOp>(store_op);
            return std::move(*this);
        }

        RenderingAttachement&& set_clear_color(glm::vec4 color) {
            std::memcpy(_info.clearValue.color.float32, glm::value_ptr(color), sizeof(float) * 4);
            return std::move(*this);
        }

        RenderingAttachement&& set_clear_depth(float depth) {
            _info.clearValue.depthStencil.depth = depth;
            return std::move(*this);
        }

        RenderingAttachement&& set_clear_stencil(uint32_t depth) {
            _info.clearValue.depthStencil.stencil = depth;
            return std::move(*this);
        }
    };

    class RenderPass {
      public:
        VkRenderingInfo _info;

        RenderPass();

        RenderPass&& add_color_attachment(VkRenderingAttachmentInfo attachment) {
            color_atts.emplace_back(attachment);

            update_color_atts();
            return std::move(*this);
        }

        RenderPass&& set_color_attachments(std::vector<VkRenderingAttachmentInfo> attachments) {
            color_atts = attachments;

            update_color_atts();
            return std::move(*this);
        }

        RenderPass&& update_color_attachment(uint32_t ix, VkRenderingAttachmentInfo attachment) {
            color_atts[ix] = attachment;

            return std::move(*this);
        }

        RenderPass&& clear_color_attachments() {
            color_atts.clear();
            color_atts.shrink_to_fit();

            update_color_atts();
            return std::move(*this);
        }

        RenderPass&& depth_attachment(VkRenderingAttachmentInfo attachment) {
            depth_att = attachment;
            _info.pDepthAttachment = &depth_att;

            return std::move(*this);
        }

        RenderPass&& clear_depth_attachment() {
            _info.pDepthAttachment = nullptr;

            return std::move(*this);
        }

        RenderPass&& stencil_attachment(VkRenderingAttachmentInfo attachment) {
            stencil_att = attachment;
            _info.pStencilAttachment = &stencil_att;

            return std::move(*this);
        }

        RenderPass&& clear_stencil_attachment() {
            _info.pStencilAttachment = nullptr;

            return std::move(*this);
        }

        RenderPass&& render_area(VkRect2D rect) {
            _info.renderArea = rect;
            return std::move(*this);
        }

        RenderPass&& render_area_offset(VkOffset2D off) {
            _info.renderArea.offset = off;
            return std::move(*this);
        }

      private:
        std::vector<VkRenderingAttachmentInfo> color_atts;
        VkRenderingAttachmentInfo depth_att;
        VkRenderingAttachmentInfo stencil_att;

        void update_color_atts() {
            _info.colorAttachmentCount = (uint32_t)color_atts.size();
            _info.pColorAttachments = color_atts.data();
        }
    };

    VkShaderModule create_shader(std::span<uint8_t> code);
    void destroy_shader(VkShaderModule shader);

    enum struct CullMode {
        NoCull = VK_CULL_MODE_NONE,
        Back = VK_CULL_MODE_BACK_BIT,
        Front = VK_CULL_MODE_FRONT_BIT,
        Both = VK_CULL_MODE_FRONT_AND_BACK,
    };

    enum struct FillMode {
        Fill = VK_POLYGON_MODE_FILL,
        Line = VK_POLYGON_MODE_LINE,
        Point = VK_POLYGON_MODE_POINT,
    };

    enum struct SampleCount {
        One = VK_SAMPLE_COUNT_1_BIT,
        Two = VK_SAMPLE_COUNT_2_BIT,
        Four = VK_SAMPLE_COUNT_4_BIT,
        Eight = VK_SAMPLE_COUNT_8_BIT,
        Sixteen = VK_SAMPLE_COUNT_16_BIT,
        ThirtyTwo = VK_SAMPLE_COUNT_32_BIT,
        SixtyFour = VK_SAMPLE_COUNT_64_BIT,
    };

    enum struct Topology {
        LineList = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        LineListAdjacency = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
        LineStrip = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
        LineStripAdjacency = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
        TriangleList = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        TriangleListAdjacency = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
        TriangleStrip = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        TriangleStripAdjancency = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
        TriangleFan = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
        Point = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        Patch = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
    };

    enum struct FrontFace {
        CW = VK_FRONT_FACE_CLOCKWISE,
        CCW = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    };

    struct BlendState {
        VkPipelineColorBlendAttachmentState _state{};

        BlendState() {
            _state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                    VK_COLOR_COMPONENT_A_BIT;
            _state.blendEnable = false;
        }

        BlendState&& blend(bool enable) {
            _state.blendEnable = enable;
            return std::move(*this);
        }

        BlendState&& color_blend_op(VkBlendOp op) {
            _state.colorBlendOp = op;
            return std::move(*this);
        }

        BlendState&& alpha_blend_op(VkBlendOp op) {
            _state.alphaBlendOp = op;
            return std::move(*this);
        }

        BlendState&& src_color_blend_factor(VkBlendFactor factor) {
            _state.srcColorBlendFactor = factor;
            return std::move(*this);
        }

        BlendState&& src_alpha_blend_factor(VkBlendFactor factor) {
            _state.srcAlphaBlendFactor = factor;
            return std::move(*this);
        }

        BlendState&& dst_color_blend_factor(VkBlendFactor factor) {
            _state.dstColorBlendFactor = factor;
            return std::move(*this);
        }

        BlendState&& dst_alpha_blend_factor(VkBlendFactor factor) {
            _state.dstAlphaBlendFactor = factor;
            return std::move(*this);
        }
    };

    struct GraphicsPipelineBuilder {
        VkShaderModule _vertex;
        VkShaderModule _fragment;

        VkDescriptorSetLayout _set_layout[4];
        uint32_t _push_constant_size = 0;

        FillMode _fill_mode = FillMode::Fill;

        std::vector<VkFormat> _color_format_attachments{};
        std::vector<VkPipelineColorBlendAttachmentState> _color_blend_attachments{};
        VkFormat _depth_format = VK_FORMAT_UNDEFINED;
        VkFormat _stencil_format = VK_FORMAT_UNDEFINED;

        GraphicsPipelineBuilder();

        GraphicsPipelineBuilder&& vertex(VkShaderModule vert) {
            _vertex = vert;
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& fragment(VkShaderModule frag) {
            _fragment = frag;
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& descriptor_layout(uint32_t index, VkDescriptorSetLayout layout) {
            _set_layout[index] = layout;
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& clear_descriptor(uint32_t index);

        GraphicsPipelineBuilder&& push_constant_size(uint32_t size) {
            _push_constant_size = size;
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& fill_mode(FillMode mode) {
            _fill_mode = mode;
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& add_color_attachment(VkFormat format, BlendState state) {
            _color_format_attachments.emplace_back(format);
            _color_blend_attachments.emplace_back(state._state);
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& add_color_attachment(VkFormat format) {
            _color_format_attachments.emplace_back(format);
            _color_blend_attachments.emplace_back(BlendState{}._state);
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& set_attachment_format(uint32_t ix, VkFormat format) {
            _color_format_attachments[ix] = format;
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& set_attachment_blend_state(uint32_t ix, BlendState state) {
            _color_blend_attachments[ix] = state._state;
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& depth_format(VkFormat format) {
            _depth_format = format;
            return std::move(*this);
        }

        GraphicsPipelineBuilder&& stencil_format(VkFormat format) {
            _stencil_format = format;
            return std::move(*this);
        }
    };

    struct GraphicsPipeline {
        using descriptor_set = std::variant<uint64_t, VkDescriptorSet>;

        struct DrawParams {
            void* push_constant = nullptr;
            std::array<descriptor_set, 4> descriptor_indexes = {descriptor::null_set, descriptor::null_set,
                                                          descriptor::null_set, descriptor::null_set};
            uint32_t vertex_count;
            uint32_t instance_count = 1;
            uint32_t first_vertex_id = 0;
            uint32_t first_instance_id = 0;
        };

        struct DrawIndirectParams {
            void* push_constant = nullptr;
            std::array<descriptor_set, 4> descriptor_indexes = {descriptor::null_set, descriptor::null_set,
                                                          descriptor::null_set, descriptor::null_set};
            VkBuffer draw_buffer;
            uint32_t start_offset = 0;
            uint32_t draw_count = 0;
            uint32_t stride = sizeof(VkDrawIndirectCommand);
        };

        struct DrawIndirectCountParams {
            void* push_constant = nullptr;
            std::array<descriptor_set, 4> descriptor_indexes = {descriptor::null_set, descriptor::null_set,
                                                          descriptor::null_set, descriptor::null_set};
            VkBuffer draw_buffer;
            uint32_t draw_offset = 0;
            VkBuffer count_buffer;
            uint32_t count_offset = 0;
            uint32_t max_draw_count = 0;
            uint32_t stride = sizeof(VkDrawIndirectCommand);
        };

        VkPipelineLayout _pipeline_layout;
        VkPipeline _pipeline;
        const uint32_t _push_constant_size;

        Topology _topology = Topology::TriangleList;
        bool _primitive_restart_enable = false;

        VkViewport _viewport;
        VkRect2D _scissor;

        CullMode _cull_mode = CullMode::Back;
        FrontFace _front_face = FrontFace::CCW;
        float _line_width = 1.0f;

        bool _stencil_test = false;
        VkStencilFaceFlags _stencil_face_flag;
        VkStencilOp _stencil_fail_op;
        VkStencilOp _stencil_pass_op;
        VkStencilOp _stencil_depth_fail_op;
        CompareOp _stencil_compare_op;
        uint32_t _stencil_compare_mask;
        uint32_t _stencil_write_mask;

        bool _depth_test = false;
        bool _depth_write = false;
        CompareOp _depth_compare_op;
        std::optional<VkDepthBiasInfoEXT> _depth_bias;

        GraphicsPipeline(const GraphicsPipelineBuilder& builder);

        void bind();
        void draw(const DrawParams& params);
        void draw_indirect(const DrawIndirectParams& params);
        void draw_indirect_count(const DrawIndirectCountParams& params);
        void destroy();

        GraphicsPipeline&& topology(Topology top) {
            _topology = top;
            return std::move(*this);
        }

        GraphicsPipeline&& primitive_restart(bool enable) {
            _primitive_restart_enable = enable;
            return std::move(*this);
        }

        GraphicsPipeline&& update_viewport_to_swapchain() {
            _viewport = VkViewport{
                .x = 0,
                .y = 0,
                .width = (float)swapchain_extent.width,
                .height = (float)swapchain_extent.height,
                .minDepth = 0,
                .maxDepth = 1,
            };
            return std::move(*this);
        }

        GraphicsPipeline&& update_scissor_to_viewport() {
            _scissor = VkRect2D{
                .offset =
                    VkOffset2D{
                        .x = (int32_t)_viewport.x,
                        .y = (int32_t)_viewport.y,
                    },
                .extent =
                    VkExtent2D{
                        .width = (uint32_t)_viewport.width,
                        .height = (uint32_t)_viewport.height,
                    },
            };
            return std::move(*this);
        }

        GraphicsPipeline&& cull_mode(CullMode mode) {
            _cull_mode = mode;
            return std::move(*this);
        }

        GraphicsPipeline&& front_face(FrontFace face) {
            _front_face = face;
            return std::move(*this);
        }

        GraphicsPipeline&& line_width(float width) {
            _line_width = width;
            return std::move(*this);
        }

        GraphicsPipeline&& stencil_test(bool enable) {
            _stencil_test = enable;
            return std::move(*this);
        }

        GraphicsPipeline&& stencil_face(VkStencilFaceFlags flags) {
            _stencil_face_flag = flags;
            return std::move(*this);
        }

        GraphicsPipeline&& stencil_op(VkStencilOp fail, VkStencilOp pass) {
            _stencil_fail_op = fail;
            _stencil_pass_op = pass;
            return std::move(*this);
        }

        GraphicsPipeline&& stencil_compare_mask(uint32_t mask) {
            _stencil_compare_mask = mask;
            return std::move(*this);
        }

        GraphicsPipeline&& stencil_write_mask(uint32_t mask) {
            _stencil_write_mask = mask;
            return std::move(*this);
        }

        GraphicsPipeline&& depth_test(bool enable) {
            _depth_test = enable;
            return std::move(*this);
        }

        GraphicsPipeline&& depth_write(bool enable) {
            _depth_write = enable;
            return std::move(*this);
        }

        GraphicsPipeline&& depth_compare_op(CompareOp op) {
            _depth_compare_op = op;
            return std::move(*this);
        }

        GraphicsPipeline&& depth_bias(VkDepthBiasInfoEXT bias) {
            _depth_bias = bias;
            return std::move(*this);
        }

        GraphicsPipeline&& disable_depth_bias() {
            _depth_bias = std::nullopt;
            return std::move(*this);
        }
    };
}

namespace engine::rendering {
    void begin(const RenderPass& render_pass);
    void end();
}
