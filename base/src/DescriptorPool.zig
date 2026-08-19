const std = @import("std");
const zprobe = @import("zprobe");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;

const Buffer = @import("Buffer.zig");
const root = @import("root.zig");
const Ctx = root.Ctx;

const Allocator = std.mem.Allocator;

const Self = @This();

pool: vk.DescriptorPool = .null_handle,
sets: []vk.DescriptorSet = &[0]vk.DescriptorSet{},
set_count: u32 = 0,

ubo_buffer: Buffer = .{},
ubo_offset: u64 = 0,

write_id: u64 = std.math.maxInt(u64),
write_buffer_infos: std.ArrayList(vk.DescriptorBufferInfo) = .empty,
write_image_infos: std.ArrayList(vk.DescriptorImageInfo) = .empty,
write_queue: std.ArrayList(vk.WriteDescriptorSet) = .empty,
buffer_write_indices: std.ArrayList(u32) = .empty,
image_write_indices: std.ArrayList(u32) = .empty,

// Raw vk descriptor sets (e.g. TexturePool's own set) registered for graph binding.
external_sets: std.ArrayList(vk.DescriptorSet) = .empty,

const max_sets: u32 = 500;
const ubo_size: vk.DeviceSize = 16000;

pub fn registerExternalSet(self: *Self, alloc: Allocator, set: vk.DescriptorSet) !u64 {
    try self.external_sets.append(alloc, set);
    return max_sets + @as(u64, @intCast(self.external_sets.items.len - 1));
}

pub fn init(
    ctx: Ctx.Query(&.{ .device, .vma_allocator, .graphics_family, .transport_family }),
    alloc: Allocator,
) !Self {
    var dp: Self = .{};

    const pool_sizes = [_]vk.DescriptorPoolSize{
        .{ .type = .combined_image_sampler, .descriptor_count = max_sets },
        .{ .type = .uniform_buffer, .descriptor_count = max_sets },
        .{ .type = .storage_buffer, .descriptor_count = max_sets },
        .{ .type = .storage_image, .descriptor_count = max_sets },
    };

    dp.pool = try ctx.view.device.createDescriptorPool(&.{
        .flags = .{ .update_after_bind_bit = true },
        .max_sets = max_sets,
        .pool_size_count = @intCast(pool_sizes.len),
        .p_pool_sizes = &pool_sizes,
    }, null);

    dp.sets = try alloc.alloc(vk.DescriptorSet, max_sets);
    @memset(dp.sets, .null_handle);

    dp.ubo_buffer = try Buffer.init(
        .from(ctx),
        .graphics,
        "DescriptorPool UBO",
        ubo_size,
        .{ .uniform_buffer_bit = true },
        .cpu_to_gpu_staging,
    );

    dp.write_buffer_infos = try std.ArrayListUnmanaged(vk.DescriptorBufferInfo).initCapacity(alloc, 64);
    dp.write_image_infos = try std.ArrayListUnmanaged(vk.DescriptorImageInfo).initCapacity(alloc, 64);
    dp.write_queue = try std.ArrayListUnmanaged(vk.WriteDescriptorSet).initCapacity(alloc, 64);
    dp.buffer_write_indices = try std.ArrayListUnmanaged(u32).initCapacity(alloc, 64);
    dp.image_write_indices = try std.ArrayListUnmanaged(u32).initCapacity(alloc, 64);

    return dp;
}

pub fn deinit(
    self: *Self,
    ctx: Ctx.Query(&.{ .device, .vma_allocator }),
    alloc: Allocator,
) void {
    ctx.view.device.destroyDescriptorPool(self.pool, null);

    self.ubo_buffer.deinitNow(.from(ctx));

    alloc.free(self.sets);
    self.external_sets.deinit(alloc);

    self.write_buffer_infos.deinit(alloc);
    self.write_image_infos.deinit(alloc);
    self.write_queue.deinit(alloc);
    self.buffer_write_indices.deinit(alloc);
    self.image_write_indices.deinit(alloc);
}

pub fn newSet(self: *Self, ctx: Ctx.Query(&.{ .device }), layout: vk.DescriptorSetLayout) !u64 {
    const id = self.set_count;
    if (id >= max_sets) return error.DescriptorPoolFull;
    self.set_count = id + 1;

    var result: [1]vk.DescriptorSet = undefined;
    try ctx.view.device.allocateDescriptorSets(&.{
        .descriptor_pool = self.pool,
        .descriptor_set_count = 1,
        .p_set_layouts = @ptrCast(&layout),
    }, &result);
    self.sets[id] = result[0];
    return id;
}

pub fn bindSet(
    self: *const Self,
    cmd_buf: vk.CommandBuffer,
    ctx: Ctx.Query(&.{ .device }),
    id: u64,
    bind_point: vk.PipelineBindPoint,
    layout: vk.PipelineLayout,
    set: u32,
) void {
    if (id >= max_sets) {
        const ext_ix: usize = @intCast(id - max_sets);
        if (ext_ix >= self.external_sets.items.len) return;
        ctx.view.device.cmdBindDescriptorSets(cmd_buf, bind_point, layout, set, &.{self.external_sets.items[ext_ix]}, null);
    } else {
        ctx.view.device.cmdBindDescriptorSets(cmd_buf, bind_point, layout, set, &.{self.sets[id]}, null);
    }
}

