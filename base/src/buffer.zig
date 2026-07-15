const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;
const GraphicsCtx = @import("graphics_ctx.zig").GraphicsCtx;
const DestroyQueue = @import("destroy_queue.zig").DestroyQueue;

pub const Buffer = struct {
    handle: vk.Buffer = .null_handle,
    allocation: vma.VmaAllocation = null,
    size: vk.DeviceSize = 0,
    address: vk.DeviceAddress = 0,
    mapped: ?[*]u8 = null,
    mapped_len: usize = 0,
    coherent: bool = false,

    pub fn init(
        gc: *const GraphicsCtx,
        queue_type: GraphicsCtx.QueueType,
        name: [:0]const u8,
        size_: vk.DeviceSize,
        usage: vk.BufferUsageFlags,
        host: bool,
    ) !Buffer {
        var buf: Buffer = undefined;

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
            .usage = if (host) vma.VMA_MEMORY_USAGE_AUTO_PREFER_HOST else vma.VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .flags = 0,
        };
        if (host) {
            alloc_info.flags |= vma.VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            alloc_info.flags |= vma.VMA_ALLOCATION_CREATE_MAPPED_BIT;
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
        if (host) {
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

    pub fn deinit(self: *Buffer, destroy_queue: *DestroyQueue) void {
        if (self.handle != .null_handle) {
            destroy_queue.enqueueBuffer(self.handle, self.allocation);
            self.handle = .null_handle;
            self.allocation = null;
        }
    }

    pub fn deinitNow(self: *Buffer, vma_alloc: vma.VmaAllocator) void {
        if (self.handle != .null_handle) {
            vma.vmaDestroyBuffer(vma_alloc, @ptrFromInt(@intFromEnum(self.handle)), self.allocation);
            self.handle = .null_handle;
            self.allocation = null;
        }
    }

    pub fn flush(self: *const Buffer, vma_alloc: vma.VmaAllocator, offset: u64, size_: u64) void {
        if (!self.coherent) {
            _ = vma.vmaFlushAllocation(vma_alloc, self.allocation, offset, size_);
        }
    }
};
