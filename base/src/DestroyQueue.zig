const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;

const Self = @This();

const Entry = union(enum) {
    buffer: struct { handle: vk.Buffer, allocation: vma.VmaAllocation },
    image: struct { handle: vk.Image, allocation: vma.VmaAllocation },
    image_view: vk.ImageView,
    sampler: vk.Sampler,
    descriptor_set_layout: vk.DescriptorSetLayout,
    descriptor_pool: vk.DescriptorPool,
};

alloc: std.mem.Allocator,
frames: [2]std.ArrayListUnmanaged(Entry) = .{ .empty, .empty },
current_frame: u32,

pub fn init(alloc: std.mem.Allocator, current_frame: u32) Self {
    return .{
        .alloc = alloc,
        .current_frame = current_frame,
    };
}

pub fn deinit(self: *Self, vma_alloc: anytype, dev: *vk.DeviceProxy) void {
    for (&self.frames) |*frame| {
        for (frame.items) |entry| {
            switch (entry) {
                .buffer => |b| vma.vmaDestroyBuffer(vma_alloc, @ptrFromInt(@intFromEnum(b.handle)), b.allocation),
                .image => |i| vma.vmaDestroyImage(vma_alloc, @ptrFromInt(@intFromEnum(i.handle)), i.allocation),
                .image_view => |v| dev.destroyImageView(v, null),
                .sampler => |s| dev.destroySampler(s, null),
                .descriptor_set_layout => |s| dev.destroyDescriptorSetLayout(s, null),
                .descriptor_pool => |p| dev.destroyDescriptorPool(p, null),
            }
        }
        frame.deinit(self.alloc);
    }
}

pub fn enqueueBuffer(self: *Self, handle: vk.Buffer, allocation: vma.VmaAllocation) !void {
    try self.frames[self.current_frame].append(self.alloc, .{ .buffer = .{ .handle = handle, .allocation = allocation } });
}

pub fn enqueueImage(self: *Self, handle: vk.Image, allocation: vma.VmaAllocation) !void {
    try self.frames[self.current_frame].append(self.alloc, .{ .image = .{ .handle = handle, .allocation = allocation } });
}

pub fn enqueueImageView(self: *Self, view: vk.ImageView) !void {
    try self.frames[self.current_frame].append(self.alloc, .{ .image_view = view });
}

pub fn enqueueSampler(self: *Self, sampler: vk.Sampler) !void {
    try self.frames[self.current_frame].append(self.alloc, .{ .sampler = sampler });
}

pub fn enqueueDescriptorSetLayout(self: *Self, set_layout: vk.DescriptorSetLayout) !void {
    try self.frames[self.current_frame].append(self.alloc, .{ .descriptor_set_layout = set_layout });
}

pub fn enqueueDescriptorPool(self: *Self, pool: vk.DescriptorPool) !void {
    try self.frames[self.current_frame].append(self.alloc, .{ .descriptor_pool = pool });
}

pub fn flush(self: *Self, vma_alloc: vma.VmaAllocator, dev: *vk.DeviceProxy) void {
    const frame = &self.frames[self.current_frame];
    for (frame.items) |entry| {
        switch (entry) {
            .buffer => |b| vma.vmaDestroyBuffer(vma_alloc, @ptrFromInt(@intFromEnum(b.handle)), b.allocation),
            .image => |i| vma.vmaDestroyImage(vma_alloc, @ptrFromInt(@intFromEnum(i.handle)), i.allocation),
            .image_view => |v| dev.destroyImageView(v, null),
            .sampler => |s| dev.destroySampler(s, null),
            .descriptor_set_layout => |s| dev.destroyDescriptorSetLayout(s, null),
            .descriptor_pool => |p| dev.destroyDescriptorPool(p, null),
        }
    }
    frame.clearRetainingCapacity();
}

pub fn update_current_frame(self: *Self, frame: u32) void {
    self.current_frame = frame;
}
