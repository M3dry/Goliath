const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;

pub const DestroyQueue = struct {
    const Entry = union(enum) {
        buffer: struct { handle: vk.Buffer, allocation: vma.VmaAllocation },
        image: struct { handle: vk.Image, allocation: vma.VmaAllocation },
        image_view: vk.ImageView,
        sampler: vk.Sampler,
    };

    alloc: std.mem.Allocator,
    frames: [2]std.ArrayListUnmanaged(Entry) = .{ .empty, .empty },

    pub fn init(alloc: std.mem.Allocator) DestroyQueue {
        return .{
            .alloc = alloc,
        };
    }

    pub fn deinit(self: *DestroyQueue, vma_alloc: anytype, dev: *vk.DeviceProxy) void {
        for (&self.frames) |*frame| {
            for (frame.items) |entry| {
                switch (entry) {
                    .buffer => |b| vma.vmaDestroyBuffer(vma_alloc, @ptrFromInt(@intFromEnum(b.handle)), b.allocation),
                    .image => |i| vma.vmaDestroyImage(vma_alloc, @ptrFromInt(@intFromEnum(i.handle)), i.allocation),
                    .image_view => |v| dev.destroyImageView(v, null),
                    .sampler => |s| dev.destroySampler(s, null),
                }
            }
            frame.deinit(self.alloc);
        }
    }

    pub fn enqueueBuffer(self: *DestroyQueue, handle: vk.Buffer, allocation: vma.VmaAllocation, frame_index: u32) void {
        self.frames[frame_index].append(self.alloc, .{ .buffer = .{ .handle = handle, .allocation = allocation } }) catch {};
    }

    pub fn enqueueImage(self: *DestroyQueue, handle: vk.Image, allocation: vma.VmaAllocation, frame_index: u32) void {
        self.frames[frame_index].append(self.alloc, .{ .image = .{ .handle = handle, .allocation = allocation } }) catch {};
    }

    pub fn enqueueImageView(self: *DestroyQueue, view: vk.ImageView, frame_index: u32) void {
        self.frames[frame_index].append(self.alloc, .{ .image_view = view }) catch {};
    }

    pub fn enqueueSampler(self: *DestroyQueue, sampler: vk.Sampler, frame_index: u32) void {
        self.frames[frame_index].append(self.alloc, .{ .sampler = sampler }) catch {};
    }

    pub fn flush(self: *DestroyQueue, vma_alloc: vma.VmaAllocator, dev: *vk.DeviceProxy, frame_index: u32) void {
        const frame = &self.frames[frame_index];
        for (frame.items) |entry| {
            switch (entry) {
                .buffer => |b| vma.vmaDestroyBuffer(vma_alloc, @ptrFromInt(@intFromEnum(b.handle)), b.allocation),
                .image => |i| vma.vmaDestroyImage(vma_alloc, @ptrFromInt(@intFromEnum(i.handle)), i.allocation),
                .image_view => |v| dev.destroyImageView(v, null),
                .sampler => |s| dev.destroySampler(s, null),
            }
        }
        frame.clearRetainingCapacity();
    }
};
