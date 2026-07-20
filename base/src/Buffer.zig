const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;

const GraphicsCtx = @import("GraphicsCtx.zig");
const DestroyQueue = @import("DestroyQueue.zig");

const Self = @This();

handle: vk.Buffer = .null_handle,
allocation: vma.VmaAllocation = null,
size: vk.DeviceSize = 0,
address: vk.DeviceAddress = 0,
mapped: ?[*]u8 = null,
mapped_len: usize = 0,
coherent: bool = false,

pub const empty: Self = .{};

pub const MemoryType = enum {
    gpu_only,
    cpu_to_gpu_staging,
    cpu_to_gpu_dynamic,
    gpu_to_cpu_readback,
};

pub fn init(
    gc: *const GraphicsCtx,
    queue_type: GraphicsCtx.QueueType,
    name: [:0]const u8,
    size_: vk.DeviceSize,
    usage: vk.BufferUsageFlags,
    mem_type: MemoryType,
) !Self {
    var buf: Self = undefined;

    var usage_with_address = usage;
    usage_with_address.shader_device_address_bit = true;

    const buffer_info = vk.BufferCreateInfo{
        .size = size_,
        .usage = usage_with_address,
        .sharing_mode = .exclusive,
        .queue_family_index_count = 1,
        .p_queue_family_indices = (&gc.queueFamilyFromType(queue_type))[0..1],
    };

    var alloc_info = vma.VmaAllocationCreateInfo{
        // .usage = if (host) vma.VMA_MEMORY_USAGE_AUTO_PREFER_HOST else vma.VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .flags = 0,
    };

    switch (mem_type) {
        .gpu_only => {
            alloc_info.usage = vma.VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            // No host flags. VMA puts this in pure DEVICE_LOCAL.
        },
        .cpu_to_gpu_staging => {
            alloc_info.usage = vma.VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            alloc_info.flags |= vma.VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            alloc_info.flags |= vma.VMA_ALLOCATION_CREATE_MAPPED_BIT;
            // PREFER_HOST tells VMA: "Keep this in System RAM, don't waste VRAM/ReBAR."
        },
        .cpu_to_gpu_dynamic => {
            alloc_info.usage = vma.VMA_MEMORY_USAGE_AUTO; 
            alloc_info.flags |= vma.VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            alloc_info.flags |= vma.VMA_ALLOCATION_CREATE_MAPPED_BIT;
            // AUTO + HOST_WRITE tells VMA: "I want the fastest GPU memory, but the CPU 
            // MUST be able to write to it." -> VMA targets ReBAR (DEVICE_LOCAL | HOST_VISIBLE).
        },
        .gpu_to_cpu_readback => {
            alloc_info.usage = vma.VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            alloc_info.flags |= vma.VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            alloc_info.flags |= vma.VMA_ALLOCATION_CREATE_MAPPED_BIT;
            // RANDOM_BIT enforces HOST_CACHED. 
        },
    }

    var alloc_info_out: vma.VmaAllocationInfo = undefined;
    const res = vma.vmaCreateBuffer(
        gc.vma_alloc,
        @ptrCast(&buffer_info),
        &alloc_info,
        @ptrCast(&buf.handle),
        &buf.allocation,
        &alloc_info_out,
    );
    if (res != 0) return error.VmaBufferCreateFailed;

    const address_info = vk.BufferDeviceAddressInfo{
        .buffer = buf.handle,
    };
    buf.address = gc.dev.getBufferDeviceAddress(&address_info);

    buf.size = size_;
    if (mem_type != .gpu_only) {
        buf.mapped = @ptrCast(@alignCast(alloc_info_out.pMappedData));
        buf.mapped_len = @intCast(size_);

        var props: u32 = undefined;
        vma.vmaGetAllocationMemoryProperties(gc.vma_alloc, buf.allocation, &props);
        buf.coherent = (props & vma.VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }

    try gc.dev.setDebugUtilsObjectNameEXT(&.{
        .object_type = .buffer,
        .object_handle = @intFromEnum(buf.handle),
        .p_object_name = name,
    });

    return buf;
}

pub fn deinit(self: *Self, destroy_queue: *DestroyQueue) void {
    if (self.handle != .null_handle) {
        destroy_queue.enqueueBuffer(self.handle, self.allocation) catch @panic("OOM");
        self.handle = .null_handle;
        self.allocation = null;
    }
}

pub fn deinitNow(self: *Self, vma_alloc: vma.VmaAllocator) void {
    if (self.handle != .null_handle) {
        vma.vmaDestroyBuffer(vma_alloc, @ptrFromInt(@intFromEnum(self.handle)), self.allocation);
        self.handle = .null_handle;
        self.allocation = null;
    }
}

pub fn flush(self: *const Self, vma_alloc: vma.VmaAllocator, offset: u64, size_: u64) void {
    if (!self.coherent) {
        _ = vma.vmaFlushAllocation(vma_alloc, self.allocation, offset, size_);
    }
}
