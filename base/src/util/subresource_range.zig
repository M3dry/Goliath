const vk = @import("vulkan");

pub fn fullRange(aspect: vk.ImageAspectFlags) vk.ImageSubresourceRange {
    return .{
        .aspect_mask = aspect,
        .base_mip_level = 0,
        .level_count = vk.REMAINING_MIP_LEVELS,
        .base_array_layer = 0,
        .layer_count = vk.REMAINING_ARRAY_LAYERS,
    };
}
