const std = @import("std");
const vk = @import("vulkan");

const root = @import("root.zig");
const Ctx = root.Ctx;

const Self = @This();

pool: vk.DescriptorPool = .null_handle,
set_layout: vk.DescriptorSetLayout = .null_handle,
set: vk.DescriptorSet = .null_handle,
capacity: u32 = 0,

pub fn init(ctx: Ctx.Query(&.{ .device }), capacity_: u32) !Self {
    var tp: Self = .{};
    tp.capacity = capacity_;

    tp.pool = try ctx.view.device.createDescriptorPool(&.{
        .flags = .{ .update_after_bind_bit = true },
        .max_sets = 1,
        .pool_size_count = 1,
        .p_pool_sizes = @ptrCast(&vk.DescriptorPoolSize{
            .type = .combined_image_sampler,
            .descriptor_count = capacity_,
        }),
    }, null);

    const binding = vk.DescriptorSetLayoutBinding{
        .binding = 0,
        .descriptor_type = .combined_image_sampler,
        .descriptor_count = capacity_,
        .stage_flags = .{ .vertex_bit = true, .fragment_bit = true, .geometry_bit = true, .tessellation_control_bit = true, .tessellation_evaluation_bit = true, .compute_bit = true },
    };

    const binding_flags = vk.DescriptorBindingFlags{
        .partially_bound_bit = true,
        .update_after_bind_bit = true,
        .variable_descriptor_count_bit = true,
    };

    const binding_flags_info = vk.DescriptorSetLayoutBindingFlagsCreateInfo{
        .binding_count = 1,
        .p_binding_flags = @ptrCast(&binding_flags),
    };

    tp.set_layout = try ctx.view.device.createDescriptorSetLayout(&.{
        .p_next = &binding_flags_info,
        .flags = .{ .update_after_bind_pool_bit = true },
        .binding_count = 1,
        .p_bindings = @ptrCast(&binding),
    }, null);

    const count_info = vk.DescriptorSetVariableDescriptorCountAllocateInfo{
        .descriptor_set_count = 1,
        .p_descriptor_counts = @ptrCast(&tp.capacity),
    };

    try ctx.view.device.allocateDescriptorSets(&.{
        .p_next = &count_info,
        .descriptor_pool = tp.pool,
        .descriptor_set_count = 1,
        .p_set_layouts = @ptrCast(&tp.set_layout),
    }, (&tp.set)[0..1]);

    return tp;
}

pub fn deinit(self: *Self, ctx: Ctx.Query(&.{ .destroy_queue })) void {
    if (self.set_layout != .null_handle) {
        ctx.view.destroy_queue.enqueueDescriptorSetLayout(self.set_layout) catch @panic("OOM");
        self.set_layout = .null_handle;
    }
    if (self.pool != .null_handle) {
        ctx.view.destroy_queue.enqueueDescriptorPool(self.pool) catch @panic("OOM");
        self.pool = .null_handle;
    }

    self.capacity = 0;
}

pub fn deinitNow(self: *Self, ctx: Ctx.Query(&.{ .device })) void {
    if (self.set_layout != .null_handle) {
        ctx.view.device.destroyDescriptorSetLayout(self.set_layout, null);
        self.set_layout = .null_handle;
    }
    if (self.pool != .null_handle) {
        ctx.view.device.destroyDescriptorPool(self.pool, null);
        self.pool = .null_handle;
    }

    self.capacity = 0;
}

pub fn update(
    self: *Self,
    ctx: Ctx.Query(&.{ .device }),
    index: u32,
    view: vk.ImageView,
    layout: vk.ImageLayout,
    sampler: vk.Sampler,
) void {
    const image_info = [1]vk.DescriptorImageInfo{.{
        .image_layout = layout,
        .image_view = view,
        .sampler = sampler,
    }};

    var dummy_buf: [1]vk.DescriptorBufferInfo = .{.{ .buffer = .null_handle, .offset = 0, .range = 0 }};
    var dummy_buf_view: [1]vk.BufferView = std.mem.zeroes([1]vk.BufferView);

    ctx.view.device.updateDescriptorSets(&.{
        .{
            .dst_set = self.set,
            .dst_binding = 0,
            .dst_array_element = index,
            .descriptor_count = 1,
            .descriptor_type = .combined_image_sampler,
            .p_image_info = &image_info,
            .p_buffer_info = &dummy_buf,
            .p_texel_buffer_view = &dummy_buf_view,
        },
    }, null);
}

pub fn bind(
    self: *const Self,
    cmd_buf: vk.CommandBuffer,
    ctx: Ctx.Query(&.{ .device }),
    bind_point: vk.PipelineBindPoint,
    layout: vk.PipelineLayout,
    set: u32,
) void {
    ctx.view.device.cmdBindDescriptorSets(cmd_buf, bind_point, layout, set, &.{self.set}, null);
}
