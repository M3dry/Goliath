const vk = @import("vulkan");
const zm = @import("zmath");

pub const SmallBuffer = @import("util/small_buffer.zig").SmallBuffer;
pub const SmallBitset = @import("util/small_bitset.zig").SmallBitset;
pub const RingBuffer = @import("util/ring_buffer.zig").RingBuffer;

pub const AABB = struct {
    min: zm.Vec,
    max: zm.Vec,
};

pub fn fullRange(aspect: vk.ImageAspectFlags) vk.ImageSubresourceRange {
    return .{
        .aspect_mask = aspect,
        .base_mip_level = 0,
        .level_count = vk.REMAINING_MIP_LEVELS,
        .base_array_layer = 0,
        .layer_count = vk.REMAINING_ARRAY_LAYERS,
    };
}

test {
    @import("std").testing.refAllDecls(@This());
}
