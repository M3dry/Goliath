const vk = @import("vulkan");
const Ctx = @import("root.zig").Ctx;

const Self = @This();

handle: vk.ShaderModule,

pub fn init(ctx: *const Ctx, spv: []const u32) !Self {
    const handle = try ctx.graphics.dev.createShaderModule(&.{
        .flags = .{},
        .code_size = spv.len * @sizeOf(u32),
        .p_code = spv.ptr,
    }, null);

    return .{
        .handle = handle,
    };
}

pub fn deinit(self: Self, ctx: *const Ctx) void {
    ctx.graphics.dev.destroyShaderModule(self.handle, null);
}

pub fn stageInfo(self: Self, stage: vk.ShaderStageFlags, entry_point: []const u8) vk.PipelineShaderStageCreateInfo {
    return .{
        .stage = stage,
        .module = self.handle,
        .p_name = @ptrCast(entry_point.ptr),
        .p_specialization_info = null,
    };
}
