#include "goliath/rendering.hpp"
#include "goliath/engine.hpp"

#include <volk.h>

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

namespace engine::rendering {
    void begin(const RenderPass& render_pass) {
        vkCmdBeginRendering(get_cmd_buf(), &render_pass._info);
    }

    void end() {
        vkCmdEndRendering(get_cmd_buf());
    }
}
