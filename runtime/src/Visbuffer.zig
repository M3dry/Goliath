const std = @import("std");
const base = @import("base");
const zm = base.zmath;
const shaders = @import("shaders");

const Self = @This();

pub const VisPC = struct {
    vp: zm.Mat,
    renderables_address: u64,
    draw_buffer_address: u64,
};

pub const SkinnedVisPC = struct {
    vp: zm.Mat,
    renderables_address: u64,
    draw_buffer_address: u64,
    joints_address: u64,
};

extent: base.vk.Extent2D,
images: [base.Ctx.frames_in_flight]base.Image2D,
views: [base.Ctx.frames_in_flight]base.ImageView,
pipeline: base.GraphicsPipeline,
skinned_pipeline: base.GraphicsPipeline,

pub fn init(ctx: *base.Ctx, render_extent: base.vk.Extent2D) !Self {
    const vis_format: base.vk.Format = .r32_uint;

    var images: [base.Ctx.frames_in_flight]base.Image2D = undefined;
    var views: [base.Ctx.frames_in_flight]base.ImageView = undefined;

    for (0..base.Ctx.frames_in_flight) |n| {
        images[n] = try base.Image2D.init(&ctx.graphics, ctx.graphics.vma_alloc, "vis_buffer", .{
            .format = vis_format,
            .extent = render_extent,
            .usage = .{ .color_attachment_bit = true, .storage_bit = true, .transfer_dst_bit = true },
        });
        errdefer for (0..n) |i| if (i == n) break else images[i].deinit(&ctx.destroy_queue);

        views[n] = try base.ImageView.init(&ctx.graphics, .{
            .image = images[n].handle,
            .format = vis_format,
            .subresource_range = .{
                .aspect_mask = .{ .color_bit = true },
                .base_mip_level = 0,
                .level_count = 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
        });
        errdefer for (0..n) |i| if (i == n) break else images[i].deinit(&ctx.destroy_queue);
    }

    const vert_mod = try base.ShaderModule.init(ctx, shaders.get(.vertex_visbuffer_raster));
    const frag_mod = try base.ShaderModule.init(ctx, shaders.get(.fragment_visbuffer_raster));
    defer vert_mod.deinit(ctx);
    defer frag_mod.deinit(ctx);

    var pipeline = try base.GraphicsPipeline.init(ctx, .{
        .vertex = vert_mod,
        .fragment = frag_mod,
        .set_layouts = &.{},
        .color_attachments = &.{.{ .format = vis_format }},
        .depth_format = .d32_sfloat,
        .push_constant_size = @intCast(base.push_constant.size(VisPC, base.layout.scalar)),
    });
    pipeline.depth_test_enable = .true;
    pipeline.depth_write_enable = .true;
    pipeline.depth_compare_op = .less;

    const skinned_vert_mod = try base.ShaderModule.init(ctx, shaders.get(.vertex_visbuffer_raster_skinned));
    defer skinned_vert_mod.deinit(ctx);

    var skinned_pipeline = try base.GraphicsPipeline.init(ctx, .{
        .vertex = skinned_vert_mod,
        .fragment = frag_mod,
        .set_layouts = &.{},
        .color_attachments = &.{.{ .format = vis_format }},
        .depth_format = .d32_sfloat,
        .push_constant_size = @intCast(base.push_constant.size(SkinnedVisPC, base.layout.scalar)),
    });
    skinned_pipeline.depth_test_enable = .true;
    skinned_pipeline.depth_write_enable = .true;
    skinned_pipeline.depth_compare_op = .less;

    return .{
        .extent = render_extent,
        .images = images,
        .views = views,
        .pipeline = pipeline,
        .skinned_pipeline = skinned_pipeline,
    };
}

pub fn deinit(self: *Self, ctx: *base.Ctx) void {
    self.skinned_pipeline.deinit(&ctx.graphics);
    self.pipeline.deinit(&ctx.graphics);

    for (0..base.Ctx.frames_in_flight) |i| {
        self.views[i].deinit(&ctx.destroy_queue);
        self.images[i].deinit(&ctx.destroy_queue);
    }
}

pub fn visbuffer_ref(self: *const Self, current_frame: u32, rg: *base.RenderGraph) !struct { base.RenderGraph.ImageRef, base.ImageView } {
    return .{try rg.addImage(.{
        .image = self.images[current_frame].handle,
        .start_usage = .{
            .stage = .{ .all_commands_bit = true },
            .access = .{ .memory_write_bit = true },
            .layout = .undefined,
        },
        .end_usage = .{
            .stage = .{ .fragment_shader_bit = true },
            .access = .{ .shader_read_bit = true },
            .layout = .general,
        },
    }), self.views[current_frame] };
}

pub const pc_size = base.push_constant.size(VisPC, base.layout.scalar);
pub const skinned_pc_size = base.push_constant.size(SkinnedVisPC, base.layout.scalar);

const DrawCmdSize = 5 * @sizeOf(u32);
const SkinnedDrawCmdSize = 6 * @sizeOf(u32) + @sizeOf(u64);

pub const RasterParams = struct {
    current_frame: u32,
    vis_ref: base.RenderGraph.ImageRef,
    vp: base.zmath.Mat,
    depth_attachment: base.RenderGraph.DepthAttachment,
    renderables_buf_ref: base.RenderGraph.BufferRef,
    renderables_buf_address: u64,
    draw_cmds_buf_ref: base.RenderGraph.BufferRef,
    draw_cmds_buf_address: u64,
    max_draw_count: u32,
    pc_buf: *[pc_size]u8,
};

pub fn raster(self: *Self, rg: *base.RenderGraph, params: RasterParams) !void {
    const p = &params;
    base.push_constant.write(VisPC, p.pc_buf, .{
        .vp = p.vp,
        .renderables_address = p.renderables_buf_address,
        .draw_buffer_address = p.draw_cmds_buf_address,
    }, base.layout.scalar);

    const pass = try rg.addGraphicsPass(.{
        .pipeline = &self.pipeline,
        .color_attachments = &.{.{
            .image = p.vis_ref,
            .view = self.views[p.current_frame].handle,
            .load_op = .clear,
            .store_op = .store,
            .clear_color = .{
                .uint_32 = .{ 0, 0, 0, 0 }
            }
        }},
        .depth_attachment = p.depth_attachment,
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = self.extent },
    });

    try pass.readBuffer(p.renderables_buf_ref, .{
        .stage = .{ .vertex_shader_bit = true },
        .access = .{ .shader_read_bit = true },
    });
    try pass.readBuffer(p.draw_cmds_buf_ref, .{
        .stage = .{ .vertex_shader_bit = true },
        .access = .{ .shader_read_bit = true },
    });

    pass.drawIndirectCount(.{
        .push_constant = p.pc_buf,
        .buffer = p.draw_cmds_buf_ref,
        .offset = 4,
        .count_buffer = p.draw_cmds_buf_ref,
        .count_offset = 0,
        .max_draw_count = p.max_draw_count,
        .stride = DrawCmdSize,
    });
}

