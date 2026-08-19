const std = @import("std");
const base = @import("base");
const zm = base.zmath;
const shaders = @import("shaders");

const Self = @This();

// Fixed schema-id cap; the loader assigns dense schema ids in [0, max_schemas).
// ponytail: fixed array instead of dynamic — bump if more schemas than this.
pub const max_schemas = 8;

pub const SchemaCounters = extern struct { count: [max_schemas]u32 };
pub const SchemaOffsets = extern struct { offset: [max_schemas]u32 };
pub const DispatchEntry = extern struct {
    group_x: u32,
    group_y: u32 = 1,
    group_z: u32 = 1,
    offset: u32,
    count: u32,
};
pub const SchemaDispatch = extern struct { entries: [max_schemas]DispatchEntry };

// Push constants, scalar layout — must match the slang structs byte-for-byte.
pub const CountPC = struct {
    screen: [2]u32,
    renderables_address: u64,
    counters_address: u64,
};
pub const OffsetsPC = struct {
    counters_address: u64,
    offsets_address: u64,
    dispatch_address: u64,
    data_size: u32,
};
pub const FragmentsPC = struct {
    screen: [2]u32,
    renderables_address: u64,
    offsets_address: u64,
    frag_ids_address: u64,
    max_schema: u32,
};

pub const count_pc_size = base.push_constant.size(CountPC, base.layout.scalar);
pub const offsets_pc_size = base.push_constant.size(OffsetsPC, base.layout.scalar);
pub const fragments_pc_size = base.push_constant.size(FragmentsPC, base.layout.scalar);

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

// set layout shared by the processing chain and the shading passes:
// binding 0 = visbuffer (r32ui storage, read), binding 1 = shading target (rgba32f storage, write)
set_layout: base.vk.DescriptorSetLayout,

counters_buf: base.Buffer,
offsets_buf: base.Buffer,
dispatch_buf: base.Buffer,
frag_ids_buf: base.Buffer,

count_pipeline: base.ComputePipeline,
offsets_pipeline: base.ComputePipeline,
fragments_pipeline: base.ComputePipeline,

