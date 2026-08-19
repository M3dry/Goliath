const vk = @import("vulkan");
const root = @import("root.zig");
const Ctx = root.Ctx;

const Self = @This();

handle: vk.ShaderModule,

pub fn init(ctx: Ctx.Query(&.{ .device }), spv: []const u32) !Self {
    const handle = try ctx.view.device.createShaderModule(&.{
        .flags = .{},
        .code_size = spv.len * @sizeOf(u32),
        .p_code = spv.ptr,
    }, null);

    return .{
        .handle = handle,
    };
}

pub fn deinit(self: Self, ctx: Ctx.Query(&.{ .device })) void {
    ctx.view.device.destroyShaderModule(self.handle, null);
}

pub fn stageInfo(self: Self, stage: vk.ShaderStageFlags, entry_point: []const u8) vk.PipelineShaderStageCreateInfo {
    return .{
        .stage = stage,
        .module = self.handle,
        .p_name = @ptrCast(entry_point.ptr),
        .p_specialization_info = null,
    };
}
