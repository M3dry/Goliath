#include "goliath/visbuffer.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"
#include "goliath/rendering.hpp"
#include "goliath/texture.hpp"

#include <cstdlib>
#include <vulkan/vulkan_core.h>

namespace engine::visbuffer {
    GPUImage* vis_buffers;
    VkImageView* vis_buffer_views;
    VkImageLayout current_layout;

    void init(VkImageMemoryBarrier2* barriers) {
        vis_buffers = (GPUImage*)malloc(sizeof(GPUImage) * frames_in_flight);
        vis_buffer_views = (VkImageView*)malloc(sizeof(VkImageView) * frames_in_flight);

        for (std::size_t i = 0; i < frames_in_flight; i++) {
            auto [img, barrier] =
                GPUImage::upload(GPUImageInfo{}
                                     .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                                     .extent(VkExtent3D{
                                         .width = swapchain_extent.width,
                                         .height = swapchain_extent.height,
                                         .depth = 1,
                                     })
                                     .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                                     .new_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                                     .format(VK_FORMAT_R32G32B32A32_UINT));
            barriers[i] = barrier;
            vis_buffers[i] = img;

            vis_buffer_views[i] = GPUImageView{img}.aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).create();
        }

        current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    void destroy() {
        for (std::size_t i = 0; i < frames_in_flight; i++) {
            vis_buffers[i].destroy();
            vkDestroyImageView(device, vis_buffer_views[i], nullptr);
        }

        free(vis_buffers);
    }

    void transition_to(VkImageMemoryBarrier2* _barrier, VkImageLayout layout) {
        barrier(_barrier);
        _barrier->newLayout = layout;
        current_layout = layout;
    }

    void barrier(VkImageMemoryBarrier2* barrier) {
        barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier->pNext = nullptr;
        barrier->oldLayout = current_layout;
        barrier->newLayout = current_layout;
        barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier->image = vis_buffers[get_current_frame()].image;
        barrier->subresourceRange = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
    }

    VkImageView get_view() {
        return vis_buffer_views[get_current_frame()];
    }

    void bind(uint32_t binding) {
        get_frame_descriptor_pool().update_storage_image(2, VK_IMAGE_LAYOUT_GENERAL, vis_buffer_views[get_current_frame()]);
    }

    RenderingAttachement attach() {
        return RenderingAttachement{}
            .set_image(get_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            .set_load_op(LoadOp::Load)
            .set_store_op(StoreOp::Store);
    }
}
