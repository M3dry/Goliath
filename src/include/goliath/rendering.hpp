#pragma once

#include "goliath/descriptor_pool.hpp"
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
#include <vulkan/vulkan_core.h>

namespace engine {
    enum struct LoadOp {
        Load = VK_ATTACHMENT_LOAD_OP_LOAD,
        Clear = VK_ATTACHMENT_LOAD_OP_CLEAR,
        DontCare = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };

    enum struct StoreOp {
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

    struct Shader {
        using Source = std::variant<const char*, std::span<uint8_t>>;

        VkShaderStageFlagBits _stage;
        VkShaderStageFlagBits _next_stage = (VkShaderStageFlagBits)0;
        const char* _main = "main";

        Shader() {};

        Shader&& stage(VkShaderStageFlagBits val) {
            _stage = val;
            return std::move(*this);
        }

        Shader&& next_stage(VkShaderStageFlagBits val) {
            _next_stage = val;
            return std::move(*this);
        }

        Shader&& main(const char* name) {
            _main = name;
            return std::move(*this);
        }

        VkShaderEXT create(Source source, uint32_t push_constant_size,
                           std::span<VkDescriptorSetLayout> set_layouts = {(VkDescriptorSetLayout*)nullptr, 0});
    };

    void destroy_shader(VkShaderEXT shader);

    enum struct CullMode {
        None = VK_CULL_MODE_NONE,
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

    struct Pipeline {
        struct DrawParams {
            void* push_constant = nullptr;
            std::array<uint64_t, 3> descriptor_indexes = {descriptor::null_set, descriptor::null_set, descriptor::null_set};
            uint32_t vertex_count;
            uint32_t instance_count = 1;
            uint32_t first_vertex_id = 0;
            uint32_t first_instance_id = 0;
        };

        VkShaderEXT _vertex;
        VkShaderEXT _fragment;

        VkDescriptorSetLayout _set_layout[4];
        uint32_t _push_constant_size;

        bool _depth_test;
        bool _depth_write;
        std::optional<VkDepthBiasInfoEXT > _depth_bias;
        VkCompareOp _depth_cmp_op;
        bool _stencil_test;
        CullMode _cull_mode;
        FillMode _fill_mode;
        SampleCount _sample_count;
        Topology _topology;
        FrontFace _front_face;
        VkViewport _viewport;
        VkRect2D _scissor;
        VkColorComponentFlags _color_components = 0;

        VkPipelineLayout _pipeline_layout;

        Pipeline();

        Pipeline&& vertex(VkShaderEXT vert) {
            _vertex = vert;
            return std::move(*this);
        }

        Pipeline&& fragment(VkShaderEXT frag) {
            _fragment = frag;
            return std::move(*this);
        }

        Pipeline&& vertex(Shader vert, Shader::Source vert_source);
        Pipeline&& fragment(Shader frag, Shader::Source frag_source);

        Pipeline&& descriptor(uint32_t index, VkDescriptorSetLayout descriptor_layout) {
            _set_layout[index] = descriptor_layout;
            return std::move(*this);
        }

        Pipeline&& clear_descriptor(uint32_t index);

        Pipeline&& push_constant_size(uint32_t size) {
            _push_constant_size = size;
            return std::move(*this);
        }

        Pipeline&& depth_test(bool enable) {
            _depth_test = enable;
            return std::move(*this);
        }

        Pipeline&& depth_write(bool enable) {
            _depth_write = enable;
            return std::move(*this);
        }

        Pipeline&& depth_bias(float clamp, float const_factor, float slope_factor) {
            _depth_bias.emplace(VkDepthBiasInfoEXT{
                .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_INFO_EXT,
                .pNext = nullptr,
                .depthBiasConstantFactor = const_factor,
                .depthBiasClamp = clamp,
                .depthBiasSlopeFactor = slope_factor,
            });
            return std::move(*this);
        }

        Pipeline&& clear_depth_bias() {
            _depth_bias = std::nullopt;
            return std::move(*this);
        }


        Pipeline&& depth_cmp_op(CompareOp op) {
            _depth_cmp_op = static_cast<VkCompareOp>(op);
            return std::move(*this);
        }

        Pipeline&& stencil_test(bool enable) {
            _stencil_test = enable;
            return std::move(*this);
        }

        Pipeline&& cull_mode(CullMode mode) {
            _cull_mode = mode;
            return std::move(*this);
        }

        Pipeline&& fill_mode(FillMode mode) {
            _fill_mode = mode;
            return std::move(*this);
        }

        Pipeline&& sample_count(SampleCount count) {
            _sample_count = count;
            return std::move(*this);
        }

        Pipeline&& topology(Topology top) {
            _topology = top;
            return std::move(*this);
        }

        Pipeline&& front_face(FrontFace face) {
            _front_face = face;
            return std::move(*this);
        }

        Pipeline&& viewport(VkViewport view) {
            _viewport = view;
            return std::move(*this);
        }

        Pipeline&& scissor(VkRect2D rect) {
            _scissor = rect;
            return std::move(*this);
        }

        Pipeline&& color_components(VkColorComponentFlags components) {
            _color_components = components;
            return std::move(*this);
        }

        Pipeline&& update_layout();

        void destroy(std::array<bool, 3> sets_to_destroy);

        void draw(const DrawParams& params);
    };

    struct PipelineBuilder {
        VkShaderModule _vertex;
        VkShaderModule _fragment;

        VkDescriptorSetLayout _set_layout[4];
        uint32_t _push_constant_size;

        FillMode _fill_mode;
        // Topology _topology;
        // bool _primitive_restart_enable;

        // VkViewport _viewport;
        //
        // CullMode _cull_mode;
        // FrontFace _front_face;
        // float _line_width;

        // bool _depth_test;
        // bool _depth_write;
        // CompareOp _depth_cmp_op;
    };

    struct Pipeline2 {
        VkPipelineLayout _pipeline_layout;
        VkPipeline _pipeline;

        Pipeline2(const PipelineBuilder& builder);
        // struct DrawParams {
        //     void* push_constant = nullptr;
        //     std::array<uint64_t, 3> descriptor_indexes = {descriptor::null_set, descriptor::null_set, descriptor::null_set};
        //     uint32_t vertex_count;
        //     uint32_t instance_count = 1;
        //     uint32_t first_vertex_id = 0;
        //     uint32_t first_instance_id = 0;
        // };
        //
        // VkShaderEXT _vertex;
        // VkShaderEXT _fragment;
        //
        // VkDescriptorSetLayout _set_layout[4];
        // uint32_t _push_constant_size;
        //
        // bool _depth_test;
        // bool _depth_write;
        // std::optional<VkDepthBiasInfoEXT > _depth_bias;
        // VkCompareOp _depth_cmp_op;
        // bool _stencil_test;
        // CullMode _cull_mode;
        // FillMode _fill_mode;
        // SampleCount _sample_count;
        // Topology _topology;
        // FrontFace _front_face;
        // VkViewport _viewport;
        // VkRect2D _scissor;
        // VkColorComponentFlags _color_components = 0;
        //
        // VkPipelineLayout _pipeline_layout;
    };
}

namespace engine::rendering {
    void begin(const RenderPass& render_pass);
    void end();
}
