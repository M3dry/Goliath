const std = @import("std");
const vk = @import("vulkan");
const ShaderModule = @import("Shader.zig");

const Ctx = @import("root.zig").Ctx;
const GraphicsCtx = @import("GraphicsCtx.zig");

const Self = @This();

pub const DispatchParams = struct {
    push_constant: ?[]const u8 = null,
    group_count_x: u32,
    group_count_y: u32,
    group_count_z: u32,
};

pub const DispatchIndirectParams = struct {
    push_constant: ?[]const u8 = null,
    buffer: vk.Buffer,
    offset: u64 = 0,
};

handle: vk.Pipeline,
layout: vk.PipelineLayout,
push_constant_size: u32,

pub const Description = struct {
    shader: ShaderModule,
    set_layouts: []const vk.DescriptorSetLayout = &.{},
    push_constant_size: u32 = 0,
};

pub fn init(ctx: *const Ctx, desc: Description) !Self {
    const dev = ctx.graphics.dev;

    const stage = desc.shader.stageInfo(.{ .compute_bit = true });

    var push_constant_range: vk.PushConstantRange = undefined;
    const pc_range_count: u32 = if (desc.push_constant_size > 0) 1 else 0;
    if (desc.push_constant_size > 0) {
        push_constant_range = .{
            .stage_flags = .{ .compute_bit = true },
            .offset = 0,
            .size = desc.push_constant_size,
        };
    }

    const pipeline_layout = try dev.createPipelineLayout(&.{
        .set_layout_count = @intCast(desc.set_layouts.len),
        .p_set_layouts = desc.set_layouts.ptr,
        .push_constant_range_count = pc_range_count,
        .p_push_constant_ranges = if (desc.push_constant_size > 0) (&push_constant_range)[0..1] else undefined,
    }, null);
    errdefer dev.destroyPipelineLayout(pipeline_layout, null);

    var pipeline: vk.Pipeline = .null_handle;
    _ = try dev.createComputePipelines(.null_handle, (&vk.ComputePipelineCreateInfo{
        .stage = stage,
        .layout = pipeline_layout,
        .base_pipeline_index = -1,
    })[0..1], null, (&pipeline)[0..1]);

    return Self{
        .handle = pipeline,
        .layout = pipeline_layout,
        .push_constant_size = desc.push_constant_size,
    };
}

pub fn deinit(self: *Self, gc: *const GraphicsCtx) void {
    if (self.handle != .null_handle) {
        gc.dev.destroyPipeline(self.handle, null);
        self.handle = .null_handle;
    }
    if (self.layout != .null_handle) {
        gc.dev.destroyPipelineLayout(self.layout, null);
        self.layout = .null_handle;
    }
}

pub fn bind(self: *const Self, gc: *const GraphicsCtx, cmd_buf: vk.CommandBuffer) void {
    gc.dev.cmdBindPipeline(cmd_buf, .compute, self.handle);
}

pub fn dispatch(self: *const Self, gc: *const GraphicsCtx, cmd_buf: vk.CommandBuffer, params: DispatchParams) void {
    if (params.push_constant) |pc| {
        gc.dev.cmdPushConstants(cmd_buf, self.layout, .{ .compute_bit = true }, 0, @intCast(pc.len), pc.ptr);
    }

    gc.dev.cmdDispatch(cmd_buf, params.group_count_x, params.group_count_y, params.group_count_z);
}

pub fn dispatchIndirect(self: *const Self, gc: *const GraphicsCtx, cmd_buf: vk.CommandBuffer, params: DispatchIndirectParams) void {
    if (params.push_constant) |pc| {
        gc.dev.cmdPushConstants(cmd_buf, self.layout, .{ .compute_bit = true }, 0, @intCast(pc.len), pc.ptr);
    }

    gc.dev.cmdDispatchIndirect(cmd_buf, params.buffer, params.offset);
}