pub fn updateSet(
    self: *Self,
    dev: *vk.DeviceProxy,
    id: u64,
    writes: []const vk.WriteDescriptorSet,
) void {
    var fixed: [64]vk.WriteDescriptorSet = undefined;
    const fixed_writes = fixed[0..writes.len];
    for (writes, 0..) |w, i| {
        fixed_writes[i] = w;
        fixed_writes[i].dst_set = self.sets[id];
    }
    dev.updateDescriptorSets(fixed_writes, null);
}

pub fn beginUpdate(self: *Self, id: u64) void {
    self.write_id = id;
    self.write_buffer_infos.clearRetainingCapacity();
    self.write_image_infos.clearRetainingCapacity();
    self.write_queue.clearRetainingCapacity();
    self.buffer_write_indices.clearRetainingCapacity();
    self.image_write_indices.clearRetainingCapacity();
}

pub fn endUpdate(self: *Self, ctx: Ctx.Query(&.{ .device })) void {
    for (self.write_queue.items, 0..) |*write, i| {
        write.dst_set = self.sets[@intCast(self.write_id)];
        switch (write.descriptor_type) {
            .uniform_buffer, .storage_buffer => {
                const idx = self.buffer_write_indices.items[i];
                write.p_buffer_info = self.write_buffer_infos.items[idx..][0..1].ptr;
            },
            .combined_image_sampler, .storage_image => {
                const idx = self.image_write_indices.items[i];
                write.p_image_info = self.write_image_infos.items[idx..][0..1].ptr;
            },
            else => {},
        }
    }
    ctx.view.device.updateDescriptorSets(self.write_queue.items, null);
    self.write_id = std.math.maxInt(u64);
}

pub fn updateUbo(self: *Self, alloc: std.mem.Allocator, binding: u32, data: []const u8) !void {
    if (self.ubo_offset + data.len > ubo_size) {
        zprobe.event(.warn, "descriptor pool ubo full", .{ .binding = binding, .len = data.len });
        return;
    }

    @memcpy(self.ubo_buffer.mapped.?[self.ubo_offset..][0..data.len], data);

    const buf_info = vk.DescriptorBufferInfo{
        .buffer = self.ubo_buffer.handle,
        .offset = self.ubo_offset,
        .range = @intCast(data.len),
    };
    self.ubo_offset += data.len;

    try self.write_buffer_infos.append(alloc, buf_info);

    const index = @as(u32, @intCast(self.write_buffer_infos.items.len - 1));
    try self.buffer_write_indices.append(alloc, index);

    try self.write_queue.append(alloc, .{
        .dst_set = .null_handle,
        .dst_binding = binding,
        .dst_array_element = 0,
        .descriptor_count = 1,
        .descriptor_type = .uniform_buffer,
        .p_buffer_info = undefined,
        .p_image_info = undefined,
        .p_texel_buffer_view = undefined,
    });
}

pub fn updateSampledImage(
    self: *Self,
    alloc: std.mem.Allocator,
    binding: u32,
    layout: vk.ImageLayout,
    view: vk.ImageView,
    sampler: vk.Sampler,
) !void {
    try self.write_image_infos.append(alloc, .{
        .image_layout = layout,
        .image_view = view,
        .sampler = sampler,
    });

    const index = @as(u32, @intCast(self.write_image_infos.items.len - 1));
    try self.image_write_indices.append(alloc, index);

    try self.write_queue.append(alloc, .{
        .dst_set = .null_handle,
        .dst_binding = binding,
        .dst_array_element = 0,
        .descriptor_count = 1,
        .descriptor_type = .combined_image_sampler,
        .p_buffer_info = undefined,
        .p_image_info = undefined,
        .p_texel_buffer_view = undefined,
    });
}

pub fn updateStorageImage(
    self: *Self,
    alloc: std.mem.Allocator,
    binding: u32,
    layout: vk.ImageLayout,
    view: vk.ImageView,
) !void {
    try self.write_image_infos.append(alloc, .{
        .image_layout = layout,
        .image_view = view,
        .sampler = .null_handle,
    });

    const index = @as(u32, @intCast(self.write_image_infos.items.len - 1));
    try self.image_write_indices.append(alloc, index);

    try self.write_queue.append(alloc, .{
        .dst_set = .null_handle,
        .dst_binding = binding,
        .dst_array_element = 0,
        .descriptor_count = 1,
        .descriptor_type = .storage_image,
        .p_buffer_info = undefined,
        .p_image_info = undefined,
        .p_texel_buffer_view = undefined,
    });
}

pub fn clear(self: *Self, ctx: Ctx.Query(&.{ .device })) !void {
    try ctx.view.device.resetDescriptorPool(self.pool, .{});
    self.set_count = 0;
    @memset(self.sets, .null_handle);
    self.ubo_offset = 0;
}
