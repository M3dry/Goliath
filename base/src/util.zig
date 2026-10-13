const vk = @import("vulkan");
const zm = @import("zmath");

pub const SmallBuffer = @import("util/small_buffer.zig").SmallBuffer;
pub const SmallBitset = @import("util/small_bitset.zig").SmallBitset;
pub const RingBuffer = @import("util/ring_buffer.zig").RingBuffer;
pub const const_view = @import("util/const_view.zig");
pub const ConstView = const_view.ConstView;
pub const toConstView = const_view.toConstView;

pub const AABB = struct {
    min: zm.Vec,
    max: zm.Vec,
};

pub const TRS = struct {
    translation: [3]f32,
    rotation: [4]f32,
    scale: [3]f32,

    pub fn toMat(self: TRS) zm.Mat {
        return zm.mul(zm.scaling(self.scale[0], self.scale[1], self.scale[2]), zm.mul(zm.matFromQuat(zm.f32x4(self.rotation[0], self.rotation[1], self.rotation[2], self.rotation[3])), zm.translation(self.translation[0], self.translation[1], self.translation[2])));
    }
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
