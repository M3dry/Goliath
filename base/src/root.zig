pub const vk = @import("vulkan");
pub const zglfw = @import("zglfw");
pub const timing = @import("timing.zig");
pub const input = @import("input.zig");
pub const imgui = @import("imgui.zig");
pub const zgui = @import("zgui");

pub const Buffer = @import("buffer.zig").Buffer;
pub const DescriptorPool = @import("descriptor_pool.zig").DescriptorPool;
pub const TexturePool = @import("texture_pool.zig").TexturePool;
pub const DestroyQueue = @import("destroy_queue.zig").DestroyQueue;
pub const ShaderModule = @import("shader.zig").ShaderModule;

const std = @import("std");
const vma = @import("vma.zig").vma;
const GraphicsCtx = @import("graphics_ctx.zig").GraphicsCtx;

const Allocator = std.mem.Allocator;

pub const BlitStrategy = enum {
    letterbox,
    stretch,
};

pub const Ctx = struct {
    pub const frames_in_flight: u32 = 2;

    window: *zglfw.Window,
    graphics: GraphicsCtx,
    frames: []Frame,
    swapchain: Swapchain,
    curent_frame: u32 = 0,

    blit_strategy: BlitStrategy,
    render_extent: vk.Extent2D,
    render_format: vk.Format,

    destroy_queue: DestroyQueue,

    timeline_semaphore: vk.Semaphore,
    timeline_value: u64,

    pub const WindowOpts = struct {
        pub const Size = union(enum) {
            dims: struct {u32, u32},
            fullscreen,
        };

        size: Size = .{ .dims = .{0,0} },
        resizable: bool = false,

        render_extent: vk.Extent2D = .{ .width = 1920, .height = 1080 },
        blit_strategy: BlitStrategy = .stretch,
    };

    pub fn init(alloc: Allocator, name: [:0]const u8, window_opts: WindowOpts) !Ctx {
        try zglfw.init();

        if (!zglfw.isVulkanSupported()) return error.NoVulkan;

        zglfw.windowHint(.client_api, .no_api);
        zglfw.windowHint(.resizable, window_opts.resizable);
        zglfw.windowHint(.auto_iconify, false);

        const mon_width, const mon_height = if (zglfw.getPrimaryMonitor()) |mon| blk: {
            const mode = mon.getVideoMode() catch break :blk .{0, 0};
            break :blk .{mode.width, mode.height};
        } else .{0, 0};

        var width: c_int, var height: c_int = switch (window_opts.size) {
            .dims => |dims| .{ @intCast(dims.@"0"), @intCast(dims.@"1") },
            .fullscreen => .{mon_width, mon_height},
        };

        const window = try zglfw.createWindow(width, height, name, null, null);

        zglfw.getFramebufferSize(window, &width, &height);
        const extent: vk.Extent2D = .{
            .width = @intCast(width),
            .height = @intCast(height),
        };

        const graphics_ctx = try GraphicsCtx.init(alloc, window);

        const frames = try alloc.alloc(Frame, frames_in_flight);
        errdefer alloc.free(frames);

        var i: usize = 0;
        errdefer for (frames[0..i]) |frame| frame.deinit(&graphics_ctx);

        const swapchain = try Swapchain.init(alloc, &graphics_ctx, extent);

        for (frames) |_| {
            frames[i] = try Frame.init(&graphics_ctx);
            i += 1;
        }

        const rt_format = .b8g8r8a8_unorm;
        {
            var rt_idx: usize = 0;
            errdefer for (frames[0..rt_idx]) |*f| f.deinitRenderTexture(&graphics_ctx);
            for (frames) |*f| {
                try f.initRenderTexture(&graphics_ctx, window_opts.render_extent, rt_format);
                rt_idx += 1;
            }
        }

        const timeline_semaphore = try graphics_ctx.dev.createSemaphore(&vk.SemaphoreCreateInfo{
            .p_next = &vk.SemaphoreTypeCreateInfo{
                .initial_value = 0,
                .semaphore_type = .timeline,
            },
        }, null);

        return Ctx{
            .window = window,
            .graphics = graphics_ctx,
            .frames = frames,
            .swapchain = swapchain,
            .blit_strategy = window_opts.blit_strategy,
            .render_extent = window_opts.render_extent,
            .render_format = rt_format,
            .destroy_queue = .init(alloc),
            .timeline_semaphore = timeline_semaphore,
            .timeline_value = 0,
        };
    }

    pub fn deinit(self: *Ctx, alloc: Allocator) void {
        if (self.graphics.dev.deviceWaitIdle()) {} else |_| { return; }

        for (self.frames) |frame| {
            frame.deinit(&self.graphics);
        }
        alloc.free(self.frames);

        self.destroy_queue.deinit(self.graphics.vma_alloc, &self.graphics.dev);

        self.graphics.dev.destroySemaphore(self.timeline_semaphore, null);

        self.swapchain.deinit(&self.graphics, alloc);
        self.graphics.deinit(alloc);

        self.window.destroy();
        zglfw.terminate();
    }

    pub fn renderTarget(self: Ctx) struct { image: vk.Image, view: vk.ImageView } {
        const frame = self.frames[self.curent_frame];
        return .{ .image = frame.render_image, .view = frame.render_view };
    }

    pub const PrepareResult = enum {
        skip_drawing,
        success,
    };

    pub fn prepare_frame(self: *Ctx) !PrepareResult {
        const frame = &self.frames[self.curent_frame];
        _ = try self.graphics.dev.waitForFences(&[_]vk.Fence{ frame.fence }, .true, std.math.maxInt(u64));
        self.destroy_queue.flush(self.graphics.vma_alloc, &self.graphics.dev, self.curent_frame);
        try self.graphics.dev.resetFences(&[_]vk.Fence{ frame.fence });

        const acquired = self.graphics.dev.acquireNextImageKHR(self.swapchain.handle, std.math.maxInt(u64), frame.semaphore, .null_handle) catch |err| if (err == error.OutOfDateKHR) vk.DeviceWrapper.AcquireNextImageKHRResult{
            .result = .error_out_of_date_khr,
            .image_index = 0,
        } else return err;
        switch (acquired.result) {
            .error_out_of_date_khr => {
                frame.acquired_swapchain = null;

                self.graphics.dev.destroySemaphore(frame.semaphore, null);
                frame.semaphore = try self.graphics.dev.createSemaphore(&.{}, null);

                self.graphics.dev.destroyFence(frame.fence, null);
                frame.fence = try self.graphics.dev.createFence(&.{
                    .flags = .{ .signaled_bit = true },
                }, null);

                return .skip_drawing;
            },
            .suboptimal_khr => {},
            else => {},
        }

        frame.acquired_swapchain = acquired.image_index;
        return .success;
    }

    pub fn prepare_draw(self: Ctx) !void {
        const frame = self.frames[self.curent_frame];
        std.debug.assert(frame.acquired_swapchain != null);

        try self.graphics.dev.resetCommandBuffer(frame.cmd_buf, .{});

        try self.graphics.dev.beginCommandBuffer(frame.cmd_buf, &.{
            .flags = .{ .one_time_submit_bit = true, },
        });

        const barriers = [_]vk.ImageMemoryBarrier2{
            .{
                .src_stage_mask = .{ .all_commands_bit = true },
                .src_access_mask = .{ .memory_write_bit = true },
                .dst_stage_mask = .{ .color_attachment_output_bit = true },
                .dst_access_mask = .{ .color_attachment_write_bit = true, .color_attachment_read_bit = true },
                .old_layout = .undefined,
                .new_layout = .color_attachment_optimal,
                .src_queue_family_index = self.graphics.graphics_family,
                .dst_queue_family_index = self.graphics.graphics_family,
                .subresource_range = .{
                    .aspect_mask = .{ .color_bit = true },
                    .base_mip_level = 0,
                    .level_count = vk.REMAINING_MIP_LEVELS,
                    .base_array_layer = 0,
                    .layer_count = vk.REMAINING_ARRAY_LAYERS,
                },
                .image = frame.render_image,
            },
            .{
                .src_stage_mask = .{ .all_commands_bit = true },
                .src_access_mask = .{ .memory_write_bit = true },
                .dst_stage_mask = .{ .all_transfer_bit = true },
                .dst_access_mask = .{ .transfer_write_bit = true },
                .old_layout = .undefined,
                .new_layout = .transfer_dst_optimal,
                .src_queue_family_index = self.graphics.graphics_family,
                .dst_queue_family_index = self.graphics.graphics_family,
                .subresource_range = .{
                    .aspect_mask = .{ .color_bit = true },
                    .base_mip_level = 0,
                    .level_count = vk.REMAINING_MIP_LEVELS,
                    .base_array_layer = 0,
                    .layer_count = vk.REMAINING_ARRAY_LAYERS,
                },
                .image = self.swapchain.images[frame.acquired_swapchain.?].image,
            },
        };
        self.graphics.dev.cmdPipelineBarrier2(frame.cmd_buf, &.{
            .image_memory_barrier_count = 2,
            .p_image_memory_barriers = &barriers,
        });
    }

    pub const SwapchainState = enum {
        recreated,
        same,
    };

    pub fn end_drawing(self: *Ctx) void {
        const frame = &self.frames[self.curent_frame];
        const acquired_image = frame.acquired_swapchain orelse return;

        const rw = self.render_extent.width;
        const rh = self.render_extent.height;
        const sw = self.swapchain.extent.width;
        const sh = self.swapchain.extent.height;

        const src_offsets = [2]vk.Offset3D{
            .{ .x = 0, .y = 0, .z = 0 },
            .{ .x = @as(i32, @intCast(rw)), .y = @as(i32, @intCast(rh)), .z = 1 },
        };

        const dst_offsets = switch (self.blit_strategy) {
            .stretch => [2]vk.Offset3D{
                .{ .x = 0, .y = 0, .z = 0 },
                .{ .x = @as(i32, @intCast(sw)), .y = @as(i32, @intCast(sh)), .z = 1 },
            },
            .letterbox => blk: {
                const src_aspect = @as(f64, @floatFromInt(rw)) / @as(f64, @floatFromInt(rh));
                const dst_aspect = @as(f64, @floatFromInt(sw)) / @as(f64, @floatFromInt(sh));
                const bw, const bh = if (dst_aspect > src_aspect) blk2: {
                    const bh2 = @as(f64, @floatFromInt(sh));
                    break :blk2 .{ bh2 * src_aspect, bh2 };
                } else blk2: {
                    const bw2 = @as(f64, @floatFromInt(sw));
                    break :blk2 .{ bw2, bw2 / src_aspect };
                };
                const ox = @as(i32, @intFromFloat(@floor((@as(f64, @floatFromInt(sw)) - bw) / 2.0)));
                const oy = @as(i32, @intFromFloat(@floor((@as(f64, @floatFromInt(sh)) - bh) / 2.0)));
                break :blk [2]vk.Offset3D{
                    .{ .x = ox, .y = oy, .z = 0 },
                    .{ .x = ox + @as(i32, @intFromFloat(bw)), .y = oy + @as(i32, @intFromFloat(bh)), .z = 1 },
                };
            },
        };

        if (self.blit_strategy == .letterbox) {
            const clear_value = vk.ClearColorValue{ .float_32 = .{ 0, 0, 0, 1 } };
            self.graphics.dev.cmdClearColorImage(
                frame.cmd_buf,
                self.swapchain.images[acquired_image].image,
                .transfer_dst_optimal,
                &clear_value,
                &[_]vk.ImageSubresourceRange{.{
                    .aspect_mask = .{ .color_bit = true },
                    .base_mip_level = 0,
                    .level_count = vk.REMAINING_MIP_LEVELS,
                    .base_array_layer = 0,
                    .layer_count = vk.REMAINING_ARRAY_LAYERS,
                }},
            );

            self.graphics.dev.cmdPipelineBarrier2(frame.cmd_buf, &.{
                .image_memory_barrier_count = 1,
                .p_image_memory_barriers = (&vk.ImageMemoryBarrier2{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = .{ .all_transfer_bit = true },
                    .dst_access_mask = .{ .transfer_write_bit = true },
                    .old_layout = .transfer_dst_optimal,
                    .new_layout = .transfer_dst_optimal,
                    .src_queue_family_index = self.graphics.graphics_family,
                    .dst_queue_family_index = self.graphics.graphics_family,
                    .subresource_range = .{
                        .aspect_mask = .{ .color_bit = true },
                        .base_mip_level = 0,
                        .level_count = vk.REMAINING_MIP_LEVELS,
                        .base_array_layer = 0,
                        .layer_count = vk.REMAINING_ARRAY_LAYERS,
                    },
                    .image = self.swapchain.images[acquired_image].image,
                })[0..1],
            });
        }

        self.graphics.dev.cmdBlitImage(
            frame.cmd_buf,
            frame.render_image,
            .transfer_src_optimal,
            self.swapchain.images[acquired_image].image,
            .transfer_dst_optimal,
            &[_]vk.ImageBlit{.{
                .src_subresource = .{ .aspect_mask = .{ .color_bit = true }, .mip_level = 0, .base_array_layer = 0, .layer_count = 1 },
                .src_offsets = src_offsets,
                .dst_subresource = .{ .aspect_mask = .{ .color_bit = true }, .mip_level = 0, .base_array_layer = 0, .layer_count = 1 },
                .dst_offsets = dst_offsets,
            }},
            .linear,
        );
    }

    pub fn end_frame(self: *Ctx, alloc: Allocator) !SwapchainState {
        const frame = &self.frames[self.curent_frame];

        var res: vk.Result = .error_out_of_date_khr;
        if (frame.acquired_swapchain) |acquired_image| {
            try self.graphics.dev.endCommandBuffer(frame.cmd_buf);

            self.timeline_value += 1;

            try self.graphics.dev.queueSubmit2(self.graphics.graphics_queue, (&vk.SubmitInfo2{
                .wait_semaphore_info_count = 1,
                .p_wait_semaphore_infos = (&vk.SemaphoreSubmitInfo{
                    .semaphore = frame.semaphore,
                    .value = 0,
                    .stage_mask = .{ .all_commands_bit = true },
                    .device_index = 0,
                })[0..1],
                .command_buffer_info_count = 1,
                .p_command_buffer_infos = (&vk.CommandBufferSubmitInfo{
                    .command_buffer = frame.cmd_buf,
                    .device_mask = 0,
                })[0..1],
                .signal_semaphore_info_count = 2,
                .p_signal_semaphore_infos = &[_]vk.SemaphoreSubmitInfo{
                    .{
                        .semaphore = self.swapchain.images[acquired_image].semaphore,
                        .value = 0,
                        .stage_mask = .{ .all_commands_bit = true },
                        .device_index = 0,
                    },
                    .{
                        .semaphore = self.timeline_semaphore,
                        .value = self.timeline_value,
                        .stage_mask = .{ .all_commands_bit = true },
                        .device_index = 0,
                    }
                },
            })[0..1], frame.fence);

            res = self.graphics.dev.queuePresentKHR(self.graphics.graphics_queue, &.{
                .wait_semaphore_count = 1,
                .p_wait_semaphores = (&self.swapchain.images[acquired_image].semaphore)[0..1],
                .swapchain_count = 1,
                .p_swapchains = (&self.swapchain.handle)[0..1],
                .p_image_indices = (&acquired_image)[0..1],
            }) catch |err| if (err == error.OutOfDateKHR) .error_out_of_date_khr else return err;

            self.curent_frame = (self.curent_frame + 1) % frames_in_flight;

            frame.acquired_swapchain = null;
        }

        switch (res) {
            .error_out_of_date_khr, .suboptimal_khr => {},
            else => return .same,
        }

        const width, const height = self.window.getFramebufferSize();
        try self.swapchain.recreate(alloc, &self.graphics, .{
            .width = @intCast(width),
            .height = @intCast(height),
        });
        return .recreated;
    }
};

