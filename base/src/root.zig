pub const vk = @import("vulkan");
pub const zglfw = @import("zglfw");
pub const zgui = @import("zgui");
pub const zmath = @import("zmath");
pub const zprobe = @import("zprobe");

pub const timing = @import("timing.zig");
pub const image_loader = @import("image_loader.zig");
pub const layout = @import("layout.zig");
pub const push_constant = @import("push_constant.zig");
pub const util = @import("util.zig");

pub const GraphicsCtx = @import("GraphicsCtx.zig");
pub const Input = @import("Input.zig");
pub const Imgui = @import("Imgui.zig");
pub const Buffer = @import("Buffer.zig");
pub const DescriptorPool = @import("DescriptorPool.zig");
pub const TexturePool = @import("TexturePool.zig");
pub const DestroyQueue = @import("DestroyQueue.zig");
pub const ShaderModule = @import("Shader.zig");
pub const GraphicsPipeline = @import("GraphicsPipeline.zig");
pub const ComputePipeline = @import("ComputePipeline.zig");
pub const Sampler = @import("Sampler.zig");
pub const Transport = @import("Transport.zig");
pub const Camera = @import("Camera.zig");
pub const RenderGraph = @import("RenderGraph.zig");

pub const Image2D = @import("image.zig").Image2D;
pub const ImageView = @import("image.zig").ImageView;

const std = @import("std");
pub const vma = @import("vma.zig").vma;

const Allocator = std.mem.Allocator;

pub const BlitStrategy = enum {
    letterbox,
    stretch,
};

