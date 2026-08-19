const std = @import("std");
const zgui = @import("zgui");
const root = @import("root.zig");
const vk = root.vk;
const Ctx = root.Ctx;

const Allocator = std.mem.Allocator;
const util = @import("util.zig");

const Self = @This();

var vk_loader_instance: vk.Instance = undefined;
var vk_loader_get_proc: vk.PfnGetInstanceProcAddr = undefined;

descriptor_pool: vk.DescriptorPool,
api_version: u32,
enabled: bool = true,

pub fn init(allocator: Allocator, ctx: Ctx.Query(&.{ .device, .instance, .vkb, .swapchain_format, .pdevice, .graphics_queue, .graphics_family, .window })) !Self {
    const dev = ctx.view.device;
    const pool = try createDescriptorPool(dev);

    const api_version = vk.makeApiVersion(0, 1, 3, 0).toU32();

    zgui.init(allocator);

    {
        vk_loader_instance = ctx.view.instance.handle;
        vk_loader_get_proc = ctx.view.vkb.dispatch.vkGetInstanceProcAddr.?;
        if (!zgui.backend.loadFunctions(api_version, vkLoader, null)) {
            return error.VulkanFunctionLoadError;
        }
    }

    const color_attachment_formats = [_]c_int{@intFromEnum(ctx.view.swapchain_format)};
    zgui.backend.init(
        .{
            .api_version = api_version,
            .instance = @ptrFromInt(@intFromEnum(ctx.view.instance.handle)),
            .physical_device = @ptrFromInt(@intFromEnum(ctx.view.pdevice)),
            .device = @ptrFromInt(@intFromEnum(dev.handle)),
            .queue_family = ctx.view.graphics_family,
            .queue = @ptrFromInt(@intFromEnum(ctx.view.graphics_queue)),
            .descriptor_pool = @ptrFromInt(@intFromEnum(pool)),
            .render_pass = null,
            .min_image_count = @intCast(Ctx.frames_in_flight),
            .image_count = @intCast(Ctx.frames_in_flight),
            .use_dynamic_rendering = true,
            .pipeline_rendering_create_info = .{
                .s_type = @intFromEnum(vk.StructureType.pipeline_rendering_create_info),
                .color_attachment_count = 1,
                .p_color_attachment_formats = color_attachment_formats[0..].ptr,
            },
        },
        ctx.view.window,
    );

    zgui.io.setConfigFlags(.{ .nav_enable_keyboard = true, .dock_enable = true });
    zgui.io.setDisplayFramebufferScale(1.0, 1.0);

    return .{
        .descriptor_pool = pool,
        .api_version = api_version,
    };
}

pub fn deinit(self: *Self, ctx: Ctx.Query(&.{ .device })) void {
    zgui.backend.deinit();
    zgui.deinit();
    ctx.view.device.destroyDescriptorPool(self.descriptor_pool, null);
}

pub fn enable(self: *Self, v: bool) void {
    self.enabled = v;
    if (!v) zgui.setWindowFocus(null);
}

pub fn newFrame(self: *Self, dt: f32, fb_width: u32, fb_height: u32) void {
    if (!self.enabled) {
        zgui.io.addMousePositionEvent(-std.math.floatMax(f32), -std.math.floatMax(f32));
    }
    zgui.io.setDeltaTime(dt);
    zgui.backend.newFrame(fb_width, fb_height);
    if (!self.enabled) {
        zgui.setNextFrameWantCaptureKeyboard(false);
        zgui.setNextFrameWantCaptureMouse(false);
    }
}

pub fn render(self: *Self, ctx: Ctx.Query(&.{ .device, .graphics_family, .frame_acquired_swapchain, .swapchain_extent, .swapchain_images }), cmd_buf: root.vk.CommandBuffer) void {
    _ = self;
    const dev = ctx.view.device;
    const qf = ctx.view.graphics_family;
    const acquired = ctx.view.frame_acquired_swapchain orelse return;
    const swap = ctx.view.swapchain_images[acquired];
    const extent = ctx.view.swapchain_extent;

    {
        const barrier = vk.ImageMemoryBarrier2{
            .src_stage_mask = .{ .all_transfer_bit = true },
            .src_access_mask = .{ .transfer_write_bit = true },
            .dst_stage_mask = .{ .color_attachment_output_bit = true },
            .dst_access_mask = .{ .color_attachment_write_bit = true, .color_attachment_read_bit = true },
            .old_layout = .transfer_dst_optimal,
            .new_layout = .color_attachment_optimal,
            .src_queue_family_index = qf,
            .dst_queue_family_index = qf,
            .subresource_range = util.fullRange(.{ .color_bit = true }),
            .image = swap.image,
        };
        dev.cmdPipelineBarrier2(cmd_buf, &.{
            .image_memory_barrier_count = 1,
            .p_image_memory_barriers = (&barrier)[0..1],
        });
    }

    {
        const color_attachment = vk.RenderingAttachmentInfo{
            .image_view = swap.view,
            .image_layout = .color_attachment_optimal,
            .resolve_mode = .{},
            .resolve_image_layout = .undefined,
            .load_op = .load,
            .store_op = .store,
            .clear_value = undefined,
        };
        const rendering_info = vk.RenderingInfo{
            .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = extent },
            .layer_count = 1,
            .view_mask = 0,
            .color_attachment_count = 1,
            .p_color_attachments = (&color_attachment)[0..1],
        };
        dev.cmdBeginRendering(cmd_buf, &rendering_info);
    }

    zgui.backend.render(@ptrFromInt(@intFromEnum(cmd_buf)));

    dev.cmdEndRendering(cmd_buf);

    {
        const barrier = vk.ImageMemoryBarrier2{
            .src_stage_mask = .{ .color_attachment_output_bit = true },
            .src_access_mask = .{ .color_attachment_write_bit = true, .color_attachment_read_bit = true },
            .dst_stage_mask = .{ .all_commands_bit = true },
            .dst_access_mask = .{ .memory_read_bit = true },
            .old_layout = .color_attachment_optimal,
            .new_layout = .present_src_khr,
            .src_queue_family_index = qf,
            .dst_queue_family_index = qf,
            .subresource_range = util.fullRange(.{ .color_bit = true }),
            .image = swap.image,
        };
        dev.cmdPipelineBarrier2(cmd_buf, &.{
            .image_memory_barrier_count = 1,
            .p_image_memory_barriers = (&barrier)[0..1],
        });
    }
}

pub fn wantCaptureKeyboard(self: *const Self) bool {
    _ = self;
    return zgui.io.getWantCaptureKeyboard();
}

pub fn wantCaptureMouse(self: *const Self) bool {
    _ = self;
    return zgui.io.getWantCaptureMouse();
}

fn vkLoader(name: [*:0]const u8, _: ?*anyopaque) callconv(.c) ?*anyopaque {
    const fn_ptr = vk_loader_get_proc(vk_loader_instance, name);
    return if (fn_ptr) |f| @as(?*anyopaque, @ptrCast(@constCast(f))) else null;
}

fn createDescriptorPool(dev: anytype) !vk.DescriptorPool {
    const pool_sizes = [_]vk.DescriptorPoolSize{
        .{ .type = .combined_image_sampler, .descriptor_count = 1 },
    };
    return try dev.createDescriptorPool(&.{
        .flags = .{ .free_descriptor_set_bit = true },
        .max_sets = 1,
        .pool_size_count = @intCast(pool_sizes.len),
        .p_pool_sizes = &pool_sizes,
    }, null);
}
