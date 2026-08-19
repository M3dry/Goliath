const std = @import("std");
const root = @import("../root.zig");
const Ctx = root.Ctx;

pub const Capability = enum {
    window, destroy_queue, descriptor_pool,
    blit_strategy, render_extent, render_format,
    vkb, instance,
    device, pdevice, pdevice_props,
    swapchain_handle, swapchain_format, swapchain_present_mode, swapchain_extent, swapchain_images, 
    vma_allocator,
    graphics_family, graphics_queue,
    transport, transport_family, transport_queue, dedicated_transport,
    surface,
    current_frame, frame_cmd_pool, frame_cmd_buf, frame_semaphore, frame_fence,
    frame_acquired_swapchain, frame_render_target, frame_render_target_view,
    frame_depth_target, frame_depth_target_view,

    pub fn typeInfo(self: Capability) struct { []const u8, type } {
        return switch (self) {
            .window, => .{ "window", *root.zglfw.Window },
            .destroy_queue, => .{ "destroy_queue", *root.DestroyQueue },
            .descriptor_pool, => .{ "descriptor_pool", *root.DescriptorPool },
            .blit_strategy, => .{ "blit_strategy", root.BlitStrategy },
            .render_extent, => .{ "render_extent", root.vk.Extent2D },
            .render_format, => .{ "render_format", root.vk.Format },
            .vkb, => .{ "vkb", root.vk.BaseWrapper },
            .instance, => .{ "instance", root.vk.InstanceProxy },
            .device, => .{ "device", root.vk.DeviceProxy },
            .pdevice, => .{ "pdevice", root.vk.PhysicalDevice },
            .pdevice_props, => .{ "pdevice_props", *const root.vk.PhysicalDeviceProperties },
            .swapchain_handle, => .{ "swapchain_handle", root.vk.SwapchainKHR },
            .swapchain_format, => .{ "swapchain_format", root.vk.Format },
            .swapchain_present_mode, => .{ "swapchain_present_mode", root.vk.PresentModeKHR },
            .swapchain_extent, => .{ "swapchain_extent", root.vk.Extent2D },
            .swapchain_images, => .{ "swapchain_images", []root.Swapchain.SwapImage },
            .vma_allocator, => .{ "vma_allocator", root.vma.VmaAllocator },
            .graphics_family, => .{ "graphics_family", u32 },
            .graphics_queue, => .{ "graphics_queue", root.vk.Queue },
            .transport => .{ "transport", *root.Transport },
            .transport_family, => .{ "transport_family", u32 },
            .transport_queue, => .{ "transport_queue", root.vk.Queue },
            .dedicated_transport, => .{ "dedicated_transport", bool },
            .surface, => .{ "surface", root.vk.SurfaceKHR },
            .current_frame, => .{ "current_frame", u32 },
            .frame_cmd_pool, => .{ "frame_cmd_pool", root.vk.CommandPool },
            .frame_cmd_buf, => .{ "frame_cmd_buf", root.vk.CommandBuffer },
            .frame_semaphore, => .{ "frame_semaphore", root.vk.Semaphore },
            .frame_fence, => .{ "frame_fence", root.vk.Fence },
            .frame_acquired_swapchain, => .{ "frame_acquired_swapchain", ?u32 },
            .frame_render_target, => .{ "frame_render_target", root.Image2D },
            .frame_render_target_view, => .{ "frame_render_target_view", root.ImageView },
            .frame_depth_target, => .{ "frame_depth_target", root.Image2D },
            .frame_depth_target_view, => .{ "frame_depth_target_view", root.ImageView },
        };
    }

    pub fn access(comptime self: Capability, ctx: *Ctx) typeInfo(self).@"1" {
        return switch (self) {
            .window, => ctx.window,
            .destroy_queue, => &ctx.destroy_queue,
            .descriptor_pool, => ctx.descriptorPool(),
            .blit_strategy, => ctx.blit_strategy,
            .render_extent, => ctx.render_extent,
            .render_format, => ctx.render_extent,
            .vkb => ctx.graphics.vkb,
            .instance, => ctx.graphics.instance,
            .device, => ctx.graphics.dev,
            .pdevice, => ctx.graphics.pdev,
            .pdevice_props, => &ctx.graphics.props,
            .swapchain_handle, => ctx.swapchain.handle,
            .swapchain_format, => ctx.swapchain.format,
            .swapchain_present_mode, => ctx.swapchain.present_mode,
            .swapchain_extent, => ctx.swapchain.extent,
            .swapchain_images, => ctx.swapchain.images,
            .vma_allocator, => ctx.graphics.vma_alloc,
            .graphics_family, => ctx.graphics.graphics_family,
            .graphics_queue, => ctx.graphics.graphics_queue,
            .transport => &ctx.transport,
            .transport_family, => ctx.graphics.transport_family,
            .transport_queue, => ctx.graphics.transport_queue,
            .dedicated_transport, => ctx.graphics.has_dedicated_transport,
            .surface, => ctx.graphics.surface,
            .current_frame, => ctx.current_frame,
            .frame_cmd_pool, => ctx.currentFrame().cmd_pool,
            .frame_cmd_buf, => ctx.currentFrame().cmd_buf,
            .frame_semaphore, => ctx.currentFrame().semaphore,
            .frame_fence, => ctx.currentFrame().fence,
            .frame_acquired_swapchain, => ctx.currentFrame().acquired_swapchain,
            .frame_render_target, => ctx.currentFrame().render_target,
            .frame_render_target_view, => ctx.currentFrame().render_target_view,
            .frame_depth_target, => ctx.currentFrame().depth_target,
            .frame_depth_target_view, => ctx.currentFrame().depth_target_view,
        };
    }
};

pub fn Query(comptime cs: []const Capability) type {
    var field_names: [cs.len][]const u8 = undefined;
    var field_types: [cs.len]type = undefined;
    var field_attrs: [cs.len]std.builtin.Type.StructField.Attributes = undefined;

    for (cs, 0..) |cap, i| {
        const name, const t = cap.typeInfo();
        field_names[i] = name;
        field_types[i] = t;
        field_attrs[i] = .{};
    }

    const ViewType = @Struct(.auto, null, &field_names, &field_types, &field_attrs);
    return struct {
        pub const View = ViewType;
        pub const caps: []const Capability = cs;
        const Self = @This();

        view: View,

        pub fn from(v: anytype) Self {
            const v_type = @TypeOf(v);
            if (v_type == Ctx) @compileError("Ctx needs to be passed in as a pointer");

            if (v_type == *Ctx) {
                return fromCtx(v);
            }

            return fromQuery(v);
        }

        pub fn fromCtx(ctx: *Ctx) Self {
            var view: View = undefined;
            inline for (caps) |cap| {
                const name, _ = cap.typeInfo();
                @field(view, name) = cap.access(ctx);
            }

            return .{
                .view = view,
            };
        }

        pub fn fromQuery(q: anytype) Self {
            const q_caps = @TypeOf(q).caps;
            comptime {
                var not_found: []const Capability = &.{};

                for (Self.caps) |cap| {
                    var found = false;
                    for (q_caps) |q_cap| {
                        if (cap == q_cap) found = true;
                    }

                    if (!found) not_found = not_found ++ &[_]Capability{ cap };
                }

                if (not_found.len != 0) @compileError(std.fmt.comptimePrint("Query doesn't include {any}", .{not_found}));
            }

            var view: View = undefined;
            inline for (caps) |cap | {
                const name, _ = cap.typeInfo();
                @field(view,  name) = @field(q.view, name);
            }

            return .{
                .view = view,
            };
        }
    };
}
