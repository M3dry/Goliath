const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;

const DestroyQueue = @import("DestroyQueue.zig");
const GraphicsCtx = @import("GraphicsCtx.zig");

fn aspectFromFormat(format: vk.Format) vk.ImageAspectFlags {
    return switch (format) {
        .d16_unorm, .d32_sfloat, .x8_d24_unorm_pack32 => .{ .depth_bit = true },
        .s8_uint => .{ .stencil_bit = true },
        .d24_unorm_s8_uint, .d32_sfloat_s8_uint => .{ .depth_bit = true, .stencil_bit = true },
        else => .{ .color_bit = true },
    };
}

pub const Image2D = struct {
    handle: vk.Image = .null_handle,
    allocation: vma.VmaAllocation = null,
    format: vk.Format = .undefined,
    extent: vk.Extent2D = .{ .width = 0, .height = 0 },
    mip_levels: u32 = 0,
    array_layers: u32 = 0,

    pub const Description = struct {
        format: vk.Format,
        extent: vk.Extent2D,
        usage: vk.ImageUsageFlags,
        mip_levels: u32 = 1,
        array_layers: u32 = 1,
    };

    pub fn init(
        gc: *const GraphicsCtx,
        vma_alloc: vma.VmaAllocator,
        name: [:0]const u8,
        desc: Description,
    ) !Image2D {
        const image_info = vk.ImageCreateInfo{
            .image_type = .@"2d",
            .format = desc.format,
            .extent = .{
                .width = desc.extent.width,
                .height = desc.extent.height,
                .depth = 1,
            },
            .mip_levels = desc.mip_levels,
            .array_layers = desc.array_layers,
            .samples = .{ .@"1_bit" = true },
            .tiling = .optimal,
            .usage = desc.usage,
            .sharing_mode = .exclusive,
            .initial_layout = .undefined,
        };

        var alloc_info = vma.VmaAllocationCreateInfo{
            .usage = vma.VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };

        var img: Image2D = undefined;
        var alloc_info_out: vma.VmaAllocationInfo = undefined;
        const res = vma.vmaCreateImage(
            vma_alloc,
            @ptrCast(&image_info),
            &alloc_info,
            @ptrCast(&img.handle),
            &img.allocation,
            &alloc_info_out,
        );
        if (res < 0) return error.VmaImageError;

        try gc.dev.setDebugUtilsObjectNameEXT(&.{
            .object_type = .image,
            .object_handle = @intFromEnum(img.handle),
            .p_object_name = name,
        });

        img.format = desc.format;
        img.extent = desc.extent;
        img.mip_levels = desc.mip_levels;
        img.array_layers = desc.array_layers;

        return img;
    }

    pub fn deinit(self: *Image2D, destroy_queue: *DestroyQueue) void {
        if (self.handle != .null_handle) {
            destroy_queue.enqueueImage(self.handle, self.allocation) catch @panic("OOM");

            self.handle = .null_handle;
            self.allocation = null;
        }
    }

    pub fn deinitNow(self: *Image2D, gc: *const GraphicsCtx) void {
        if (self.handle != .null_handle) {
            gc.dev.destroyImage(self.handle, null);
            vma.vmaDestroyImage(gc.vma_alloc, @ptrFromInt(@intFromEnum(self.handle)), self.allocation);

            self.handle = .null_handle;
            self.allocation = null;
        }
    }
};

pub const ImageView = struct {
    handle: vk.ImageView = .null_handle,

    pub const Description = struct {
        image: vk.Image,
        format: vk.Format,
        view_type: vk.ImageViewType = .@"2d",
        components: vk.ComponentMapping = .{
            .r = .identity,
            .g = .identity,
            .b = .identity,
            .a = .identity,
        },
        subresource_range: vk.ImageSubresourceRange = .{
            .aspect_mask = .{ .color_bit = true },
            .base_mip_level = 0,
            .level_count = vk.REMAINING_MIP_LEVELS,
            .base_array_layer = 0,
            .layer_count = vk.REMAINING_ARRAY_LAYERS,
        },

        pub fn fromImage(image: *const Image2D) Description {
            return .{
                .image = image.handle,
                .format = image.format,
                .subresource_range = .{
                    .aspect_mask = aspectFromFormat(image.format),
                    .base_mip_level = 0,
                    .level_count = image.mip_levels,
                    .base_array_layer = 0,
                    .layer_count = image.array_layers,
                },
            };
        }
    };

    pub fn init(gc: *const GraphicsCtx, desc: Description) !ImageView {
        const handle = try gc.dev.createImageView(&.{
            .image = desc.image,
            .view_type = desc.view_type,
            .format = desc.format,
            .components = desc.components,
            .subresource_range = desc.subresource_range,
        }, null);

        return .{ .handle = handle };
    }

    pub fn deinit(self: *ImageView, destroy_queue: *DestroyQueue) void {
        if (self.handle != .null_handle) {
            destroy_queue.enqueueImageView(self.handle) catch @panic("OOM");
            self.handle = .null_handle;
        }
    }

    pub fn deinitNow(self: *ImageView, gc: *const GraphicsCtx) void {
        if (self.handle != .null_handle) {
            gc.dev.destroyImageView(self.handle, null);
            self.handle = .null_handle;
        }
    }
};
