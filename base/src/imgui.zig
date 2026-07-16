const std = @import("std");
const zgui = @import("zgui");
const vk = @import("vulkan");

const Allocator = std.mem.Allocator;
const fullRange = @import("util/subresource_range.zig").fullRange;

var vk_loader_instance: vk.Instance = undefined;
var vk_loader_get_proc: vk.PfnGetInstanceProcAddr = undefined;

pub const ImguiState = struct {
    descriptor_pool: vk.DescriptorPool,
    api_version: u32,
    enabled: bool = true,

    pub fn init(allocator: Allocator, ctx: anytype) !ImguiState {
        const dev = ctx.graphics.dev;
        const pool = try createDescriptorPool(dev);

        const api_version = vk.makeApiVersion(0, 1, 3, 0).toU32();

        zgui.init(allocator);

        {
            vk_loader_instance = ctx.graphics.instance.handle;
            vk_loader_get_proc = ctx.graphics.vkb.dispatch.vkGetInstanceProcAddr.?;
            if (!zgui.backend.loadFunctions(api_version, vkLoader, null)) {
                @panic("failed to load Vulkan functions for ImGui");
            }
        }

        const color_attachment_formats = [_]c_int{@intFromEnum(ctx.swapchain.format)};
        zgui.backend.init(
            .{
                .api_version = api_version,
                .instance = @ptrFromInt(@intFromEnum(ctx.graphics.instance.handle)),
                .physical_device = @ptrFromInt(@intFromEnum(ctx.graphics.pdev)),
                .device = @ptrFromInt(@intFromEnum(dev.handle)),
                .queue_family = ctx.graphics.graphics_family,
                .queue = @ptrFromInt(@intFromEnum(ctx.graphics.graphics_queue)),
                .descriptor_pool = @ptrFromInt(@intFromEnum(pool)),
                .render_pass = null,
                .min_image_count = @intCast(ctx.frames.len),
                .image_count = @intCast(ctx.frames.len),
                .use_dynamic_rendering = true,
                .pipeline_rendering_create_info = .{
                    .s_type = @intFromEnum(vk.StructureType.pipeline_rendering_create_info),
                    .color_attachment_count = 1,
                    .p_color_attachment_formats = color_attachment_formats[0..].ptr,
                },
            },
            ctx.window,
        );

        zgui.io.setConfigFlags(.{ .nav_enable_keyboard = true, .dock_enable = true });
        zgui.io.setDisplayFramebufferScale(1.0, 1.0);

        return .{
            .descriptor_pool = pool,
            .api_version = api_version,
        };
    }

    pub fn deinit(self: *ImguiState, dev: anytype) void {
        zgui.backend.deinit();
        zgui.deinit();
        dev.destroyDescriptorPool(self.descriptor_pool, null);
    }

    pub fn enable(self: *ImguiState, v: bool) void {
        self.enabled = v;
        if (!v) zgui.setWindowFocus(null);
    }

    pub fn newFrame(self: *ImguiState, dt: f32, fb_width: u32, fb_height: u32) void {
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

    pub fn render(self: *ImguiState, ctx: anytype) void {
        _ = self;
        const frame = ctx.frames[ctx.current_frame];
        const cmd_buf = frame.cmd_buf;
        const dev = ctx.graphics.dev;
        const qf = ctx.graphics.graphics_family;
        const acquired = frame.acquired_swapchain orelse return;
        const swap = ctx.swapchain.images[acquired];
        const extent = ctx.swapchain.extent;

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
                .subresource_range = fullRange(.{ .color_bit = true }),
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
                .subresource_range = fullRange(.{ .color_bit = true }),
                .image = swap.image,
            };
            dev.cmdPipelineBarrier2(cmd_buf, &.{
                .image_memory_barrier_count = 1,
                .p_image_memory_barriers = (&barrier)[0..1],
            });
        }
    }

    pub fn wantCaptureKeyboard(self: *const ImguiState) bool {
        _ = self;
        return zgui.io.getWantCaptureKeyboard();
    }

    pub fn wantCaptureMouse(self: *const ImguiState) bool {
        _ = self;
        return zgui.io.getWantCaptureMouse();
    }
};

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