pub const Ctx = struct {
    pub const frames_in_flight: u32 = 2;
    pub const depth_texture: vk.Format = .d32_sfloat;

    window: *zglfw.Window,
    graphics: GraphicsCtx,
    transport: Transport,
    frames: []Frame,
    swapchain: Swapchain,
    current_frame: u32 = 0,

    blit_strategy: BlitStrategy,
    render_extent: vk.Extent2D,
    render_format: vk.Format,

    destroy_queue: DestroyQueue,

    timeline_semaphore: vk.Semaphore,
    timeline_value: u64,

    descriptor_pools: [frames_in_flight]DescriptorPool,

    pub const query = @import("Ctx/query.zig");
    pub const Query = query.Query;

    pub const Opts = struct {
        pub const Size = union(enum) {
            dims: struct { u32, u32 },
            fullscreen,
        };

        name: [*:0]const u8,

        size: Size = .{ .dims = .{ 0, 0 } },
        resizable: bool = false,

        render_extent: vk.Extent2D = .{ .width = 1920, .height = 1080 },
        blit_strategy: BlitStrategy = .stretch,
        render_format: vk.Format,
    };

    // TODO: investiage how to fix this, issue is that currently `Transport`'s mutexes can't change memory location
    /// Ctx can't move in memory location
    pub fn init(ctx: *Ctx, alloc: Allocator, io: std.Io, name: [:0]const u8, window_opts: Opts) !void {
        ctx.current_frame = 0;

        try zglfw.init();

        if (!zglfw.isVulkanSupported()) return error.NoVulkan;

        zglfw.windowHint(.client_api, .no_api);
        zglfw.windowHint(.resizable, window_opts.resizable);
        zglfw.windowHint(.auto_iconify, false);

        const mon_width, const mon_height = if (zglfw.getPrimaryMonitor()) |mon| blk: {
            const mode = mon.getVideoMode() catch break :blk .{ 0, 0 };
            break :blk .{ mode.width, mode.height };
        } else .{ 0, 0 };

        var width: c_int, var height: c_int = switch (window_opts.size) {
            .dims => |dims| .{ @intCast(dims.@"0"), @intCast(dims.@"1") },
            .fullscreen => .{ mon_width, mon_height },
        };

        ctx.window = try zglfw.createWindow(width, height, name, null, null);

        zglfw.getFramebufferSize(ctx.window, &width, &height);
        const extent: vk.Extent2D = .{
            .width = @intCast(width),
            .height = @intCast(height),
        };

       ctx.graphics = try GraphicsCtx.init(alloc, ctx.window, window_opts.name);

        try ctx.transport.init(.from(ctx), alloc, io);
        errdefer ctx.transport.deinit(.from(ctx));

        ctx.frames = try alloc.alloc(Frame, frames_in_flight);
        errdefer alloc.free(ctx.frames);

        {
            var i: usize = 0;
            var rt_idx: usize = 0;
            var depth_idx: usize = 0;
            errdefer for (ctx.frames[0..i]) |*frame| frame.deinit(.from(ctx));
            errdefer for (ctx.frames[0..rt_idx]) |*f| f.deinitRenderTexture(.from(ctx));
            errdefer for (ctx.frames[0..depth_idx]) |*f| f.deinitDepthTexture(.from(ctx));

            for (ctx.frames) |_| {
                ctx.frames[i] = try Frame.init(.from(ctx));
                i += 1;

                try ctx.frames[rt_idx].initRenderTexture(.from(ctx), window_opts.render_extent, window_opts.render_format);
                rt_idx += 1;

                try ctx.frames[depth_idx].initDepthTexture(.from(ctx), window_opts.render_extent);
                depth_idx += 1;
            }
        }

        ctx.swapchain = try Swapchain.init(alloc, .from(ctx), extent);

        ctx.timeline_value = 0;
        ctx.timeline_semaphore = try ctx.graphics.dev.createSemaphore(&vk.SemaphoreCreateInfo{
            .p_next = &vk.SemaphoreTypeCreateInfo{
                .initial_value = ctx.timeline_value,
                .semaphore_type = .timeline,
            },
        }, null);

        {
            var i: usize = 0;
            errdefer for (ctx.descriptor_pools[0..i]) |*pool| pool.deinit(.from(ctx), alloc);

            for (ctx.descriptor_pools) |_| {
                ctx.descriptor_pools[i] = try DescriptorPool.init(.from(ctx), alloc);
                i += 1;
            }
        }

        ctx.blit_strategy = window_opts.blit_strategy;
        ctx.render_extent = window_opts.render_extent;
        ctx.render_format = window_opts.render_format;
        ctx.destroy_queue = .init(alloc, 0);
    }

    pub fn deinit(self: *Ctx, alloc: Allocator) void {
        self.transport.deinit(.from(self));

        for (&self.descriptor_pools) |*pool| {
            pool.deinit(.from(self), alloc);
        }

        for (self.frames) |*frame| {
            frame.deinit(.from(self));
        }
        alloc.free(self.frames);

        self.destroy_queue.deinit(.from(self));

        self.graphics.dev.destroySemaphore(self.timeline_semaphore, null);

        self.swapchain.deinit(.from(self), alloc);
        self.graphics.deinit(alloc);

        self.window.destroy();
        zglfw.terminate();
    }

    pub fn renderTarget(self: *Ctx) struct { image: vk.Image, view: vk.ImageView } {
        const frame = self.currentFrame();
        return .{ .image = frame.render_target.handle, .view = frame.render_target_view.handle };
    }

    pub fn depthTarget(self: *Ctx) struct { image: vk.Image, view: vk.ImageView } {
        const frame = self.currentFrame();
        return .{ .image = frame.depth_target.handle, .view = frame.depth_target_view.handle };
    }

    pub fn descriptorPool(self: *Ctx) *DescriptorPool {
        return &self.descriptor_pools[self.current_frame];
    }

    pub fn currentFrame(self: *Ctx) *Frame {
        return &self.frames[self.current_frame];
    }

    pub const PrepareResult = enum {
        skip_drawing,
        success,
    };

    pub fn prepare_frame(self: *Ctx) !PrepareResult {
        const frame = &self.frames[self.current_frame];
        _ = try self.graphics.dev.waitForFences(&[_]vk.Fence{frame.fence}, .true, std.math.maxInt(u64));
        self.destroy_queue.flush(.from(self));
        try self.graphics.dev.resetFences(&[_]vk.Fence{frame.fence});

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
        try self.descriptor_pools[self.current_frame].clear(.from(self));
        return .success;
    }

    pub fn prepare_draw(self: Ctx) !void {
        const frame = self.frames[self.current_frame];
        std.debug.assert(frame.acquired_swapchain != null);

        try self.graphics.dev.resetCommandBuffer(frame.cmd_buf, .{});

        try self.graphics.dev.beginCommandBuffer(frame.cmd_buf, &.{
            .flags = .{
                .one_time_submit_bit = true,
            },
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
                .subresource_range = util.fullRange(.{ .color_bit = true }),
                .image = frame.render_target.handle,
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
                .subresource_range = util.fullRange(.{ .color_bit = true }),
                .image = self.swapchain.images[frame.acquired_swapchain.?].image,
            },
            .{
                .src_stage_mask = .{ .all_commands_bit = true },
                .src_access_mask = .{ .memory_write_bit = true },
                .dst_stage_mask = .{ .early_fragment_tests_bit = true, .late_fragment_tests_bit = true },
                .dst_access_mask = .{ .depth_stencil_attachment_read_bit = true, .depth_stencil_attachment_write_bit = true },
                .old_layout = .undefined,
                .new_layout = .depth_stencil_attachment_optimal,
                .src_queue_family_index = self.graphics.graphics_family,
                .dst_queue_family_index = self.graphics.graphics_family,
                .subresource_range = util.fullRange(.{ .depth_bit = true }),
                .image = frame.depth_target.handle,
            },
        };
        self.graphics.dev.cmdPipelineBarrier2(frame.cmd_buf, &.{
            .image_memory_barrier_count = 3,
            .p_image_memory_barriers = &barriers,
        });
    }

    pub const SwapchainState = enum {
        recreated,
        same,
    };

    pub fn end_drawing(self: *Ctx) void {
        const frame = &self.frames[self.current_frame];
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
                &[_]vk.ImageSubresourceRange{util.fullRange(.{ .color_bit = true })},
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
                    .subresource_range = util.fullRange(.{ .color_bit = true }),
                    .image = self.swapchain.images[acquired_image].image,
                })[0..1],
            });
        }

        self.graphics.dev.cmdBlitImage(
            frame.cmd_buf,
            frame.render_target.handle,
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
        const frame = &self.frames[self.current_frame];

        var res: vk.Result = .error_out_of_date_khr;
        if (frame.acquired_swapchain) |acquired_image| {
            try self.graphics.dev.endCommandBuffer(frame.cmd_buf);

            self.timeline_value += 1;

            // graphics queue is externally synchronized; worker-side ownership
            // handoffs submit through the same lock
            self.transport.graphics_submit_lock.lockUncancelable(self.transport.io);
            defer self.transport.graphics_submit_lock.unlock(self.transport.io);

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
                .p_signal_semaphore_infos = &[_]vk.SemaphoreSubmitInfo{ .{
                    .semaphore = self.swapchain.images[acquired_image].semaphore,
                    .value = 0,
                    .stage_mask = .{ .all_commands_bit = true },
                    .device_index = 0,
                }, .{
                    .semaphore = self.timeline_semaphore,
                    .value = self.timeline_value,
                    .stage_mask = .{ .all_commands_bit = true },
                    .device_index = 0,
                } },
            })[0..1], frame.fence);

            res = self.graphics.dev.queuePresentKHR(self.graphics.graphics_queue, &.{
                .wait_semaphore_count = 1,
                .p_wait_semaphores = (&self.swapchain.images[acquired_image].semaphore)[0..1],
                .swapchain_count = 1,
                .p_swapchains = (&self.swapchain.handle)[0..1],
                .p_image_indices = (&acquired_image)[0..1],
            }) catch |err| if (err == error.OutOfDateKHR) .error_out_of_date_khr else return err;

            self.current_frame = (self.current_frame + 1) % frames_in_flight;
            self.destroy_queue.update_current_frame(self.current_frame);

            frame.acquired_swapchain = null;
        }

        switch (res) {
            .error_out_of_date_khr, .suboptimal_khr => {},
            else => return .same,
        }

        const width, const height = self.window.getFramebufferSize();
        try self.swapchain.recreate(alloc, .from(self), .{
            .width = @intCast(width),
            .height = @intCast(height),
        });
        return .recreated;
    }
};

