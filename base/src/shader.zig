const vk = @import("vulkan");
const Ctx = @import("root.zig").Ctx;

pub const ShaderModule = struct {
    handle: vk.ShaderModule,
    stage: vk.ShaderStageFlags,

    pub fn init(ctx: *const Ctx, spv: []const u32, stage: vk.ShaderStageFlags) !ShaderModule {
        const handle = try ctx.graphics.dev.createShaderModule(&.{
            .flags = .{},
            .code_size = spv.len * @sizeOf(u32),
            .p_code = spv.ptr,
        }, null);

        return .{
            .handle = handle,
            .stage = stage
        };
    }

    pub fn deinit(self: ShaderModule, dev: vk.Device) void {
        dev.destroyShaderModule(self.handle, null);
    }

    pub fn stageInfo(self: ShaderModule) vk.PipelineShaderStageCreateInfo {
        return .{
            .stage = self.stage,
            .module = self.handle,
            .p_name = "main",
            .p_specialization_info = null,
        };
    }
};
