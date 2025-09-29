#pragma once

#include <cstring>
#include <vector>
#include <volk.h>

#include <glm/vec4.hpp>
#include <glm/gtc/type_ptr.hpp>
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

      // private:
        std::vector<VkRenderingAttachmentInfo> color_atts;
        VkRenderingAttachmentInfo depth_att;
        VkRenderingAttachmentInfo stencil_att;

        void update_color_atts() {
            _info.colorAttachmentCount = (uint32_t)color_atts.size();
            _info.pColorAttachments = color_atts.data();
        }
    };

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

    class Pipeline {
        VkShaderEXT vertex{};
        VkShaderEXT fragment{};

        VkDescriptorSetLayout set_layout[3];
        uint32_t push_constant_size;

        bool depth_test;
        bool depth_write;
        bool depth_bias;
        bool stencil_test;
        CullMode cull_mode;
        FillMode fill_mode;
        SampleCount sample_count;
        Topology topology;

        Pipeline(VkShaderEXT vertex, VkShaderEXT fragment);

        void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex_id = 0,
                  uint32_t first_instance_id = 0);
    };
}

namespace engine::rendering {
    void begin(const RenderPass& render_pass);
    void end();
}
