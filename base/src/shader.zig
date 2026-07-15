const vk = @import("vulkan");
const Ctx = @import("root.zig").Ctx;

pub const ShaderModule = struct {
    handle: vk.ShaderModule,

    pub fn init(ctx: *const Ctx, spv: []const u32) !ShaderModule {
        const handle = try ctx.graphics.dev.createShaderModule(&.{
            .flags = .{},
            .code_size = spv.len * @sizeOf(u32),
            .p_code = spv.ptr,
        }, null);

        return .{
            .handle = handle,
        };
    }

    pub fn deinit(self: ShaderModule, ctx: *const Ctx) void {
        ctx.graphics.dev.destroyShaderModule(self.handle, null);
    }

    pub fn stageInfo(self: ShaderModule, stage: vk.ShaderStageFlags) vk.PipelineShaderStageCreateInfo {
        return .{
            .stage = stage,
            .module = self.handle,
            .p_name = "main",
            .p_specialization_info = null,
        };
    }
};
