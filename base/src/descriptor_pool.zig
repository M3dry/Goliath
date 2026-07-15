const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;
const buf = @import("buffer.zig");

const GraphicsContext = @import("graphics_ctx.zig").GraphicsCtx;
const Buffer = buf.Buffer;

const Allocator = std.mem.Allocator;

pub const DescriptorPool = struct {
    pool: vk.DescriptorPool = .null_handle,
    sets: []vk.DescriptorSet = &[0]vk.DescriptorSet{},
    set_count: u32 = 0,

    ubo_buffer: Buffer = .{},
    ubo_offset: u64 = 0,

    write_id: u64 = std.math.maxInt(u64),
    write_buffer_infos: std.ArrayListUnmanaged(vk.DescriptorBufferInfo) = .{ .items = &.{}, .capacity = 0 },
    write_image_infos: std.ArrayListUnmanaged(vk.DescriptorImageInfo) = .{ .items = &.{}, .capacity = 0 },
    write_queue: std.ArrayListUnmanaged(vk.WriteDescriptorSet) = .{ .items = &.{}, .capacity = 0 },
    buffer_write_indices: std.ArrayListUnmanaged(u32) = .{ .items = &.{}, .capacity = 0 },
    image_write_indices: std.ArrayListUnmanaged(u32) = .{ .items = &.{}, .capacity = 0 },

    const max_sets: u32 = 500;
    const ubo_size: vk.DeviceSize = 16000;

    pub fn init(
        gc: *const GraphicsContext,
        alloc: Allocator,
    ) !DescriptorPool {
        var dp: DescriptorPool = .{};

        const pool_sizes = [_]vk.DescriptorPoolSize{
            .{ .type = .combined_image_sampler, .descriptor_count = max_sets },
            .{ .type = .uniform_buffer, .descriptor_count = max_sets },
            .{ .type = .storage_buffer, .descriptor_count = max_sets },
            .{ .type = .storage_image, .descriptor_count = max_sets },
        };

        dp.pool = try gc.dev.createDescriptorPool(&.{
            .flags = .{ .update_after_bind_bit = true },
            .max_sets = max_sets,
            .pool_size_count = @intCast(pool_sizes.len),
            .p_pool_sizes = &pool_sizes,
        }, null);

        dp.sets = try alloc.alloc(vk.DescriptorSet, max_sets);
        @memset(dp.sets, .null_handle);

        dp.ubo_buffer = try Buffer.init(
            gc,
            .graphics,
            "DescriptorPool UBO",
            ubo_size,
            .{ .uniform_buffer_bit = true },
            true,
        );

        dp.write_buffer_infos = try std.ArrayListUnmanaged(vk.DescriptorBufferInfo).initCapacity(alloc, 64);
        dp.write_image_infos = try std.ArrayListUnmanaged(vk.DescriptorImageInfo).initCapacity(alloc, 64);
        dp.write_queue = try std.ArrayListUnmanaged(vk.WriteDescriptorSet).initCapacity(alloc, 64);
        dp.buffer_write_indices = try std.ArrayListUnmanaged(u32).initCapacity(alloc, 64);
        dp.image_write_indices = try std.ArrayListUnmanaged(u32).initCapacity(alloc, 64);

        return dp;
    }

    pub fn deinit(
        self: *DescriptorPool,
        gc: *const GraphicsContext,
        alloc: Allocator,
    ) void {
        if (self.pool != .null_handle) {
            gc.dev.destroyDescriptorPool(self.pool, null);
        }
        self.ubo_buffer.deinitNow(gc.vma_alloc);
        alloc.free(self.sets);
        self.write_buffer_infos.deinit(alloc);
        self.write_image_infos.deinit(alloc);
        self.write_queue.deinit(alloc);
        self.buffer_write_indices.deinit(alloc);
        self.image_write_indices.deinit(alloc);
    }

    pub fn newSet(self: *DescriptorPool, dev: *vk.DeviceProxy, layout: vk.DescriptorSetLayout) !u64 {
        const id = self.set_count;
        if (id >= max_sets) return error.DescriptorPoolFull;
        self.set_count = id + 1;

        var result: [1]vk.DescriptorSet = undefined;
        try dev.allocateDescriptorSets(&.{
            .descriptor_pool = self.pool,
            .descriptor_set_count = 1,
            .p_set_layouts = @ptrCast(&layout),
        }, &result);
        self.sets[id] = result[0];
        return id;
    }

    pub fn bindSet(
        self: *const DescriptorPool,
        cmd_buf: vk.CommandBuffer,
        dev: *vk.DeviceProxy,
        id: u64,
        bind_point: vk.PipelineBindPoint,
        layout: vk.PipelineLayout,
        set: u32,
    ) void {
        dev.cmdBindDescriptorSets(cmd_buf, bind_point, layout, set, &.{self.sets[id]}, null);
    }

    pub fn updateSet(
        self: *DescriptorPool,
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

    pub fn beginUpdate(self: *DescriptorPool, id: u64) void {
        self.write_id = id;
        self.write_buffer_infos.clearRetainingCapacity();
        self.write_image_infos.clearRetainingCapacity();
        self.write_queue.clearRetainingCapacity();
        self.buffer_write_indices.clearRetainingCapacity();
        self.image_write_indices.clearRetainingCapacity();
    }

    pub fn endUpdate(self: *DescriptorPool, dev: *vk.DeviceProxy) void {
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
        dev.updateDescriptorSets(self.write_queue.items, null);
        self.write_id = std.math.maxInt(u64);
    }

    pub fn updateUbo(self: *DescriptorPool, binding: u32, data: []const u8) void {
        if (self.ubo_offset + data.len > ubo_size) return;

        @memcpy(self.ubo_buffer.mapped.?[self.ubo_offset..][0..data.len], data);

        const buf_info = vk.DescriptorBufferInfo{
            .buffer = self.ubo_buffer.handle,
            .offset = self.ubo_offset,
            .range = @intCast(data.len),
        };
        self.ubo_offset += data.len;

        self.write_buffer_infos.append(std.heap.c_allocator, buf_info) catch return;

        const index = @as(u32, @intCast(self.write_buffer_infos.items.len - 1));
        self.buffer_write_indices.append(std.heap.c_allocator, index) catch return;

        self.write_queue.append(std.heap.c_allocator, .{
            .dst_set = .null_handle,
            .dst_binding = binding,
            .dst_array_element = 0,
            .descriptor_count = 1,
            .descriptor_type = .uniform_buffer,
            .p_buffer_info = undefined,
            .p_image_info = undefined,
            .p_texel_buffer_view = undefined,
        }) catch return;
    }

    pub fn updateSampledImage(
        self: *DescriptorPool,
        binding: u32,
        layout: vk.ImageLayout,
        view: vk.ImageView,
        sampler: vk.Sampler,
    ) void {
        self.write_image_infos.append(std.heap.c_allocator, .{
            .image_layout = layout,
            .image_view = view,
            .sampler = sampler,
        }) catch return;

        const index = @as(u32, @intCast(self.write_image_infos.items.len - 1));
        self.image_write_indices.append(std.heap.c_allocator, index) catch return;

        self.write_queue.append(std.heap.c_allocator, .{
            .dst_set = .null_handle,
            .dst_binding = binding,
            .dst_array_element = 0,
            .descriptor_count = 1,
            .descriptor_type = .combined_image_sampler,
            .p_buffer_info = undefined,
            .p_image_info = undefined,
            .p_texel_buffer_view = undefined,
        }) catch return;
    }

    pub fn updateStorageImage(
        self: *DescriptorPool,
        binding: u32,
        layout: vk.ImageLayout,
        view: vk.ImageView,
    ) void {
        self.write_image_infos.append(std.heap.c_allocator, .{
            .image_layout = layout,
            .image_view = view,
            .sampler = .null_handle,
        }) catch return;

        const index = @as(u32, @intCast(self.write_image_infos.items.len - 1));
        self.image_write_indices.append(std.heap.c_allocator, index) catch return;

        self.write_queue.append(std.heap.c_allocator, .{
            .dst_set = .null_handle,
            .dst_binding = binding,
            .dst_array_element = 0,
            .descriptor_count = 1,
            .descriptor_type = .storage_image,
            .p_buffer_info = undefined,
            .p_image_info = undefined,
            .p_texel_buffer_view = undefined,
        }) catch return;
    }

    pub fn clear(self: *DescriptorPool, dev: *vk.DeviceProxy) void {
        _ = dev.resetDescriptorPool(self.pool, .{}) catch {};
        self.set_count = 0;
        @memset(self.sets, .null_handle);
        self.ubo_offset = 0;
    }
};