const Frame = struct {
    cmd_pool: vk.CommandPool,
    cmd_buf: vk.CommandBuffer,

    semaphore: vk.Semaphore,
    fence: vk.Fence,
    acquired_swapchain: ?u32,

    render_image: vk.Image,
    render_view: vk.ImageView,
    alloc: vma.VmaAllocation,

    pub fn init(gc: *const GraphicsCtx) !Frame {
        const cmd_pool = try gc.dev.createCommandPool(&.{
            .flags = .{ .reset_command_buffer_bit = true },
            .queue_family_index = gc.graphics_family,
        }, null);

        var cmd_buf: vk.CommandBuffer = undefined;
        try gc.dev.allocateCommandBuffers(&.{
            .command_pool = cmd_pool,
            .command_buffer_count = 1,
            .level = .primary,
        }, (&cmd_buf)[0..1]);

        const semaphore = try gc.dev.createSemaphore(&.{}, null);

        const fence = try gc.dev.createFence(&.{
            .flags = .{ .signaled_bit = true },
        }, null);

        return .{
            .cmd_pool = cmd_pool,
            .cmd_buf = cmd_buf,
            .semaphore = semaphore,
            .fence = fence,
            .acquired_swapchain = null,
            .render_image = .null_handle,
            .render_view = .null_handle,
            .alloc = null,
        };
    }

    pub fn initRenderTexture(self: *Frame, gc: *const GraphicsCtx, extent: vk.Extent2D, format: vk.Format) !void {
        const res = vma.vmaCreateImage(gc.vma_alloc, @ptrCast(&vk.ImageCreateInfo{
            .image_type = .@"2d",
            .format = format,
            .extent = .{ .width = extent.width, .height = extent.height, .depth = 1 },
            .mip_levels = 1,
            .array_layers = 1,
            .samples = .{ .@"1_bit" = true },
            .tiling = .optimal,
            .usage = .{ .color_attachment_bit = true, .transfer_src_bit = true, .transfer_dst_bit = true },
            .sharing_mode = .exclusive,
            .initial_layout = .undefined,
        }), &vma.VmaAllocationCreateInfo{
            .usage = vma.VMA_MEMORY_USAGE_GPU_ONLY,
        }, @ptrCast(&self.render_image), &self.alloc, null);
        errdefer vma.vmaDestroyImage(gc.vma_alloc, @ptrFromInt(@intFromEnum(self.render_image)), self.alloc);
        if (res < 0) return error.VmaImageError;

        self.render_view = try gc.dev.createImageView(&.{
            .image = self.render_image,
            .view_type = .@"2d",
            .format = format,
            .components = .{ .r = .identity, .g = .identity, .b = .identity, .a = .identity },
            .subresource_range = .{
                .aspect_mask = .{ .color_bit = true },
                .base_mip_level = 0,
                .level_count = 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
        }, null);
    }

    pub fn deinitRenderTexture(self: *Frame, gc: *const GraphicsCtx) void {
        if (self.render_image != .null_handle) {
            gc.dev.destroyImageView(self.render_view, null);
            vma.vmaDestroyImage(gc.vma_alloc, @ptrFromInt(@intFromEnum(self.render_image)), self.alloc);

            self.render_image = .null_handle;
            self.render_view = .null_handle;
            self.alloc = null;
        }
    }

    pub fn deinit(self: Frame, gc: *const GraphicsCtx) void {
        if (self.render_image != .null_handle) {
            gc.dev.destroyImageView(self.render_view, null);
            vma.vmaDestroyImage(gc.vma_alloc, @ptrFromInt(@intFromEnum(self.render_image)), self.alloc);
        }
        gc.dev.destroyCommandPool(self.cmd_pool, null);
        gc.dev.destroySemaphore(self.semaphore, null);
        gc.dev.destroyFence(self.fence, null);
    }
};

const Swapchain = struct {
    const SwapImage = struct {
        image: vk.Image,
        view: vk.ImageView,
        semaphore: vk.Semaphore,

        pub fn init(gc: *const GraphicsCtx, img: vk.Image, format: vk.Format) !SwapImage {
            const view = try gc.dev.createImageView(&.{
                .image = img,
                .view_type = .@"2d",
                .format = format,
                .components = .{ .r = .identity, .g = .identity, .b = .identity, .a = .identity },
                .subresource_range = .{
                    .aspect_mask = .{ .color_bit = true },
                    .base_mip_level = 0,
                    .level_count = 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
            }, null);
            errdefer gc.dev.destroyImageView(view, null);

            const semaphore = try gc.dev.createSemaphore(&vk.SemaphoreCreateInfo{}, null);

            return .{
                .image = img,
                .view = view,
                .semaphore = semaphore,
            };
        }

        pub fn deinit(self: SwapImage, gc: *const GraphicsCtx) void {
            gc.dev.destroyImageView(self.view, null);
            gc.dev.destroySemaphore(self.semaphore, null);
        }
    };

    handle: vk.SwapchainKHR,
    format: vk.Format,
    present_mode: vk.PresentModeKHR,
    extent: vk.Extent2D,
    images: []SwapImage,

    pub fn init(alloc: Allocator, gc: *const GraphicsCtx, extent: vk.Extent2D) !Swapchain {
        return try initRecycle(alloc, gc, extent, .null_handle);
    }

    fn initRecycle(alloc: Allocator, gc: *const GraphicsCtx, extent: vk.Extent2D, old_handle: vk.SwapchainKHR) !Swapchain {
        const caps = try gc.instance.getPhysicalDeviceSurfaceCapabilitiesKHR(gc.pdev, gc.surface);
        const actual_extent = if (caps.current_extent.width != 0xFFFF_FFFF) caps.current_extent else vk.Extent2D{
            .width = std.math.clamp(extent.width, caps.min_image_extent.width, caps.max_image_extent.width),
            .height = std.math.clamp(extent.height, caps.min_image_extent.height, caps.max_image_extent.height),
        };

        if (actual_extent.width == 0 or actual_extent.height == 0) {
            return error.InvalidSurfaceDimensions;
        }

        const preferred_format = vk.SurfaceFormatKHR{
            .format = .b8g8r8_srgb,
            .color_space = .srgb_nonlinear_khr,
        };
        const surface_formats = try gc.instance.getPhysicalDeviceSurfaceFormatsAllocKHR(gc.pdev, gc.surface, alloc);
        defer alloc.free(surface_formats);

        const surface_format = for (surface_formats) |sf| {
            if (std.meta.eql(sf, preferred_format)) {
                break preferred_format;
            }
        } else surface_formats[0];

        const present_modes = try gc.instance.getPhysicalDeviceSurfacePresentModesAllocKHR(gc.pdev, gc.surface, alloc);
        defer alloc.free(present_modes);

        const preferred_modes = [_]vk.PresentModeKHR{
            .mailbox_khr,
            .immediate_khr,
        };

        const present_mode: vk.PresentModeKHR = for (preferred_modes) |mode| {
            if (std.mem.indexOfScalar(vk.PresentModeKHR, present_modes, mode) != null) {
                break mode;
            }
        } else .fifo_khr;

        var image_count = caps.min_image_count + 1;
        if (caps.max_image_count > 0) {
            image_count = @min(image_count, caps.max_image_count);
        }

        const handle = gc.dev.createSwapchainKHR(&.{
            .surface = gc.surface,
            .min_image_count = image_count,
            .image_format = surface_format.format,
            .image_color_space = surface_format.color_space,
            .image_extent = actual_extent,
            .image_array_layers = 1,
            .image_usage = .{ .transfer_dst_bit = true, .color_attachment_bit = true },
            .image_sharing_mode = .exclusive,
            .pre_transform = caps.current_transform,
            .composite_alpha = .{ .opaque_bit_khr = true },
            .present_mode = present_mode,
            .clipped = .true,
            .old_swapchain = old_handle,
        }, null) catch return error.SwapchainCreationFailed;
        errdefer gc.dev.destroySwapchainKHR(handle, null);

        if (old_handle != .null_handle) {
            gc.dev.destroySwapchainKHR(old_handle, null);
        }

        const images = try gc.dev.getSwapchainImagesAllocKHR(handle, alloc);
        defer alloc.free(images);

        const swap_images = try alloc.alloc(SwapImage, images.len);
        errdefer alloc.free(swap_images);

        var i: usize = 0;
        errdefer for (swap_images[0..i]) |si| si.deinit(gc);

        for (images) |image| {
            swap_images[i] = try SwapImage.init(gc, image, surface_format.format);
            i += 1;
        }

        return .{
            .handle = handle,
            .format = surface_format.format,
            .present_mode = present_mode,
            .extent = actual_extent,
            .images = swap_images,
        };
    }

    pub fn deinitExceptSwapchain(self: Swapchain, gc: *const GraphicsCtx, alloc: Allocator) void {
        for (self.images) |image| {
            image.deinit(gc);
        }
        alloc.free(self.images);
    }

    pub fn deinit(self: Swapchain, gc: *const GraphicsCtx, alloc: Allocator) void {
        self.deinitExceptSwapchain(gc, alloc);
        gc.dev.destroySwapchainKHR(self.handle, null);
    }

    pub fn recreate(self: *Swapchain, alloc: Allocator, gc: *const GraphicsCtx, extent: vk.Extent2D) !void {
        try gc.dev.queueWaitIdle(gc.graphics_queue);

        self.deinitExceptSwapchain(gc, alloc);

        self.* = try initRecycle(alloc, gc, extent, self.handle);
    }
};