pub fn init(ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .destroy_queue, .graphics_family, .transport_family, .render_extent }), render_extent: base.vk.Extent2D) !Self {
    const vis_format: base.vk.Format = .r32_uint;

    var images: [base.Ctx.frames_in_flight]base.Image2D = undefined;
    var views: [base.Ctx.frames_in_flight]base.ImageView = undefined;

    for (0..base.Ctx.frames_in_flight) |n| {
        images[n] = try base.Image2D.init(.from(ctx), "vis_buffer", .{
            .format = vis_format,
            .extent = render_extent,
            .usage = .{ .color_attachment_bit = true, .storage_bit = true, .transfer_dst_bit = true },
        });
        errdefer for (0..n) |i| if (i == n) break else images[i].deinit(.from(ctx));

        views[n] = try base.ImageView.init(.from(ctx), .{
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
        errdefer for (0..n) |i| if (i == n) break else images[i].deinit(.from(ctx));
    }

    const vis_mod = try base.ShaderModule.init(.from(ctx), shaders.get(.visbuffer_raster));
    defer vis_mod.deinit(.from(ctx));

    var pipeline = try base.GraphicsPipeline.init(.from(ctx), .{
        .vertex = vis_mod,
        .vertex_entry_point = "vertex",
        .fragment = vis_mod,
        .fragment_entry_point = "fragment",
        .set_layouts = &.{},
        .color_attachments = &.{.{ .format = vis_format }},
        .depth_format = .d32_sfloat,
        .push_constant_size = @intCast(base.push_constant.size(VisPC, base.layout.scalar)),
    });
    pipeline.depth_test_enable = .true;
    pipeline.depth_write_enable = .true;
    pipeline.depth_compare_op = .less;

    const skinned_vert_mod = try base.ShaderModule.init(.from(ctx), shaders.get(.visbuffer_raster_skinned));
    defer skinned_vert_mod.deinit(.from(ctx));

    var skinned_pipeline = try base.GraphicsPipeline.init(.from(ctx), .{
        .vertex = skinned_vert_mod,
        .vertex_entry_point = "vertex",
        .fragment = vis_mod,
        .fragment_entry_point = "fragment",
        .set_layouts = &.{},
        .color_attachments = &.{.{ .format = vis_format }},
        .depth_format = .d32_sfloat,
        .push_constant_size = @intCast(base.push_constant.size(SkinnedVisPC, base.layout.scalar)),
    });
    skinned_pipeline.depth_test_enable = .true;
    skinned_pipeline.depth_write_enable = .true;
    skinned_pipeline.depth_compare_op = .less;

    const set_layout = try ctx.view.device.createDescriptorSetLayout(&.{
        .binding_count = 2,
        .p_bindings = &.{
            base.vk.DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptor_type = .storage_image,
                .descriptor_count = 1,
                .stage_flags = .{ .compute_bit = true },
            },
            base.vk.DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptor_type = .storage_image,
                .descriptor_count = 1,
                .stage_flags = .{ .compute_bit = true },
            },
        },
    }, null);
    errdefer ctx.view.device.destroyDescriptorSetLayout(set_layout, null);

    var counters_buf = try base.Buffer.init(.from(ctx), .graphics, "Schema counters", @sizeOf(SchemaCounters), .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    errdefer counters_buf.deinit(.from(ctx));
    var offsets_buf = try base.Buffer.init(.from(ctx), .graphics, "Schema offsets", @sizeOf(SchemaOffsets), .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    errdefer offsets_buf.deinit(.from(ctx));
    var dispatch_buf = try base.Buffer.init(.from(ctx), .graphics, "Schema dispatch", @sizeOf(SchemaDispatch), .{ .storage_buffer_bit = true, .indirect_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    errdefer dispatch_buf.deinit(.from(ctx));
    var frag_ids_buf = try base.Buffer.init(.from(ctx), .graphics, "Fragment ids", @as(u64, render_extent.width) * render_extent.height * 4, .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
    errdefer frag_ids_buf.deinit(.from(ctx));

    const count_mod = try base.ShaderModule.init(.from(ctx), shaders.get(.visbuffer_schema_count));
    defer count_mod.deinit(.from(ctx));
    const offsets_mod = try base.ShaderModule.init(.from(ctx), shaders.get(.visbuffer_schema_offsets));
    defer offsets_mod.deinit(.from(ctx));
    const fragments_mod = try base.ShaderModule.init(.from(ctx), shaders.get(.visbuffer_schema_fragments));
    defer fragments_mod.deinit(.from(ctx));

    var count_pipeline = try base.ComputePipeline.init(.from(ctx), .{
        .shader = count_mod,
        .entry_point = "compute",
        .set_layouts = &.{set_layout},
        .push_constant_size = @intCast(count_pc_size),
    });
    errdefer count_pipeline.deinit(.from(ctx));
    var offsets_pipeline = try base.ComputePipeline.init(.from(ctx), .{
        .shader = offsets_mod,
        .entry_point = "compute",
        .set_layouts = &.{set_layout},
        .push_constant_size = @intCast(offsets_pc_size),
    });
    errdefer offsets_pipeline.deinit(.from(ctx));
    var fragments_pipeline = try base.ComputePipeline.init(.from(ctx), .{
        .shader = fragments_mod,
        .entry_point = "compute",
        .set_layouts = &.{set_layout},
        .push_constant_size = @intCast(fragments_pc_size),
    });
    errdefer fragments_pipeline.deinit(.from(ctx));

    return .{
        .extent = render_extent,
        .images = images,
        .views = views,
        .pipeline = pipeline,
        .skinned_pipeline = skinned_pipeline,
        .set_layout = set_layout,
        .counters_buf = counters_buf,
        .offsets_buf = offsets_buf,
        .dispatch_buf = dispatch_buf,
        .frag_ids_buf = frag_ids_buf,
        .count_pipeline = count_pipeline,
        .offsets_pipeline = offsets_pipeline,
        .fragments_pipeline = fragments_pipeline,
    };
}

pub fn deinit(self: *Self, ctx: base.Ctx.Query(&.{ .device, .destroy_queue })) void {
    self.fragments_pipeline.deinit(.from(ctx));
    self.offsets_pipeline.deinit(.from(ctx));
    self.count_pipeline.deinit(.from(ctx));
    self.skinned_pipeline.deinit(.from(ctx));
    self.pipeline.deinit(.from(ctx));

    self.frag_ids_buf.deinit(.from(ctx));
    self.dispatch_buf.deinit(.from(ctx));
    self.offsets_buf.deinit(.from(ctx));
    self.counters_buf.deinit(.from(ctx));

    ctx.view.device.destroyDescriptorSetLayout(self.set_layout, null);

    for (0..base.Ctx.frames_in_flight) |i| {
        self.views[i].deinit(.from(ctx));
        self.images[i].deinit(.from(ctx));
    }
}

pub fn visbuffer_ref(self: *const Self, current_frame: u32, rg: *base.RenderGraph) !struct { base.RenderGraph.ImageRef, base.ImageView } {
    return .{ try rg.addImage(.{
        .image = self.images[current_frame].handle,
        .start_usage = .{
            .stage = .{ .all_commands_bit = true },
            .access = .{ .memory_write_bit = true },
            .layout = .undefined,
        },
        .end_usage = .{
            .stage = .{ .compute_shader_bit = true },
            .access = .{ .shader_storage_read_bit = true },
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
    draw_cmds_buf_ref: base.RenderGraph.BufferRef,
    max_draw_count: u32,
    pc_buf: *[pc_size]u8,
};

pub fn raster(self: *Self, rg: *base.RenderGraph, params: RasterParams) !void {
    const p = &params;
    base.push_constant.write(VisPC, p.pc_buf, .{
        .vp = p.vp,
        .renderables_address = rg.getBuffer(p.renderables_buf_ref).address,
        .draw_buffer_address = rg.getBuffer(p.draw_cmds_buf_ref).address,
    }, base.layout.scalar);

    const pass = try rg.addGraphicsPass(.{
        .pipeline = &self.pipeline,
        .color_attachments = &.{.{ .image = p.vis_ref, .view = self.views[p.current_frame].handle, .load_op = .clear, .store_op = .store, .clear_color = .{ .uint_32 = .{ 0, 0, 0, 0 } } }},
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
    draw_cmds_buf_ref: base.RenderGraph.BufferRef,
    joints_ref: base.RenderGraph.BufferRef,
    arena_ref: base.RenderGraph.BufferRef,
    max_draw_count: u32,
    pc_buf: *[skinned_pc_size]u8,
};

pub fn rasterSkinned(self: *Self, rg: *base.RenderGraph, params: SkinnedRasterParams) !void {
    const p = &params;
    base.push_constant.write(SkinnedVisPC, p.pc_buf, .{
        .vp = p.vp,
        .renderables_address = rg.getBuffer(p.renderables_buf_ref).address,
        .draw_buffer_address = rg.getBuffer(p.draw_cmds_buf_ref).address,
        .joints_address = rg.getBuffer(p.joints_ref).address,
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
    // The vertex shader writes the skinned geometry cache; the shading pass reads it,
    // so declare the dependency for the barrier.
    try pass.writeBuffer(p.arena_ref, .{
        .stage = .{ .vertex_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
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

/// Per-frame descriptor set with the visbuffer (binding 0) and shading target
/// (binding 1) as storage images. Caller updates nothing; both views are set here.
pub fn shadingSet(self: *const Self, alloc: std.mem.Allocator, ctx: base.Ctx.Query(&.{ .device, .descriptor_pool, .current_frame }), target_view: base.vk.ImageView) !u64 {
    const set_id = try ctx.view.descriptor_pool.newSet(.from(ctx), self.set_layout);
    ctx.view.descriptor_pool.beginUpdate(set_id);
    try ctx.view.descriptor_pool.updateStorageImage(alloc, 0, .general, self.views[ctx.view.current_frame].handle);
    try ctx.view.descriptor_pool.updateStorageImage(alloc, 1, .general, target_view);
    ctx.view.descriptor_pool.endUpdate(.from(ctx));
    return set_id;
}

pub const ProcessParams = struct {
    vis_ref: base.RenderGraph.ImageRef,
    set_id: u64,
    renderables_buf_ref: base.RenderGraph.BufferRef,
    counters_ref: base.RenderGraph.BufferRef,
    offsets_ref: base.RenderGraph.BufferRef,
    dispatch_ref: base.RenderGraph.BufferRef,
    frag_ids_ref: base.RenderGraph.BufferRef,
    count_pc_buf: *[count_pc_size]u8,
    offsets_pc_buf: *[offsets_pc_size]u8,
    fragments_pc_buf: *[fragments_pc_size]u8,
};

/// Resets counters, counts fragments per schema, prefix-sums into per-schema
/// dispatch entries, scatters fragment pixel ids. Must run after visbuffer raster.
pub fn process(self: *Self, rg: *base.RenderGraph, params: ProcessParams) !void {
    const p = &params;

    base.push_constant.write(CountPC, p.count_pc_buf, .{
        .screen = .{ self.extent.width, self.extent.height },
        .renderables_address = rg.getBuffer(p.renderables_buf_ref).address,
        .counters_address = self.counters_buf.address,
    }, base.layout.scalar);

    const count_pass = try rg.addComputePass(.{
        .pipeline = &self.count_pipeline,
        .descriptor_sets = &.{p.set_id},
        .dispatch = .{
            .push_constant = p.count_pc_buf,
            .group_count_x = (self.extent.width + 15) / 16,
            .group_count_y = (self.extent.height + 15) / 16,
            .group_count_z = 1,
        },
    });
    try count_pass.readImage(p.vis_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
        .layout = .general,
    });
    try count_pass.readBuffer(p.renderables_buf_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try count_pass.writeBuffer(p.counters_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
    });

    base.push_constant.write(OffsetsPC, p.offsets_pc_buf, .{
        .counters_address = self.counters_buf.address,
        .offsets_address = self.offsets_buf.address,
        .dispatch_address = self.dispatch_buf.address,
        .data_size = max_schemas,
    }, base.layout.scalar);

    const offsets_pass = try rg.addComputePass(.{
        .pipeline = &self.offsets_pipeline,
        .descriptor_sets = &.{p.set_id},
        .dispatch = .{
            .push_constant = p.offsets_pc_buf,
            .group_count_x = 1,
            .group_count_y = 1,
            .group_count_z = 1,
        },
    });
    try offsets_pass.readBuffer(p.counters_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try offsets_pass.writeBuffer(p.offsets_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
    });
    try offsets_pass.writeBuffer(p.dispatch_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
    });

    base.push_constant.write(FragmentsPC, p.fragments_pc_buf, .{
        .screen = .{ self.extent.width, self.extent.height },
        .renderables_address = rg.getBuffer(p.renderables_buf_ref).address,
        .offsets_address = self.offsets_buf.address,
        .frag_ids_address = self.frag_ids_buf.address,
        .max_schema = max_schemas - 1,
    }, base.layout.scalar);

    const fragments_pass = try rg.addComputePass(.{
        .pipeline = &self.fragments_pipeline,
        .descriptor_sets = &.{p.set_id},
        .dispatch = .{
            .push_constant = p.fragments_pc_buf,
            .group_count_x = (self.extent.width + 15) / 16,
            .group_count_y = (self.extent.height + 15) / 16,
            .group_count_z = 1,
        },
    });
    try fragments_pass.readImage(p.vis_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
        .layout = .general,
    });
    try fragments_pass.readBuffer(p.renderables_buf_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true },
    });
    try fragments_pass.readBuffer(p.offsets_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_read_bit = true, .shader_storage_write_bit = true },
    });
    try fragments_pass.writeBuffer(p.frag_ids_ref, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_storage_write_bit = true },
    });
}