pub const SkinnedRasterParams = struct {
    current_frame: u32,
    vis_ref: base.RenderGraph.ImageRef,
    vp: base.zmath.Mat,
    depth_attachment: base.RenderGraph.DepthAttachment,
    renderables_buf_ref: base.RenderGraph.BufferRef,
    renderables_buf_address: u64,
    draw_cmds_buf_ref: base.RenderGraph.BufferRef,
    draw_cmds_buf_address: u64,
    joints_buf_address: u64,
    max_draw_count: u32,
    pc_buf: *[skinned_pc_size]u8,
};

pub fn rasterSkinned(self: *Self, rg: *base.RenderGraph, params: SkinnedRasterParams) !void {
    const p = &params;
    base.push_constant.write(SkinnedVisPC, p.pc_buf, .{
        .vp = p.vp,
        .renderables_address = p.renderables_buf_address,
        .draw_buffer_address = p.draw_cmds_buf_address,
        .joints_address = p.joints_buf_address,
    }, base.layout.scalar);

    const pass = try rg.addGraphicsPass(.{
        .pipeline = &self.skinned_pipeline,
        .color_attachments = &.{.{
            .image = p.vis_ref,
            .view = self.views[p.current_frame].handle,
            .load_op = .load,
            .store_op = .store,
        }},
        .depth_attachment = p.depth_attachment,
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = self.extent },
    });

    try pass.readBuffer(p.renderables_buf_ref, .{
        .stage = .{ .vertex_shader_bit = true },
        .access = .{ .shader_read_bit = true },
    });
    try pass.readBuffer(p.draw_cmds_buf_ref, .{
        .stage = .{ .vertex_shader_bit = true },
        .access = .{ .shader_read_bit = true },
    });

    pass.drawIndirectCount(.{
        .push_constant = p.pc_buf,
        .buffer = p.draw_cmds_buf_ref,
        .offset = 8,
        .count_buffer = p.draw_cmds_buf_ref,
        .count_offset = 0,
        .max_draw_count = p.max_draw_count,
        .stride = SkinnedDrawCmdSize,
    });
}