pub const Frame = struct {
    cmd_pool: vk.CommandPool,
    cmd_buf: vk.CommandBuffer,

    semaphore: vk.Semaphore,
    fence: vk.Fence,
    acquired_swapchain: ?u32,

    render_target: Image2D = .{},
    render_target_view: ImageView = .{},
    depth_target: Image2D = .{},
    depth_target_view: ImageView = .{},

    pub fn init(ctx: Ctx.Query(&.{ .device, .graphics_family })) !Frame {
        const cmd_pool = try ctx.view.device.createCommandPool(&.{
            .flags = .{ .reset_command_buffer_bit = true },
            .queue_family_index = ctx.view.graphics_family,
        }, null);

        var cmd_buf: vk.CommandBuffer = undefined;
        try ctx.view.device.allocateCommandBuffers(&.{
            .command_pool = cmd_pool,
            .command_buffer_count = 1,
            .level = .primary,
        }, (&cmd_buf)[0..1]);

        const semaphore = try ctx.view.device.createSemaphore(&.{}, null);

        const fence = try ctx.view.device.createFence(&.{
            .flags = .{ .signaled_bit = true },
        }, null);

        return .{
            .cmd_pool = cmd_pool,
            .cmd_buf = cmd_buf,
            .semaphore = semaphore,
            .fence = fence,
            .acquired_swapchain = null,
        };
    }

    pub fn initRenderTexture(self: *Frame, ctx: Ctx.Query(&.{ .device, .vma_allocator }), extent: vk.Extent2D, format: vk.Format) !void {
        self.render_target = try Image2D.init(ctx, "render_target", .{
            .format = format,
            .extent = extent,
            .usage = .{ .color_attachment_bit = true, .storage_bit = true, .transfer_src_bit = true, .transfer_dst_bit = true },
        });
        errdefer self.render_target.deinitNow(ctx);

        self.render_target_view = try ImageView.init(.from(ctx), .{
            .image = self.render_target.handle,
            .format = format,
            .subresource_range = .{
                .aspect_mask = .{ .color_bit = true },
                .base_mip_level = 0,
                .level_count = 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
        });
        errdefer self.render_target_view.deinitNow(.from(ctx));
    }

    pub fn deinitRenderTexture(self: *Frame, ctx: Ctx.Query(&.{ .device, .vma_allocator })) void {
        self.render_target_view.deinitNow(.from(ctx));
        self.render_target.deinitNow(ctx);
    }

    pub fn initDepthTexture(self: *Frame, ctx: Ctx.Query(&.{ .device, .vma_allocator }), extent: vk.Extent2D) !void {
        self.depth_target = try Image2D.init(ctx, "depth_target", .{
            .format = Ctx.depth_texture,
            .extent = extent,
            .usage = .{ .depth_stencil_attachment_bit = true },
        });
        errdefer self.depth_target.deinitNow(ctx);

        self.depth_target_view = try ImageView.init(.from(ctx), .{
            .image = self.depth_target.handle,
            .format = .d32_sfloat,
            .subresource_range = .{
                .aspect_mask = .{ .depth_bit = true },
                .base_mip_level = 0,
                .level_count = 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
        });
        errdefer self.depth_target_view.deinitNow(.from(ctx));
    }

    pub fn deinitDepthTexture(self: *Frame, ctx: Ctx.Query(&.{ .device, .vma_allocator })) void {
        self.depth_target_view.deinitNow(.from(ctx));
        self.depth_target.deinitNow(ctx);
    }

    pub fn deinit(self: *Frame, ctx: Ctx.Query(&.{ .device, .vma_allocator })) void {
        if (self.render_target.handle != .null_handle) {
            self.render_target_view.deinitNow(.from(ctx));
            self.render_target.deinitNow(ctx);
        }
        if (self.depth_target.handle != .null_handle) {
            self.depth_target_view.deinitNow(.from(ctx));
            self.depth_target.deinitNow(ctx);
        }
        ctx.view.device.destroyCommandPool(self.cmd_pool, null);
        ctx.view.device.destroySemaphore(self.semaphore, null);
        ctx.view.device.destroyFence(self.fence, null);
    }
};

pub const Swapchain = struct {
    pub const SwapImage = struct {
        image: vk.Image,
        view: vk.ImageView,
        semaphore: vk.Semaphore,

        pub fn init(ctx: Ctx.Query(&.{ .device }), img: vk.Image, format: vk.Format) !SwapImage {
            const view = try ctx.view.device.createImageView(&.{
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
            errdefer ctx.view.device.destroyImageView(view, null);

            const semaphore = try ctx.view.device.createSemaphore(&vk.SemaphoreCreateInfo{}, null);

            return .{
                .image = img,
                .view = view,
                .semaphore = semaphore,
            };
        }

        pub fn deinit(self: SwapImage, ctx: Ctx.Query(&.{ .device })) void {
            ctx.view.device.destroyImageView(self.view, null);
            ctx.view.device.destroySemaphore(self.semaphore, null);
        }
    };

    handle: vk.SwapchainKHR,
    format: vk.Format,
    present_mode: vk.PresentModeKHR,
    extent: vk.Extent2D,
    images: []SwapImage,

    pub fn init(alloc: Allocator, ctx: Ctx.Query(&.{ .instance, .pdevice, .surface, .device }), extent: vk.Extent2D) !Swapchain {
        return try initRecycle(alloc, ctx, extent, .null_handle);
    }

    fn initRecycle(alloc: Allocator, ctx: Ctx.Query(&.{ .instance, .pdevice, .surface, .device }), extent: vk.Extent2D, old_handle: vk.SwapchainKHR) !Swapchain {
        const caps = try ctx.view.instance.getPhysicalDeviceSurfaceCapabilitiesKHR(ctx.view.pdevice, ctx.view.surface);
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
        const surface_formats = try ctx.view.instance.getPhysicalDeviceSurfaceFormatsAllocKHR(ctx.view.pdevice, ctx.view.surface, alloc);
        defer alloc.free(surface_formats);

        const surface_format = for (surface_formats) |sf| {
            if (std.meta.eql(sf, preferred_format)) {
                break preferred_format;
            }
        } else surface_formats[0];

        const present_modes = try ctx.view.instance.getPhysicalDeviceSurfacePresentModesAllocKHR(ctx.view.pdevice, ctx.view.surface, alloc);
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

        const handle = ctx.view.device.createSwapchainKHR(&.{
            .surface = ctx.view.surface,
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
        errdefer ctx.view.device.destroySwapchainKHR(handle, null);

        if (old_handle != .null_handle) {
            ctx.view.device.destroySwapchainKHR(old_handle, null);
        }

        const images = try ctx.view.device.getSwapchainImagesAllocKHR(handle, alloc);
        defer alloc.free(images);

        const swap_images = try alloc.alloc(SwapImage, images.len);
        errdefer alloc.free(swap_images);

        var i: usize = 0;
        errdefer for (swap_images[0..i]) |si| si.deinit(.from(ctx));

        for (images) |image| {
            swap_images[i] = try SwapImage.init(.from(ctx), image, surface_format.format);
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

    pub fn deinitExceptSwapchain(self: Swapchain, ctx: Ctx.Query(&.{ .instance, .pdevice, .surface, .device }), alloc: Allocator) void {
        for (self.images) |image| {
            image.deinit(.from(ctx));
        }
        alloc.free(self.images);
    }

    pub fn deinit(self: Swapchain, ctx: Ctx.Query(&.{ .instance, .pdevice, .surface, .device }), alloc: Allocator) void {
        self.deinitExceptSwapchain(ctx, alloc);
        ctx.view.device.destroySwapchainKHR(self.handle, null);
    }

    pub fn recreate(self: *Swapchain, alloc: Allocator, ctx: Ctx.Query(&.{ .instance, .pdevice, .surface, .device, .graphics_queue }), extent: vk.Extent2D) !void {
        try ctx.view.device.queueWaitIdle(ctx.view.graphics_queue);

        self.deinitExceptSwapchain(.from(ctx), alloc);

        self.* = try initRecycle(alloc, .from(ctx), extent, self.handle);
    }
};

test {
    std.testing.refAllDecls(@This());
}
