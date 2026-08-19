const std = @import("std");
const vk = @import("vulkan");
const root = @import("root.zig");
const Ctx = root.Ctx;

const Self = @This();

handle: vk.Sampler = .null_handle,

pub const null_handle: Self = .{
    .handle = .null_handle,
};

pub const Description = struct {
    mag_filter: vk.Filter = .linear,
    min_filter: vk.Filter = .linear,
    mipmap_mode: vk.SamplerMipmapMode = .linear,
    mip_lod_bias: f32 = 0,
    address_mode_u: vk.SamplerAddressMode = .repeat,
    address_mode_v: vk.SamplerAddressMode = .repeat,
    address_mode_w: vk.SamplerAddressMode = .repeat,
    anisotropy_enable: vk.Bool32 = .false,
    max_anisotropy: f32 = 0,
    min_lod: f32 = 0,
    max_lod: f32 = vk.LOD_CLAMP_NONE,
    compare_enable: vk.Bool32 = .false,
    compare_op: vk.CompareOp = .never,
    border_color: vk.BorderColor = .float_transparent_black,
    unnormalized_coordinates: vk.Bool32 = .false,
};

pub fn init(ctx: Ctx.Query(&.{ .device }), desc: Description) !Self {
    const handle = try ctx.view.device.createSampler(&.{
        .mag_filter = desc.mag_filter,
        .min_filter = desc.min_filter,
        .mipmap_mode = desc.mipmap_mode,
        .mip_lod_bias = desc.mip_lod_bias,
        .address_mode_u = desc.address_mode_u,
        .address_mode_v = desc.address_mode_v,
        .address_mode_w = desc.address_mode_w,
        .anisotropy_enable = desc.anisotropy_enable,
        .max_anisotropy = desc.max_anisotropy,
        .compare_enable = desc.compare_enable,
        .compare_op = desc.compare_op,
        .min_lod = desc.min_lod,
        .max_lod = desc.max_lod,
        .border_color = desc.border_color,
        .unnormalized_coordinates = desc.unnormalized_coordinates,
    }, null);

    return .{ .handle = handle };
}

pub fn deinit(self: *Self, ctx: Ctx.Query(&.{ .destroy_queue })) void {
    if (self.handle != .null_handle) {
        ctx.view.destroy_queue.enqueueSampler(self.handle) catch @panic("OOM");
        self.handle = .null_handle;
    }
}

pub fn deinitNow(self: *Self, ctx: Ctx.Query(&.{ .device })) void {
    if (self.handle != .null_handle) {
        ctx.view.device.destroySampler(self.handle, null);
        self.handle = .null_handle;
    }
}
