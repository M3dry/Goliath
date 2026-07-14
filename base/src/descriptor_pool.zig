const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;
const buf = @import("buffer.zig");

const Buffer = buf.Buffer;

pub const DescriptorPool = struct {
    pool: vk.DescriptorPool = .null_handle,
    sets: []vk.DescriptorSet = &[0]vk.DescriptorSet{},
    set_count: u32 = 0,

    ubo_buffer: Buffer = .{},
    ubo_offset: u64 = 0,

    write_id: u64 = std.math.maxInt(u64),
    write_buffer_infos: std.ArrayListUnmanaged(vk.DescriptorBufferInfo) = .{},
    write_image_infos: std.ArrayListUnmanaged(vk.DescriptorImageInfo) = .{},
    write_queue: std.ArrayListUnmanaged(vk.WriteDescriptorSet) = .{},

    const max_sets: u32 = 500;
    const ubo_size: vk.DeviceSize = 16000;

    pub fn init(
        dev: *vk.DeviceProxy,
        vma_alloc: vma.VmaAllocator,
        gfx_queue_family: u32,
        alloc: std.mem.Allocator,
    ) !DescriptorPool {
        var dp: DescriptorPool = .{};

        const pool_sizes = [_]vk.DescriptorPoolSize{
            .{ .type = .combined_image_sampler, .descriptor_count = max_sets },
            .{ .type = .uniform_buffer, .descriptor_count = max_sets },
            .{ .type = .storage_buffer, .descriptor_count = max_sets },
            .{ .type = .storage_image, .descriptor_count = max_sets },
        };

        dp.pool = try dev.createDescriptorPool(&.{
            .flags = .{ .update_after_bind_bit = true },
            .max_sets = max_sets,
            .pool_size_count = @intCast(pool_sizes.len),
            .p_pool_sizes = &pool_sizes,
        }, null);

        dp.sets = try alloc.alloc(vk.DescriptorSet, max_sets);
        @memset(dp.sets, .null_handle);

        dp.ubo_buffer = try Buffer.init(
            dev,
            vma_alloc,
            gfx_queue_family,
            "DescriptorPool UBO",
            ubo_size,
            .{ .uniform_buffer_bit = true },
            true,
        );

        dp.write_buffer_infos = try std.ArrayListUnmanaged(vk.DescriptorBufferInfo).initCapacity(alloc, 64);
        dp.write_image_infos = try std.ArrayListUnmanaged(vk.DescriptorImageInfo).initCapacity(alloc, 64);
        dp.write_queue = try std.ArrayListUnmanaged(vk.WriteDescriptorSet).initCapacity(alloc, 64);

        return dp;
    }

    pub fn deinit(
        self: *DescriptorPool,
        dev: *vk.DeviceProxy,
        vma_alloc: vma.VmaAllocator,
        alloc: std.mem.Allocator,
    ) void {
        if (self.pool != .null_handle) {
            dev.destroyDescriptorPool(self.pool, null);
        }
        self.ubo_buffer.deinitNow(vma_alloc);
        alloc.free(self.sets);
        self.write_buffer_infos.deinit(alloc);
        self.write_image_infos.deinit(alloc);
        self.write_queue.deinit(alloc);
    }

    pub fn newSet(self: *DescriptorPool, dev: *vk.DeviceProxy, layout: vk.DescriptorSetLayout) !u64 {
        const id = self.set_count;
        if (id >= max_sets) return error.DescriptorPoolFull;
        self.set_count = id + 1;

        const result = try dev.allocateDescriptorSets(&.{
            .descriptor_pool = self.pool,
            .descriptor_set_count = 1,
            .p_set_layouts = @ptrCast(&layout),
        });
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
    }

    pub fn endUpdate(self: *DescriptorPool, dev: *vk.DeviceProxy) void {
        for (self.write_queue.items) |*write| {
            write.dst_set = self.sets[@intCast(self.write_id)];
            switch (write.descriptor_type) {
                .uniform_buffer, .storage_buffer => {
                    const idx = @intFromPtr(write.p_buffer_info);
                    write.p_buffer_info = &self.write_buffer_infos.items[idx];
                },
                .combined_image_sampler, .storage_image => {
                    const idx = @intFromPtr(write.p_image_info);
                    write.p_image_info = &self.write_image_infos.items[idx];
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

        const index = self.write_buffer_infos.items.len - 1;
        self.write_queue.append(std.heap.c_allocator, .{
            .descriptor_count = 1,
            .descriptor_type = .uniform_buffer,
            .dst_binding = binding,
            .p_buffer_info = @ptrFromInt(index),
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

        const index = self.write_image_infos.items.len - 1;
        self.write_queue.append(std.heap.c_allocator, .{
            .descriptor_count = 1,
            .descriptor_type = .combined_image_sampler,
            .dst_binding = binding,
            .p_image_info = @ptrFromInt(index),
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

        const index = self.write_image_infos.items.len - 1;
        self.write_queue.append(std.heap.c_allocator, .{
            .descriptor_count = 1,
            .descriptor_type = .storage_image,
            .dst_binding = binding,
            .p_image_info = @ptrFromInt(index),
        }) catch return;
    }

    pub fn clear(self: *DescriptorPool, dev: *vk.DeviceProxy) void {
        _ = dev.resetDescriptorPool(self.pool, 0) catch {};
        self.set_count = 0;
        @memset(self.sets, .null_handle);
        self.ubo_offset = 0;
    }
};
