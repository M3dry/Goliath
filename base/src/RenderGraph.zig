const std = @import("std");
const zprobe = @import("zprobe");
const vk = @import("vulkan");
const util = @import("util.zig");

const GraphicsCtx = @import("GraphicsCtx.zig");
const DescriptorPool = @import("DescriptorPool.zig");
const GraphicsPipeline = @import("GraphicsPipeline.zig");
const ComputePipeline = @import("ComputePipeline.zig");
const Buffer = @import("Buffer.zig");
const SmallBuffer = @import("util/small_buffer.zig").SmallBuffer;

const RenderGraph = @This();

const Allocator = std.mem.Allocator;

pub const ImageRef = struct {
    index: u32,
};

pub const BufferRef = struct {
    index: u32,
};

pub const ImageUsage = struct {
    stage: vk.PipelineStageFlags2,
    access: vk.AccessFlags2,
    layout: vk.ImageLayout,
};

pub const BufferUsage = struct {
    stage: vk.PipelineStageFlags2,
    access: vk.AccessFlags2,
};

pub const ImageContract = struct {
    image: vk.Image,
    aspect: vk.ImageAspectFlags = .{ .color_bit = true },
    start_usage: ImageUsage,
    end_usage: ImageUsage,
};

pub const BufferContract = struct {
    buffer: Buffer,
    offset: u64,
    size: u64,
    start_usage: BufferUsage,
    end_usage: BufferUsage,
};

pub const DrawCall = struct {
    push_constant: ?[]const u8 = null,
    vertex_count: u32,
    instance_count: u32 = 1,
    first_vertex: u32 = 0,
    first_instance: u32 = 0,
};

pub const DrawIndirect = struct {
    push_constant: ?[]const u8 = null,
    buffer: BufferRef,
    offset: u32 = 0,
    draw_count: u32,
    stride: u32 = @sizeOf(vk.DrawIndirectCommand),
};

pub const DrawIndirectCount = struct {
    push_constant: ?[]const u8 = null,
    buffer: BufferRef,
    offset: u32 = 0,
    count_buffer: BufferRef,
    count_offset: u32 = 0,
    max_draw_count: u32,
    stride: u32 = @sizeOf(vk.DrawIndirectCommand),
};

pub const DispatchCall = struct {
    push_constant: ?[]const u8 = null,
    group_count_x: u32,
    group_count_y: u32,
    group_count_z: u32,
};

pub const DispatchIndirect = struct {
    push_constant: ?[]const u8 = null,
    buffer: BufferRef,
    offset: u64 = 0,
};

pub const FillBuffer = struct {
    buffer: BufferRef,
    offset: u64 = 0,
    size: u64,
    value: u32 = 0,
};

pub const ClearImage = struct {
    image: ImageRef,
    color: vk.ClearColorValue,
    range: vk.ImageSubresourceRange = .{
        .aspect_mask = .{ .color_bit = true },
        .base_mip_level = 0,
        .level_count = vk.REMAINING_MIP_LEVELS,
        .base_array_layer = 0,
        .layer_count = vk.REMAINING_ARRAY_LAYERS,
    },
};

pub const ColorAttachment = struct {
    image: ImageRef,
    view: vk.ImageView,
    load_op: vk.AttachmentLoadOp = .clear,
    store_op: vk.AttachmentStoreOp = .store,
    clear_color: vk.ClearColorValue = .{ .float_32 = .{ 0, 0, 0, 0 } },
    layout: vk.ImageLayout = .color_attachment_optimal,
};

pub const DepthAttachment = struct {
    image: ImageRef,
    view: vk.ImageView,
    load_op: vk.AttachmentLoadOp = .clear,
    store_op: vk.AttachmentStoreOp = .store,
    clear_depth: f32 = 1.0,
    clear_stencil: u32 = 0,
    layout: vk.ImageLayout = .depth_stencil_attachment_optimal,
    has_stencil: bool = false,
};

pub const StencilAttachment = struct {
    image: ImageRef,
    view: vk.ImageView,
    load_op: vk.AttachmentLoadOp = .clear,
    store_op: vk.AttachmentStoreOp = .store,
    clear_stencil: u32 = 0,
    layout: vk.ImageLayout = .depth_stencil_attachment_optimal,
};

const ImageRefUsage = struct {
    ref: ImageRef,
    usage: ImageUsage,
};

const BufferRefUsage = struct {
    ref: BufferRef,
    usage: BufferUsage,
};

pub const GraphicsPass = struct {
    pipeline: *GraphicsPipeline,
    color_attachments: []const ColorAttachment = &.{},
    depth_attachment: ?DepthAttachment = null,
    stencil_attachment: ?StencilAttachment = null,
    render_area: vk.Rect2D,
    descriptor_sets: []const u64 = &.{},
    reads_images: std.ArrayListUnmanaged(ImageRefUsage) = .empty,
    reads_buffers: std.ArrayListUnmanaged(BufferRefUsage) = .empty,
    writes_images: std.ArrayListUnmanaged(ImageRefUsage) = .empty,
    writes_buffers: std.ArrayListUnmanaged(BufferRefUsage) = .empty,
    draws: std.ArrayListUnmanaged(DrawCall) = .empty,
    indirect: ?DrawIndirect = null,
    indirect_count: ?DrawIndirectCount = null,
    query_pool: ?vk.QueryPool = null,
    query_slot: u32 = 0,
};

pub const ComputePass = struct {
    pipeline: *ComputePipeline,
    descriptor_sets: []const u64 = &.{},
    reads_images: std.ArrayListUnmanaged(ImageRefUsage) = .empty,
    reads_buffers: std.ArrayListUnmanaged(BufferRefUsage) = .empty,
    writes_images: std.ArrayListUnmanaged(ImageRefUsage) = .empty,
    writes_buffers: std.ArrayListUnmanaged(BufferRefUsage) = .empty,
    dispatch: DispatchCall,
    indirect: ?DispatchIndirect = null,
    query_pool: ?vk.QueryPool = null,
    query_slot: u32 = 0,
};

pub const TransferPass = struct {
    fills: std.ArrayListUnmanaged(FillBuffer) = .empty,
    clears: std.ArrayListUnmanaged(ClearImage) = .empty,
    reads_images: std.ArrayListUnmanaged(ImageRefUsage) = .empty,
    writes_images: std.ArrayListUnmanaged(ImageRefUsage) = .empty,
    reads_buffers: std.ArrayListUnmanaged(BufferRefUsage) = .empty,
    writes_buffers: std.ArrayListUnmanaged(BufferRefUsage) = .empty,
};

pub const PassType = enum { graphics, compute, transfer };

fn PassHandle(comptime pass_type: PassType) type {
    return struct {
        const pass_name: []const u8 = switch (pass_type) {
            .graphics => "graphics",
            .compute => "compute",
            .transfer => "transfer",
        };

        rg: *RenderGraph,
        index: u32,

        pub fn readImage(self: @This(), image: ImageRef, usage: ImageUsage) Allocator.Error!void {
            try @field(self.rg.passes.items[self.index], pass_name).reads_images.append(self.rg.alloc, .{ .ref = image, .usage = usage });
        }
        pub fn readBuffer(self: @This(), buffer: BufferRef, usage: BufferUsage) Allocator.Error!void {
            try @field(self.rg.passes.items[self.index], pass_name).reads_buffers.append(self.rg.alloc, .{ .ref = buffer, .usage = usage });
        }
        pub fn writeImage(self: @This(), image: ImageRef, usage: ImageUsage) Allocator.Error!void {
            try @field(self.rg.passes.items[self.index], pass_name).writes_images.append(self.rg.alloc, .{ .ref = image, .usage = usage });
        }
        pub fn writeBuffer(self: @This(), buffer: BufferRef, usage: BufferUsage) Allocator.Error!void {
            try @field(self.rg.passes.items[self.index], pass_name).writes_buffers.append(self.rg.alloc, .{ .ref = buffer, .usage = usage });
        }
        pub fn setDescriptorSets(self: @This(), sets: []const u64) void {
            @field(self.rg.passes.items[self.index], pass_name).descriptor_sets = sets;
        }

        pub fn draw(self: @This(), call: DrawCall) Allocator.Error!void {
            if (pass_type != .graphics) @compileError("draw is only valid on graphics passes");
            try self.rg.passes.items[self.index].graphics.draws.append(self.rg.alloc, call);
        }
        pub fn drawIndirect(self: @This(), indirect: DrawIndirect) void {
            if (pass_type != .graphics) @compileError("drawIndirect is only valid on graphics passes");
            self.rg.passes.items[self.index].graphics.indirect = indirect;
        }
        pub fn drawIndirectCount(self: @This(), count: DrawIndirectCount) void {
            if (pass_type != .graphics) @compileError("drawIndirectCount is only valid on graphics passes");
            self.rg.passes.items[self.index].graphics.indirect_count = count;
        }
        pub fn fillBuffer(self: @This(), fill: FillBuffer) Allocator.Error!void {
            if (pass_type != .transfer) @compileError("fillBuffer is only valid on transfer passes");
            const tp = &self.rg.passes.items[self.index].transfer;
            try tp.writes_buffers.append(self.rg.alloc, .{
                .ref = fill.buffer,
                .usage = .{ .stage = .{ .all_transfer_bit = true }, .access = .{ .transfer_write_bit = true } },
            });
            try tp.fills.append(self.rg.alloc, fill);
        }
        pub fn clearImage(self: @This(), clear: ClearImage) Allocator.Error!void {
            if (pass_type != .transfer) @compileError("clearImage is only valid on transfer passes");
            const tp = &self.rg.passes.items[self.index].transfer;
            try tp.writes_images.append(self.rg.alloc, .{
                .ref = clear.image,
                .usage = .{ .stage = .{ .all_transfer_bit = true }, .access = .{ .transfer_write_bit = true }, .layout = .transfer_dst_optimal },
            });
            try tp.clears.append(self.rg.alloc, clear);
        }
    };
}

pub const GraphicsPassHandle = PassHandle(.graphics);
pub const ComputePassHandle = PassHandle(.compute);
pub const TransferPassHandle = PassHandle(.transfer);

pub const Pass = union(enum) {
    graphics: GraphicsPass,
    compute: ComputePass,
    transfer: TransferPass,
};

const ImageTrackedState = struct {
    layout: vk.ImageLayout,
    stage: vk.PipelineStageFlags2,
    access: vk.AccessFlags2,
};

const BufferTrackedState = struct {
    stage: vk.PipelineStageFlags2,
    access: vk.AccessFlags2,
};

fn attachmentLayoutToUsage(layout: vk.ImageLayout) ImageUsage {
    return switch (layout) {
        .color_attachment_optimal => .{
            .stage = .{ .color_attachment_output_bit = true },
            .access = .{ .color_attachment_read_bit = true, .color_attachment_write_bit = true },
            .layout = layout,
        },
        .depth_stencil_attachment_optimal, .depth_attachment_optimal => .{
            .stage = .{ .early_fragment_tests_bit = true, .late_fragment_tests_bit = true },
            .access = .{ .depth_stencil_attachment_read_bit = true, .depth_stencil_attachment_write_bit = true },
            .layout = layout,
        },
        .depth_stencil_read_only_optimal, .depth_read_only_optimal => .{
            .stage = .{ .early_fragment_tests_bit = true, .late_fragment_tests_bit = true },
            .access = .{ .depth_stencil_attachment_read_bit = true },
            .layout = layout,
        },
        .shader_read_only_optimal => .{
            .stage = .{ .fragment_shader_bit = true },
            .access = .{ .shader_read_bit = true },
            .layout = layout,
        },
        .transfer_src_optimal => .{
            .stage = .{ .all_transfer_bit = true },
            .access = .{ .transfer_read_bit = true },
            .layout = layout,
        },
        .transfer_dst_optimal => .{
            .stage = .{ .all_transfer_bit = true },
            .access = .{ .transfer_write_bit = true },
            .layout = layout,
        },
        .general => .{
            .stage = .{ .all_commands_bit = true },
            .access = .{ .memory_read_bit = true, .memory_write_bit = true },
            .layout = layout,
        },
        .present_src_khr => .{
            .stage = .{},
            .access = .{},
            .layout = layout,
        },
        .undefined => .{
            .stage = .{ .all_commands_bit = true },
            .access = .{ .memory_write_bit = true },
            .layout = layout,
        },
        else => blk: {
            zprobe.event(.warn, "unhandled image layout", .{ .layout = @tagName(layout) });
            break :blk .{
                .stage = .{ .all_commands_bit = true },
                .access = .{ .memory_read_bit = true, .memory_write_bit = true },
                .layout = layout,
            };
        },
    };
}

fn imageUsageEqual(a: ImageTrackedState, b: ImageUsage) bool {
    return a.layout == b.layout and
        std.meta.eql(a.stage, b.stage) and
        std.meta.eql(a.access, b.access);
}

fn bufferUsageEqual(a: BufferTrackedState, b: BufferUsage) bool {
    return std.meta.eql(a.stage, b.stage) and
        std.meta.eql(a.access, b.access);
}

fn mergeStage(a: vk.PipelineStageFlags2, b: vk.PipelineStageFlags2) vk.PipelineStageFlags2 {
    return a.merge(b);
}

fn mergeAccess(a: vk.AccessFlags2, b: vk.AccessFlags2) vk.AccessFlags2 {
    return a.merge(b);
}

fn mergeBufferUsage(a: BufferUsage, b: BufferUsage) BufferUsage {
    return .{
        .stage = mergeStage(a.stage, b.stage),
        .access = mergeAccess(a.access, b.access),
    };
}

fn mergeImageUsage(a: ImageUsage, b: ImageUsage) ImageUsage {
    return .{
        .stage = mergeStage(a.stage, b.stage),
        .access = mergeAccess(a.access, b.access),
        .layout = b.layout,
    };
}

fn upsert(
    comptime V: type,
    map: *std.AutoArrayHashMapUnmanaged(u32, V),
    idx: u32,
    usage: V,
    comptime mergeFn: fn (V, V) V,
    alloc: Allocator,
) Allocator.Error!void {
    const gop = try map.getOrPut(alloc, idx);
    if (gop.found_existing) {
        gop.value_ptr.* = mergeFn(gop.value_ptr.*, usage);
    } else {
        gop.value_ptr.* = usage;
    }
}

fn collectIoRequirements(
    pass_images: *std.AutoArrayHashMapUnmanaged(u32, ImageUsage),
    pass_buffers: *std.AutoArrayHashMapUnmanaged(u32, BufferUsage),
    reads_images: []const ImageRefUsage,
    reads_buffers: []const BufferRefUsage,
    writes_images: []const ImageRefUsage,
    writes_buffers: []const BufferRefUsage,
    alloc: Allocator,
) Allocator.Error!void {
    for (reads_images) |ri| { try upsert(ImageUsage, pass_images, ri.ref.index, ri.usage, mergeImageUsage, alloc); }
    for (reads_buffers) |rb| { try upsert(BufferUsage, pass_buffers, rb.ref.index, rb.usage, mergeBufferUsage, alloc); }
    for (writes_images) |wi| { try upsert(ImageUsage, pass_images, wi.ref.index, wi.usage, mergeImageUsage, alloc); }
    for (writes_buffers) |wb| { try upsert(BufferUsage, pass_buffers, wb.ref.index, wb.usage, mergeBufferUsage, alloc); }
}

fn collectPassRequirementsGraphics(
    pass_images: *std.AutoArrayHashMapUnmanaged(u32, ImageUsage),
    pass_buffers: *std.AutoArrayHashMapUnmanaged(u32, BufferUsage),
    gp: *const GraphicsPass,
    alloc: Allocator,
) Allocator.Error!void {
    pass_images.clearRetainingCapacity();
    pass_buffers.clearRetainingCapacity();

    for (gp.color_attachments) |att| {
        try upsert(ImageUsage, pass_images, att.image.index, attachmentLayoutToUsage(att.layout), mergeImageUsage, alloc);
    }
    if (gp.depth_attachment) |att| {
        try upsert(ImageUsage, pass_images, att.image.index, attachmentLayoutToUsage(att.layout), mergeImageUsage, alloc);
    }
    if (gp.stencil_attachment) |att| {
        try upsert(ImageUsage, pass_images, att.image.index, attachmentLayoutToUsage(att.layout), mergeImageUsage, alloc);
    }

    try collectIoRequirements(pass_images, pass_buffers, gp.reads_images.items, gp.reads_buffers.items, gp.writes_images.items, gp.writes_buffers.items, alloc);

    const indirect_stage = vk.PipelineStageFlags2{ .draw_indirect_bit = true };
    const indirect_access = vk.AccessFlags2{ .indirect_command_read_bit = true };

    if (gp.indirect) |ind| {
        try upsert(BufferUsage, pass_buffers, ind.buffer.index, .{ .stage = indirect_stage, .access = indirect_access }, mergeBufferUsage, alloc);
    }
    if (gp.indirect_count) |ic| {
        try upsert(BufferUsage, pass_buffers, ic.buffer.index, .{ .stage = indirect_stage, .access = indirect_access }, mergeBufferUsage, alloc);
        try upsert(BufferUsage, pass_buffers, ic.count_buffer.index, .{ .stage = indirect_stage, .access = indirect_access }, mergeBufferUsage, alloc);
    }
}

fn collectPassRequirementsCompute(
    pass_images: *std.AutoArrayHashMapUnmanaged(u32, ImageUsage),
    pass_buffers: *std.AutoArrayHashMapUnmanaged(u32, BufferUsage),
    cp: *const ComputePass,
    alloc: Allocator,
) Allocator.Error!void {
    pass_images.clearRetainingCapacity();
    pass_buffers.clearRetainingCapacity();

    try collectIoRequirements(pass_images, pass_buffers, cp.reads_images.items, cp.reads_buffers.items, cp.writes_images.items, cp.writes_buffers.items, alloc);

    const indirect_stage = vk.PipelineStageFlags2{ .draw_indirect_bit = true };
    const indirect_access = vk.AccessFlags2{ .indirect_command_read_bit = true };

    if (cp.indirect) |ind| {
        try upsert(BufferUsage, pass_buffers, ind.buffer.index, .{ .stage = indirect_stage, .access = indirect_access }, mergeBufferUsage, alloc);
    }
}

fn collectPassRequirementsTransfer(
    pass_images: *std.AutoArrayHashMapUnmanaged(u32, ImageUsage),
    pass_buffers: *std.AutoArrayHashMapUnmanaged(u32, BufferUsage),
    tp: *const TransferPass,
    alloc: Allocator,
) Allocator.Error!void {
    pass_images.clearRetainingCapacity();
    pass_buffers.clearRetainingCapacity();

    for (tp.reads_images.items) |ri| {
        try upsert(ImageUsage, pass_images, ri.ref.index, ri.usage, mergeImageUsage, alloc);
    }
    for (tp.writes_images.items) |wi| {
        try upsert(ImageUsage, pass_images, wi.ref.index, wi.usage, mergeImageUsage, alloc);
    }
    for (tp.reads_buffers.items) |rb| {
        try upsert(BufferUsage, pass_buffers, rb.ref.index, rb.usage, mergeBufferUsage, alloc);
    }
    for (tp.writes_buffers.items) |wb| {
        try upsert(BufferUsage, pass_buffers, wb.ref.index, wb.usage, mergeBufferUsage, alloc);
    }
}

fn emitPassBarriers(
    pass_images: std.AutoArrayHashMapUnmanaged(u32, ImageUsage),
    pass_buffers: std.AutoArrayHashMapUnmanaged(u32, BufferUsage),
    image_states: []ImageTrackedState,
    buffer_states: []BufferTrackedState,
    contracts: []const ImageContract,
    buf_contracts: []const BufferContract,
    qf: u32,
    img_bars: *std.ArrayListUnmanaged(vk.ImageMemoryBarrier2),
    buf_bars: *std.ArrayListUnmanaged(vk.BufferMemoryBarrier2),
    alloc: Allocator,
) Allocator.Error!void {
    var it = pass_images.iterator();
    while (it.next()) |entry| {
        const idx = entry.key_ptr.*;
        const req = entry.value_ptr.*;
        const cur = &image_states[idx];
        if (!imageUsageEqual(cur.*, req)) {
            try img_bars.append(alloc, .{
                .src_stage_mask = cur.stage,
                .src_access_mask = cur.access,
                .dst_stage_mask = req.stage,
                .dst_access_mask = req.access,
                .old_layout = cur.layout,
                .new_layout = req.layout,
                .src_queue_family_index = qf,
                .dst_queue_family_index = qf,
                .image = contracts[idx].image,
                .subresource_range = util.fullRange(contracts[idx].aspect),
            });
            cur.* = .{
                .layout = req.layout,
                .stage = req.stage,
                .access = req.access,
            };
        }
    }

    var bit = pass_buffers.iterator();
    while (bit.next()) |entry| {
        const idx = entry.key_ptr.*;
        const req = entry.value_ptr.*;
        const cur = &buffer_states[idx];
        if (!bufferUsageEqual(cur.*, req)) {
            try buf_bars.append(alloc, .{
                .src_stage_mask = cur.stage,
                .src_access_mask = cur.access,
                .dst_stage_mask = req.stage,
                .dst_access_mask = req.access,
                .src_queue_family_index = qf,
                .dst_queue_family_index = qf,
                .buffer = buf_contracts[idx].buffer.handle,
                .offset = buf_contracts[idx].offset,
                .size = buf_contracts[idx].size,
            });
            cur.* = .{
                .stage = req.stage,
                .access = req.access,
            };
        }
    }
}

fn recordGraphicsCommands(
    gp: *const GraphicsPass,
    gc: *const GraphicsCtx,
    cmd_buf: vk.CommandBuffer,
    dp: *DescriptorPool,
    buf_contracts: []const BufferContract,
) void {
    for (gp.descriptor_sets, 0..) |set_id, i| {
        dp.bindSet(cmd_buf, gc, set_id, .graphics, gp.pipeline.layout, @intCast(i));
    }

    gp.pipeline.bind(gc, cmd_buf);

    for (gp.draws.items) |d| {
        gp.pipeline.draw(gc, cmd_buf, .{
            .push_constant = d.push_constant,
            .vertex_count = d.vertex_count,
            .instance_count = d.instance_count,
            .first_vertex = d.first_vertex,
            .first_instance = d.first_instance,
        });
    }

    if (gp.indirect) |ind| {
        gp.pipeline.drawIndirect(gc, cmd_buf, .{
            .push_constant = ind.push_constant,
            .buffer = buf_contracts[ind.buffer.index].buffer.handle,
            .offset = ind.offset,
            .draw_count = ind.draw_count,
            .stride = ind.stride,
        });
    }
    if (gp.indirect_count) |ic| {
        gp.pipeline.drawIndirectCount(gc, cmd_buf, .{
            .push_constant = ic.push_constant,
            .buffer = buf_contracts[ic.buffer.index].buffer.handle,
            .offset = ic.offset,
            .count_buffer = buf_contracts[ic.count_buffer.index].buffer.handle,
            .count_offset = ic.count_offset,
            .max_draw_count = ic.max_draw_count,
            .stride = ic.stride,
        });
    }
}

fn recordGraphicsPass(
    gp: *const GraphicsPass,
    gc: *const GraphicsCtx,
    cmd_buf: vk.CommandBuffer,
    dp: *DescriptorPool,
    buf_contracts: []const BufferContract,
    alloc: Allocator,
) Allocator.Error!void {
    const dev = &gc.dev;
    try beginRendering(gp, dev, cmd_buf, alloc);
    recordGraphicsCommands(gp, gc, cmd_buf, dp, buf_contracts);
    dev.cmdEndRendering(cmd_buf);
}

fn renderTargetsEqual(a: *const GraphicsPass, b: *const GraphicsPass) bool {
    if (a.color_attachments.len != b.color_attachments.len) return false;
    for (a.color_attachments, b.color_attachments) |ca, cb| {
        if (ca.view != cb.view or ca.layout != cb.layout) return false;
    }
    if ((a.depth_attachment != null) != (b.depth_attachment != null)) return false;
    if (a.depth_attachment) |da| {
        const db = b.depth_attachment.?;
        if (da.view != db.view or da.layout != db.layout) return false;
    }
    if ((a.stencil_attachment != null) != (b.stencil_attachment != null)) return false;
    if (a.stencil_attachment) |sa| {
        const sb = b.stencil_attachment.?;
        if (sa.view != sb.view or sa.layout != sb.layout) return false;
    }
    return a.render_area.offset.x == b.render_area.offset.x and
        a.render_area.offset.y == b.render_area.offset.y and
        a.render_area.extent.width == b.render_area.extent.width and
        a.render_area.extent.height == b.render_area.extent.height;
}

fn beginRendering(
    gp: *const GraphicsPass,
    dev: *const vk.DeviceProxy,
    cmd_buf: vk.CommandBuffer,
    alloc: Allocator,
) Allocator.Error!void {
    var color_buf: SmallBuffer(vk.RenderingAttachmentInfo, 16) = .{};
    defer color_buf.deinit(alloc);
    const colors = try color_buf.get(alloc, gp.color_attachments.len);

    for (gp.color_attachments, 0..) |att, i| {
        colors[i] = .{
            .image_view = att.view,
            .image_layout = att.layout,
            .resolve_mode = .{},
            .resolve_image_layout = .undefined,
            .load_op = att.load_op,
            .store_op = att.store_op,
            .clear_value = .{ .color = att.clear_color },
        };
    }

    var depth_attachment_info: vk.RenderingAttachmentInfo = undefined;
    const p_depth: ?*const vk.RenderingAttachmentInfo = if (gp.depth_attachment) |*att| blk: {
        depth_attachment_info = .{
            .image_view = att.view,
            .image_layout = att.layout,
            .resolve_mode = .{},
            .resolve_image_layout = .undefined,
            .load_op = att.load_op,
            .store_op = att.store_op,
            .clear_value = .{ .depth_stencil = .{ .depth = att.clear_depth, .stencil = att.clear_stencil } },
        };
        break :blk &depth_attachment_info;
    } else null;

    var stencil_attachment_info: vk.RenderingAttachmentInfo = undefined;
    const p_stencil: ?*const vk.RenderingAttachmentInfo = if (gp.stencil_attachment) |*att| blk: {
        stencil_attachment_info = .{
            .image_view = att.view,
            .image_layout = att.layout,
            .resolve_mode = .{},
            .resolve_image_layout = .undefined,
            .load_op = att.load_op,
            .store_op = att.store_op,
            .clear_value = .{ .depth_stencil = .{ .depth = 0, .stencil = att.clear_stencil } },
        };
        break :blk &stencil_attachment_info;
    } else if (gp.depth_attachment) |att| blk: {
        if (att.has_stencil) break :blk p_depth else break :blk null;
    } else null;

    dev.cmdBeginRendering(cmd_buf, &.{
        .render_area = gp.render_area,
        .layer_count = 1,
        .view_mask = 0,
        .color_attachment_count = @intCast(gp.color_attachments.len),
        .p_color_attachments = colors.ptr,
        .p_depth_attachment = p_depth,
        .p_stencil_attachment = p_stencil,
    });
}

fn recordComputePass(
    cp: *const ComputePass,
    gc: *const GraphicsCtx,
    cmd_buf: vk.CommandBuffer,
    dp: *DescriptorPool,
    buf_contracts: []const BufferContract,
) void {
    for (cp.descriptor_sets, 0..) |set_id, i| {
        dp.bindSet(cmd_buf, gc, set_id, .compute, cp.pipeline.layout, @intCast(i));
    }

    cp.pipeline.bind(gc, cmd_buf);

    if (cp.indirect) |ind| {
        cp.pipeline.dispatchIndirect(gc, cmd_buf, .{
            .push_constant = ind.push_constant,
            .buffer = buf_contracts[ind.buffer.index].buffer.handle,
            .offset = ind.offset,
        });
    } else {
        cp.pipeline.dispatch(gc, cmd_buf, .{
            .push_constant = cp.dispatch.push_constant,
            .group_count_x = cp.dispatch.group_count_x,
            .group_count_y = cp.dispatch.group_count_y,
            .group_count_z = cp.dispatch.group_count_z,
        });
    }
}

fn recordTransferPass(
    tp: *const TransferPass,
    gc: *const GraphicsCtx,
    cmd_buf: vk.CommandBuffer,
    buf_contracts: []const BufferContract,
    img_contracts: []const ImageContract,
) void {
    for (tp.fills.items) |fill| {
        gc.dev.cmdFillBuffer(
            cmd_buf,
            buf_contracts[fill.buffer.index].buffer.handle,
            fill.offset,
            fill.size,
            fill.value,
        );
    }
    for (tp.clears.items) |clear| {
        gc.dev.cmdClearColorImage(
            cmd_buf,
            img_contracts[clear.image.index].image,
            .transfer_dst_optimal,
            &clear.color,
            &[_]vk.ImageSubresourceRange{clear.range},
        );
    }
}

fn flushBarriers(
    gc: *const GraphicsCtx,
    cmd_buf: vk.CommandBuffer,
    img_bars: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2),
    buf_bars: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2),
) void {
    if (img_bars.items.len == 0 and buf_bars.items.len == 0) return;
    gc.dev.cmdPipelineBarrier2(cmd_buf, &.{
        .buffer_memory_barrier_count = @intCast(buf_bars.items.len),
        .p_buffer_memory_barriers = buf_bars.items.ptr,
        .image_memory_barrier_count = @intCast(img_bars.items.len),
        .p_image_memory_barriers = img_bars.items.ptr,
    });
}

fn emitFinalBarriers(
    self: *const RenderGraph,
    image_states: []ImageTrackedState,
    buffer_states: []BufferTrackedState,
    qf: u32,
    gc: *const GraphicsCtx,
    cmd_buf: vk.CommandBuffer,
    alloc: Allocator,
) Allocator.Error!void {
    var img_bars: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty;
    var buf_bars: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty;

    for (self.images.items, image_states) |contract, state| {
        if (!imageUsageEqual(state, contract.end_usage)) {
            try img_bars.append(alloc, .{
                .src_stage_mask = state.stage,
                .src_access_mask = state.access,
                .dst_stage_mask = contract.end_usage.stage,
                .dst_access_mask = contract.end_usage.access,
                .old_layout = state.layout,
                .new_layout = contract.end_usage.layout,
                .src_queue_family_index = qf,
                .dst_queue_family_index = qf,
                .image = contract.image,
                .subresource_range = util.fullRange(contract.aspect),
            });
        }
    }

    for (self.buffers.items, buffer_states) |contract, state| {
        if (!bufferUsageEqual(state, contract.end_usage)) {
            try buf_bars.append(alloc, .{
                .src_stage_mask = state.stage,
                .src_access_mask = state.access,
                .dst_stage_mask = contract.end_usage.stage,
                .dst_access_mask = contract.end_usage.access,
                .src_queue_family_index = qf,
                .dst_queue_family_index = qf,
                .buffer = contract.buffer.handle,
                .offset = contract.offset,
                .size = contract.size,
            });
        }
    }

    flushBarriers(gc, cmd_buf, img_bars, buf_bars);
    img_bars.deinit(alloc);
    buf_bars.deinit(alloc);
}

alloc: Allocator,
images: std.ArrayListUnmanaged(ImageContract),
buffers: std.ArrayListUnmanaged(BufferContract),
passes: std.ArrayListUnmanaged(Pass),

pub fn init(alloc: Allocator) RenderGraph {
    return .{
        .alloc = alloc,
        .images = .empty,
        .buffers = .empty,
        .passes = .empty,
    };
}
pub fn deinit(self: *RenderGraph) void {
    for (self.passes.items) |*pass| {
        switch (pass.*) {
            .graphics => |*gp| {
                self.alloc.free(gp.color_attachments);
                self.alloc.free(gp.descriptor_sets);
                gp.reads_images.deinit(self.alloc);
                gp.reads_buffers.deinit(self.alloc);
                gp.writes_images.deinit(self.alloc);
                gp.writes_buffers.deinit(self.alloc);
                gp.draws.deinit(self.alloc);
            },
            .compute => |*cp| {
                self.alloc.free(cp.descriptor_sets);
                cp.reads_images.deinit(self.alloc);
                cp.reads_buffers.deinit(self.alloc);
                cp.writes_images.deinit(self.alloc);
                cp.writes_buffers.deinit(self.alloc);
            },
            .transfer => |*tp| {
                tp.fills.deinit(self.alloc);
                tp.clears.deinit(self.alloc);
                tp.reads_images.deinit(self.alloc);
                tp.writes_images.deinit(self.alloc);
                tp.reads_buffers.deinit(self.alloc);
                tp.writes_buffers.deinit(self.alloc);
            },
        }
    }
    self.passes.deinit(self.alloc);
    self.buffers.deinit(self.alloc);
    self.images.deinit(self.alloc);
}

pub fn addImage(self: *RenderGraph, contract: ImageContract) Allocator.Error!ImageRef {
    try self.images.append(self.alloc, contract);
    return .{ .index = @intCast(self.images.items.len - 1) };
}

pub fn addBuffer(self: *RenderGraph, contract: BufferContract) Allocator.Error!BufferRef {
    try self.buffers.append(self.alloc, contract);
    return .{ .index = @intCast(self.buffers.items.len - 1) };
}

pub fn getBuffer(self: *const RenderGraph, ref: BufferRef) Buffer {
    return self.buffers.items[ref.index].buffer;
}

pub fn addGraphicsPass(self: *RenderGraph, desc: GraphicsPass) Allocator.Error!GraphicsPassHandle {
    const color_attachments = try self.alloc.dupe(ColorAttachment, desc.color_attachments);
    // descriptor_sets may be a stack literal in the caller; own a copy.
    const descriptor_sets = try self.alloc.dupe(u64, desc.descriptor_sets);
    try self.passes.append(self.alloc, .{ .graphics = .{
        .pipeline = desc.pipeline,
        .color_attachments = color_attachments,
        .depth_attachment = desc.depth_attachment,
        .stencil_attachment = desc.stencil_attachment,
        .render_area = desc.render_area,
        .descriptor_sets = descriptor_sets,
        .indirect = desc.indirect,
        .indirect_count = desc.indirect_count,
        .query_pool = desc.query_pool,
        .query_slot = desc.query_slot,
    } });
    return .{ .rg = self, .index = @intCast(self.passes.items.len - 1) };
}

pub fn addComputePass(self: *RenderGraph, desc: ComputePass) Allocator.Error!ComputePassHandle {
    // descriptor_sets may be a stack literal in the caller; own a copy.
    const descriptor_sets = try self.alloc.dupe(u64, desc.descriptor_sets);
    try self.passes.append(self.alloc, .{ .compute = .{
        .pipeline = desc.pipeline,
        .descriptor_sets = descriptor_sets,
        .dispatch = desc.dispatch,
        .indirect = desc.indirect,
        .query_pool = desc.query_pool,
        .query_slot = desc.query_slot,
    } });
    return .{ .rg = self, .index = @intCast(self.passes.items.len - 1) };
}

pub fn addTransferPass(self: *RenderGraph, desc: TransferPass) Allocator.Error!TransferPassHandle {
    try self.passes.append(self.alloc, .{ .transfer = .{
        .fills = desc.fills,
        .reads_buffers = desc.reads_buffers,
        .writes_buffers = desc.writes_buffers,
    } });
    return .{ .rg = self, .index = @intCast(self.passes.items.len - 1) };
}

/// Consumes `other` by merging its passes into `self`.
/// After this call `other` is undefined and must not be used.
pub fn merge(self: *RenderGraph, other: *RenderGraph) (Allocator.Error || error{DanglingReference})!void {
    var image_remap = std.AutoArrayHashMapUnmanaged(u32, u32){};
    defer image_remap.deinit(self.alloc);

    for (other.images.items, 0..) |o_img, o_idx| {
        var found = false;
        for (self.images.items, 0..) |s_img, s_idx| {
            if (s_img.image == o_img.image) {
                try image_remap.put(self.alloc, @intCast(o_idx), @intCast(s_idx));
                self.images.items[s_idx].end_usage = o_img.end_usage;
                found = true;
                break;
            }
        }
        if (!found) {
            const new_idx = self.images.items.len;
            try self.images.append(self.alloc, o_img);
            try image_remap.put(self.alloc, @intCast(o_idx), @intCast(new_idx));
        }
    }

    var buffer_remap = std.AutoArrayHashMapUnmanaged(u32, u32){};
    defer buffer_remap.deinit(self.alloc);

    for (other.buffers.items, 0..) |o_buf, o_idx| {
        var found = false;
        for (self.buffers.items, 0..) |s_buf, s_idx| {
            if (s_buf.buffer.handle == o_buf.buffer.handle) {
                try buffer_remap.put(self.alloc, @intCast(o_idx), @intCast(s_idx));
                self.buffers.items[s_idx].end_usage = o_buf.end_usage;
                found = true;
                break;
            }
        }
        if (!found) {
            const new_idx = self.buffers.items.len;
            try self.buffers.append(self.alloc, o_buf);
            try buffer_remap.put(self.alloc, @intCast(o_idx), @intCast(new_idx));
        }
    }

    for (other.passes.items) |*o_pass| {
        switch (o_pass.*) {
            .graphics => |*o_gp| {
                var color_attachments: []ColorAttachment = &.{};
                if (o_gp.color_attachments.len > 0) {
                    color_attachments = try self.alloc.dupe(ColorAttachment, o_gp.color_attachments);
                    for (color_attachments) |*ca| {
                        ca.image.index = image_remap.get(ca.image.index) orelse return error.DanglingReference;
                    }
                }

                var new_gp = GraphicsPass{
                    .pipeline = o_gp.pipeline,
                    .color_attachments = color_attachments,
                    .depth_attachment = if (o_gp.depth_attachment) |da| DepthAttachment{
                        .image = .{ .index = image_remap.get(da.image.index) orelse return error.DanglingReference },
                        .view = da.view,
                        .load_op = da.load_op,
                        .store_op = da.store_op,
                        .clear_depth = da.clear_depth,
                        .clear_stencil = da.clear_stencil,
                        .layout = da.layout,
                        .has_stencil = da.has_stencil,
                    } else null,
                    .stencil_attachment = if (o_gp.stencil_attachment) |sa| StencilAttachment{
                        .image = .{ .index = image_remap.get(sa.image.index) orelse return error.DanglingReference },
                        .view = sa.view,
                        .load_op = sa.load_op,
                        .store_op = sa.store_op,
                        .clear_stencil = sa.clear_stencil,
                        .layout = sa.layout,
                    } else null,
                    .render_area = o_gp.render_area,
                    .descriptor_sets = o_gp.descriptor_sets,
                    .reads_images = .empty,
                    .reads_buffers = .empty,
                    .writes_images = .empty,
                    .writes_buffers = .empty,
                    .draws = .empty,
                    .indirect = if (o_gp.indirect) |ind| DrawIndirect{
                        .push_constant = ind.push_constant,
                        .buffer = .{ .index = buffer_remap.get(ind.buffer.index) orelse return error.DanglingReference },
                        .offset = ind.offset,
                        .draw_count = ind.draw_count,
                        .stride = ind.stride,
                    } else null,
                    .indirect_count = if (o_gp.indirect_count) |ic| DrawIndirectCount{
                        .push_constant = ic.push_constant,
                        .buffer = .{ .index = buffer_remap.get(ic.buffer.index) orelse return error.DanglingReference },
                        .offset = ic.offset,
                        .count_buffer = .{ .index = buffer_remap.get(ic.count_buffer.index) orelse return error.DanglingReference },
                        .count_offset = ic.count_offset,
                        .max_draw_count = ic.max_draw_count,
                        .stride = ic.stride,
                    } else null,
                    .query_pool = o_gp.query_pool,
                    .query_slot = o_gp.query_slot,
                };

                for (o_gp.reads_images.items) |item| {
                    try new_gp.reads_images.append(self.alloc, .{ .ref = .{ .index = image_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_gp.reads_buffers.items) |item| {
                    try new_gp.reads_buffers.append(self.alloc, .{ .ref = .{ .index = buffer_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_gp.writes_images.items) |item| {
                    try new_gp.writes_images.append(self.alloc, .{ .ref = .{ .index = image_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_gp.writes_buffers.items) |item| {
                    try new_gp.writes_buffers.append(self.alloc, .{ .ref = .{ .index = buffer_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                try new_gp.draws.appendSlice(self.alloc, o_gp.draws.items);

                try self.passes.append(self.alloc, .{ .graphics = new_gp });
            },
            .compute => |*o_cp| {
                var new_cp = ComputePass{
                    .pipeline = o_cp.pipeline,
                    .descriptor_sets = o_cp.descriptor_sets,
                    .reads_images = .empty,
                    .reads_buffers = .empty,
                    .writes_images = .empty,
                    .writes_buffers = .empty,
                    .dispatch = o_cp.dispatch,
                    .indirect = if (o_cp.indirect) |ind| DispatchIndirect{
                        .push_constant = ind.push_constant,
                        .buffer = .{ .index = buffer_remap.get(ind.buffer.index) orelse return error.DanglingReference },
                        .offset = ind.offset,
                    } else null,
                    .query_pool = o_cp.query_pool,
                    .query_slot = o_cp.query_slot,
                };

                for (o_cp.reads_images.items) |item| {
                    try new_cp.reads_images.append(self.alloc, .{ .ref = .{ .index = image_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_cp.reads_buffers.items) |item| {
                    try new_cp.reads_buffers.append(self.alloc, .{ .ref = .{ .index = buffer_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_cp.writes_images.items) |item| {
                    try new_cp.writes_images.append(self.alloc, .{ .ref = .{ .index = image_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_cp.writes_buffers.items) |item| {
                    try new_cp.writes_buffers.append(self.alloc, .{ .ref = .{ .index = buffer_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }

                try self.passes.append(self.alloc, .{ .compute = new_cp });
            },
            .transfer => |*o_tp| {
                var new_tp = TransferPass{
                    .fills = .empty,
                    .reads_buffers = .empty,
                    .writes_buffers = .empty,
                };

                for (o_tp.reads_images.items) |item| {
                    try new_tp.reads_images.append(self.alloc, .{ .ref = .{ .index = image_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_tp.writes_images.items) |item| {
                    try new_tp.writes_images.append(self.alloc, .{ .ref = .{ .index = image_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_tp.clears.items) |item| {
                    var c = item;
                    c.image.index = image_remap.get(item.image.index) orelse return error.DanglingReference;
                    try new_tp.clears.append(self.alloc, c);
                }
                for (o_tp.reads_buffers.items) |item| {
                    try new_tp.reads_buffers.append(self.alloc, .{ .ref = .{ .index = buffer_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_tp.writes_buffers.items) |item| {
                    try new_tp.writes_buffers.append(self.alloc, .{ .ref = .{ .index = buffer_remap.get(item.ref.index) orelse return error.DanglingReference }, .usage = item.usage });
                }
                for (o_tp.fills.items) |item| {
                    try new_tp.fills.append(self.alloc, .{
                        .buffer = .{ .index = buffer_remap.get(item.buffer.index) orelse return error.DanglingReference },
                        .offset = item.offset,
                        .size = item.size,
                        .value = item.value,
                    });
                }

                try self.passes.append(self.alloc, .{ .transfer = new_tp });
            },
        }
    }

    other.deinit();
    other.* = undefined;
}

pub fn run(
    self: *const RenderGraph,
    gc: *const GraphicsCtx,
    cmd_buf: vk.CommandBuffer,
    dp: *DescriptorPool,
) Allocator.Error!void {
    const dev = &gc.dev;
    const qf = gc.graphics_family;

    var arena = std.heap.ArenaAllocator.init(self.alloc);
    defer arena.deinit();
    const alloc = arena.allocator();

    const num_images = self.images.items.len;
    const num_buffers = self.buffers.items.len;

    var image_states: []ImageTrackedState = &.{};
    var buffer_states: []BufferTrackedState = &.{};

    if (num_images > 0) {
        image_states = try alloc.alloc(ImageTrackedState, num_images);
        for (self.images.items, 0..) |contract, i| {
            image_states[i] = .{
                .layout = contract.start_usage.layout,
                .stage = contract.start_usage.stage,
                .access = contract.start_usage.access,
            };
        }
    }

    if (num_buffers > 0) {
        buffer_states = try alloc.alloc(BufferTrackedState, num_buffers);
        for (self.buffers.items, 0..) |contract, i| {
            buffer_states[i] = .{
                .stage = contract.start_usage.stage,
                .access = contract.start_usage.access,
            };
        }
    }

    const max_capacity = @max(num_images, num_buffers) * 2 + 16;
    var pass_images = std.AutoArrayHashMapUnmanaged(u32, ImageUsage){};
    try pass_images.ensureTotalCapacity(alloc, max_capacity);
    var pass_buffers = std.AutoArrayHashMapUnmanaged(u32, BufferUsage){};
    try pass_buffers.ensureTotalCapacity(alloc, max_capacity);

    var img_bars: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty;
    var buf_bars: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty;

    var prev_gp: ?*const GraphicsPass = null;
    const dev_proxy = dev;

    for (self.passes.items) |*pass| {
        switch (pass.*) {
            .graphics => |*gp| {
                try collectPassRequirementsGraphics(&pass_images, &pass_buffers, gp, alloc);
                try emitPassBarriers(
                    pass_images,
                    pass_buffers,
                    image_states,
                    buffer_states,
                    self.images.items,
                    self.buffers.items,
                    qf,
                    &img_bars,
                    &buf_bars,
                    alloc,
                );

                const can_merge = if (prev_gp) |prev| blk: {
                    if (img_bars.items.len > 0 or buf_bars.items.len > 0) break :blk false;
                    break :blk renderTargetsEqual(prev, gp);
                } else false;

                if (gp.query_pool) |pool| {
                    dev_proxy.cmdWriteTimestamp2(cmd_buf, .{ .all_commands_bit = true }, pool, gp.query_slot);
                }

                if (can_merge) {
                    recordGraphicsCommands(gp, gc, cmd_buf, dp, self.buffers.items);
                } else {
                    if (prev_gp != null) dev_proxy.cmdEndRendering(cmd_buf);
                    flushBarriers(gc, cmd_buf, img_bars, buf_bars);
                    img_bars.clearRetainingCapacity();
                    buf_bars.clearRetainingCapacity();
                    try beginRendering(gp, dev, cmd_buf, alloc);
                    recordGraphicsCommands(gp, gc, cmd_buf, dp, self.buffers.items);
                }

                if (gp.query_pool) |pool| {
                    dev_proxy.cmdWriteTimestamp2(cmd_buf, .{ .all_commands_bit = true }, pool, gp.query_slot + 1);
                }

                prev_gp = gp;
            },
            .compute => |*cp| {
                if (prev_gp != null) {
                    dev_proxy.cmdEndRendering(cmd_buf);
                    prev_gp = null;
                }
                try collectPassRequirementsCompute(&pass_images, &pass_buffers, cp, alloc);
                try emitPassBarriers(
                    pass_images,
                    pass_buffers,
                    image_states,
                    buffer_states,
                    self.images.items,
                    self.buffers.items,
                    qf,
                    &img_bars,
                    &buf_bars,
                    alloc,
                );
                flushBarriers(gc, cmd_buf, img_bars, buf_bars);
                img_bars.clearRetainingCapacity();
                buf_bars.clearRetainingCapacity();

                if (cp.query_pool) |pool| {
                    dev_proxy.cmdWriteTimestamp2(cmd_buf, .{ .all_commands_bit = true }, pool, cp.query_slot);
                }

                recordComputePass(cp, gc, cmd_buf, dp, self.buffers.items);

                if (cp.query_pool) |pool| {
                    dev_proxy.cmdWriteTimestamp2(cmd_buf, .{ .all_commands_bit = true }, pool, cp.query_slot + 1);
                }
            },
            .transfer => |*tp| {
                if (prev_gp != null) {
                    dev_proxy.cmdEndRendering(cmd_buf);
                    prev_gp = null;
                }
                try collectPassRequirementsTransfer(&pass_images, &pass_buffers, tp, alloc);
                try emitPassBarriers(
                    pass_images,
                    pass_buffers,
                    image_states,
                    buffer_states,
                    self.images.items,
                    self.buffers.items,
                    qf,
                    &img_bars,
                    &buf_bars,
                    alloc,
                );
                flushBarriers(gc, cmd_buf, img_bars, buf_bars);
                img_bars.clearRetainingCapacity();
                buf_bars.clearRetainingCapacity();

                recordTransferPass(tp, gc, cmd_buf, self.buffers.items, self.images.items);
            },
        }
    }

    if (prev_gp != null) dev_proxy.cmdEndRendering(cmd_buf);

    try emitFinalBarriers(self, image_states, buffer_states, qf, gc, cmd_buf, alloc);
}

test "builder: add images, buffers, passes and deinit" {
    const alloc = std.testing.allocator;

    var rg = RenderGraph.init(alloc);
    defer rg.deinit();

    const rt_img: vk.Image = @enumFromInt(0);
    const rt_view: vk.ImageView = @enumFromInt(0);
    const depth_img: vk.Image = @enumFromInt(0);
    const depth_view: vk.ImageView = @enumFromInt(0);
    const vb: Buffer = .{ .handle = @enumFromInt(0) };
    const count_buf: Buffer = .{ .handle = @enumFromInt(1) };

    const rt = try rg.addImage(.{
        .image = rt_img,
        .start_usage = .{
            .stage = .{ .all_commands_bit = true },
            .access = .{ .memory_write_bit = true },
            .layout = .undefined,
        },
        .end_usage = .{
            .stage = .{ .all_transfer_bit = true },
            .access = .{ .transfer_read_bit = true },
            .layout = .transfer_src_optimal,
        },
    });
    try std.testing.expectEqual(@as(u32, 0), rt.index);

    const depth = try rg.addImage(.{
        .image = depth_img,
        .start_usage = .{
            .stage = .{ .all_commands_bit = true },
            .access = .{ .memory_write_bit = true },
            .layout = .undefined,
        },
        .end_usage = .{
            .stage = .{ .early_fragment_tests_bit = true },
            .access = .{ .depth_stencil_attachment_write_bit = true },
            .layout = .depth_stencil_attachment_optimal,
        },
    });
    try std.testing.expectEqual(@as(u32, 1), depth.index);

    const buf_a = try rg.addBuffer(.{
        .buffer = vb,
        .offset = 0,
        .size = 64,
        .start_usage = .{
            .stage = .{ .all_commands_bit = true },
            .access = .{ .memory_write_bit = true },
        },
        .end_usage = .{
            .stage = .{ .vertex_input_bit = true },
            .access = .{ .memory_read_bit = true },
        },
    });
    try std.testing.expectEqual(@as(u32, 0), buf_a.index);

    const buf_b = try rg.addBuffer(.{
        .buffer = count_buf,
        .offset = 0,
        .size = 16,
        .start_usage = .{
            .stage = .{ .all_commands_bit = true },
            .access = .{ .memory_write_bit = true },
        },
        .end_usage = .{
            .stage = .{ .vertex_input_bit = true },
            .access = .{ .memory_read_bit = true },
        },
    });
    try std.testing.expectEqual(@as(u32, 1), buf_b.index);

    const gp_desc = GraphicsPass{
        .pipeline = undefined,
        .color_attachments = &.{.{ .image = rt, .view = rt_view }},
        .depth_attachment = .{ .image = depth, .view = depth_view },
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1920, .height = 1080 } },
    };

    const gp_handle = try rg.addGraphicsPass(gp_desc);
    try std.testing.expectEqual(@as(u32, 0), gp_handle.index);

    try gp_handle.readBuffer(buf_a, .{
        .stage = .{ .vertex_input_bit = true },
        .access = .{ .memory_read_bit = true },
    });
    try gp_handle.writeImage(rt, .{
        .stage = .{ .color_attachment_output_bit = true },
        .access = .{ .color_attachment_write_bit = true },
        .layout = .color_attachment_optimal,
    });
    try gp_handle.draw(.{ .vertex_count = 3 });
    gp_handle.setDescriptorSets(&.{ 7 });

    try std.testing.expectEqual(@as(usize, 2), rg.images.items.len);
    try std.testing.expectEqual(@as(usize, 2), rg.buffers.items.len);
    try std.testing.expectEqual(@as(usize, 1), rg.passes.items.len);

    {
        const gp = &rg.passes.items[0].graphics;
        try std.testing.expectEqual(@as(usize, 1), gp.draws.items.len);
        try std.testing.expectEqual(@as(usize, 0), gp.reads_images.items.len);
        try std.testing.expectEqual(@as(usize, 1), gp.reads_buffers.items.len);
        try std.testing.expectEqual(@as(usize, 1), gp.writes_images.items.len);
        try std.testing.expectEqual(@as(usize, 0), gp.writes_buffers.items.len);
        try std.testing.expectEqual(@as(u32, 3), gp.draws.items[0].vertex_count);
        try std.testing.expectEqual(@as(usize, 1), gp.descriptor_sets.len);
        try std.testing.expectEqual(@as(u64, 7), gp.descriptor_sets[0]);

        try std.testing.expectEqual(@as(u32, 1), gp.color_attachments.len);
        try std.testing.expectEqual(@as(u32, 0), gp.color_attachments[0].image.index);
        try std.testing.expectEqual(rt_view, gp.color_attachments[0].view);
        try std.testing.expectEqual(@as(u32, 1), gp.depth_attachment.?.image.index);
    }

    const cp_desc = ComputePass{
        .pipeline = undefined,
        .dispatch = .{ .group_count_x = 8, .group_count_y = 8, .group_count_z = 1 },
        .indirect = null,
    };

    const cp_handle = try rg.addComputePass(cp_desc);
    try std.testing.expectEqual(@as(u32, 1), cp_handle.index);

    try cp_handle.readImage(rt, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_read_bit = true },
        .layout = .read_only_optimal,
    });
    try cp_handle.writeImage(rt, .{
        .stage = .{ .compute_shader_bit = true },
        .access = .{ .shader_write_bit = true },
        .layout = .general,
    });
    cp_handle.setDescriptorSets(&.{ 42 });

    try std.testing.expectEqual(@as(usize, 2), rg.passes.items.len);

    {
        const cp = &rg.passes.items[1].compute;
        try std.testing.expectEqual(@as(usize, 1), cp.reads_images.items.len);
        try std.testing.expectEqual(@as(usize, 1), cp.writes_images.items.len);
        try std.testing.expectEqual(@as(u32, 8), cp.dispatch.group_count_x);
        try std.testing.expectEqual(@as(u64, 42), cp.descriptor_sets[0]);
    }
}

test "indirect: drawIndirect and drawIndirectCount with BufferRef" {
    const alloc = std.testing.allocator;

    var rg = RenderGraph.init(alloc);
    defer rg.deinit();

    const vb: Buffer = .{ .handle = @enumFromInt(100) };
    const cb: Buffer = .{ .handle = @enumFromInt(200) };

    const ind_buf = try rg.addBuffer(.{
        .buffer = vb, .offset = 0, .size = 256,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });
    const cnt_buf = try rg.addBuffer(.{
        .buffer = cb, .offset = 0, .size = 16,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });

    const pass = try rg.addGraphicsPass(.{
        .pipeline = undefined,
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1, .height = 1 } },
    });

    pass.drawIndirect(.{
        .buffer = ind_buf,
        .draw_count = 10,
    });
    pass.drawIndirectCount(.{
        .buffer = ind_buf,
        .count_buffer = cnt_buf,
        .max_draw_count = 10,
    });

    {
        const gp = &rg.passes.items[0].graphics;
        try std.testing.expect(gp.indirect != null);
        try std.testing.expect(gp.indirect_count != null);
        try std.testing.expectEqual(@as(u32, 0), gp.indirect.?.buffer.index);
        try std.testing.expectEqual(@as(u32, 0), gp.indirect_count.?.buffer.index);
        try std.testing.expectEqual(@as(u32, 1), gp.indirect_count.?.count_buffer.index);
    }
}

test "indirect: dispatchIndirect with BufferRef" {
    const alloc = std.testing.allocator;

    var rg = RenderGraph.init(alloc);
    defer rg.deinit();

    const vb: Buffer = .{ .handle = @enumFromInt(100) };

    const ind_buf = try rg.addBuffer(.{
        .buffer = vb, .offset = 0, .size = 64,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });

    _ = try rg.addComputePass(.{
        .pipeline = undefined,
        .dispatch = .{ .group_count_x = 1, .group_count_y = 1, .group_count_z = 1 },
        .indirect = .{
            .buffer = ind_buf,
        },
    });

    const cp = &rg.passes.items[0].compute;
    try std.testing.expect(cp.indirect != null);
    try std.testing.expectEqual(@as(u32, 0), cp.indirect.?.buffer.index);
}

test "merge: explicit read + indirect draw merge into one barrier" {
    const alloc = std.testing.allocator;

    var rg = RenderGraph.init(alloc);
    defer rg.deinit();

    const rt_img: vk.Image = @enumFromInt(0);
    const rt_view: vk.ImageView = @enumFromInt(0);
    const vb: Buffer = .{ .handle = @enumFromInt(100) };

    const rt = try rg.addImage(.{
        .image = rt_img,
        .start_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
        .end_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
    });
    const ind_buf = try rg.addBuffer(.{
        .buffer = vb, .offset = 0, .size = 256,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });

    const pass = try rg.addGraphicsPass(.{
        .pipeline = undefined,
        .color_attachments = &.{.{ .image = rt, .view = rt_view }},
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1, .height = 1 } },
    });

    try pass.readBuffer(ind_buf, .{
        .stage = .{ .vertex_shader_bit = true },
        .access = .{ .shader_read_bit = true },
    });
    pass.drawIndirect(.{
        .buffer = ind_buf,
        .draw_count = 5,
    });

    var pass_images = std.AutoArrayHashMapUnmanaged(u32, ImageUsage){};
    var pass_buffers = std.AutoArrayHashMapUnmanaged(u32, BufferUsage){};

    const gp = &rg.passes.items[0].graphics;
    try collectPassRequirementsGraphics(&pass_images, &pass_buffers, gp, alloc);
    defer {
        pass_images.deinit(alloc);
        pass_buffers.deinit(alloc);
    }

    try std.testing.expectEqual(@as(usize, 1), pass_buffers.count());

    const merged = pass_buffers.get(0).?;
    try std.testing.expect(merged.stage.vertex_shader_bit);
    try std.testing.expect(merged.stage.draw_indirect_bit);
    try std.testing.expect(merged.access.shader_read_bit);
    try std.testing.expect(merged.access.indirect_command_read_bit);
}

test "barrier helpers: image usage equality" {
    const a: ImageTrackedState = .{ .layout = .undefined, .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } };
    const b: ImageUsage = .{ .layout = .undefined, .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } };
    try std.testing.expect(imageUsageEqual(a, b));

    const c: ImageUsage = .{ .layout = .color_attachment_optimal, .stage = .{ .color_attachment_output_bit = true }, .access = .{ .color_attachment_write_bit = true } };
    try std.testing.expect(!imageUsageEqual(a, c));
}

test "barrier helpers: buffer usage equality" {
    const a: BufferTrackedState = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } };
    const b: BufferUsage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } };
    try std.testing.expect(bufferUsageEqual(a, b));

    const c: BufferUsage = .{ .stage = .{ .vertex_input_bit = true }, .access = .{ .memory_read_bit = true } };
    try std.testing.expect(!bufferUsageEqual(a, c));
}

test "attachmentLayoutToUsage: color attachment" {
    const u = attachmentLayoutToUsage(.color_attachment_optimal);
    try std.testing.expect(u.stage.color_attachment_output_bit);
    try std.testing.expectEqual(.color_attachment_optimal, u.layout);
}

test "attachmentLayoutToUsage: depth read-only" {
    const u = attachmentLayoutToUsage(.depth_stencil_read_only_optimal);
    try std.testing.expect(u.stage.early_fragment_tests_bit);
    try std.testing.expect(!u.access.depth_stencil_attachment_write_bit);
    try std.testing.expect(u.access.depth_stencil_attachment_read_bit);
}

test "merge helpers: stage and access bits OR correctly" {
    const a = vk.PipelineStageFlags2{ .vertex_shader_bit = true };
    const b = vk.PipelineStageFlags2{ .draw_indirect_bit = true };
    const merged = mergeStage(a, b);
    try std.testing.expect(merged.vertex_shader_bit);
    try std.testing.expect(merged.draw_indirect_bit);
    try std.testing.expect(!merged.fragment_shader_bit);

    const acc_a = vk.AccessFlags2{ .shader_read_bit = true };
    const acc_b = vk.AccessFlags2{ .indirect_command_read_bit = true };
    const acc_merged = mergeAccess(acc_a, acc_b);
    try std.testing.expect(acc_merged.shader_read_bit);
    try std.testing.expect(acc_merged.indirect_command_read_bit);
}

test "merge helpers: buffer merge combines both" {
    const a = BufferUsage{ .stage = .{ .vertex_shader_bit = true }, .access = .{ .shader_read_bit = true } };
    const b = BufferUsage{ .stage = .{ .draw_indirect_bit = true }, .access = .{ .indirect_command_read_bit = true } };
    const m = mergeBufferUsage(a, b);
    try std.testing.expect(m.stage.vertex_shader_bit);
    try std.testing.expect(m.stage.draw_indirect_bit);
    try std.testing.expect(m.access.shader_read_bit);
    try std.testing.expect(m.access.indirect_command_read_bit);
}

test "merge: two disjoint graphs concatenate cleanly" {
    const alloc = std.testing.allocator;

    var rg1 = RenderGraph.init(alloc);
    defer rg1.deinit();

    const img1: vk.Image = @enumFromInt(1);
    const buf1: Buffer = .{ .handle = @enumFromInt(100) };

    _ = try rg1.addImage(.{
        .image = img1,
        .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true }, .layout = .undefined },
        .end_usage = .{ .stage = .{ .all_transfer_bit = true }, .access = .{ .transfer_read_bit = true }, .layout = .transfer_src_optimal },
    });
    _ = try rg1.addBuffer(.{
        .buffer = buf1, .offset = 0, .size = 64,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });
    _ = try rg1.addGraphicsPass(.{
        .pipeline = undefined,
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1, .height = 1 } },
    });

    var rg2 = RenderGraph.init(alloc);

    const img2: vk.Image = @enumFromInt(2);
    const buf2: Buffer = .{ .handle = @enumFromInt(200) };

    _ = try rg2.addImage(.{
        .image = img2,
        .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true }, .layout = .undefined },
        .end_usage = .{ .stage = .{ .color_attachment_output_bit = true }, .access = .{ .color_attachment_write_bit = true }, .layout = .color_attachment_optimal },
    });
    _ = try rg2.addBuffer(.{
        .buffer = buf2, .offset = 0, .size = 128,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });
    _ = try rg2.addComputePass(.{
        .pipeline = undefined,
        .dispatch = .{ .group_count_x = 1, .group_count_y = 1, .group_count_z = 1 },
    });

    try rg1.merge(&rg2);

    try std.testing.expectEqual(@as(usize, 2), rg1.images.items.len);
    try std.testing.expectEqual(@as(usize, 2), rg1.buffers.items.len);
    try std.testing.expectEqual(@as(usize, 2), rg1.passes.items.len);
    try std.testing.expectEqual(img1, rg1.images.items[0].image);
    try std.testing.expectEqual(img2, rg1.images.items[1].image);
    try std.testing.expectEqual(buf1, rg1.buffers.items[0].buffer);
    try std.testing.expectEqual(@as(u64, 64), rg1.buffers.items[0].size);
    try std.testing.expectEqual(buf2, rg1.buffers.items[1].buffer);
    try std.testing.expectEqual(@as(u64, 128), rg1.buffers.items[1].size);
    try std.testing.expect(rg1.passes.items[0] == .graphics);
    try std.testing.expect(rg1.passes.items[1] == .compute);
}

test "merge: overlapping image dedup and end_usage inheritance" {
    const alloc = std.testing.allocator;

    var rg1 = RenderGraph.init(alloc);
    defer rg1.deinit();

    const shared_img: vk.Image = @enumFromInt(42);
    const shared_buf: Buffer = .{ .handle = @enumFromInt(7) };

    _ = try rg1.addImage(.{
        .image = shared_img,
        .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true }, .layout = .undefined },
        .end_usage = .{ .stage = .{ .all_transfer_bit = true }, .access = .{ .transfer_read_bit = true }, .layout = .transfer_src_optimal },
    });
    _ = try rg1.addBuffer(.{
        .buffer = shared_buf, .offset = 0, .size = 32,
        .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } },
        .end_usage = .{ .stage = .{ .vertex_input_bit = true }, .access = .{ .memory_read_bit = true } },
    });
    const pa = try rg1.addGraphicsPass(.{
        .pipeline = undefined,
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1, .height = 1 } },
    });
    try pa.draw(.{ .vertex_count = 3 });

    var rg2 = RenderGraph.init(alloc);

    _ = try rg2.addImage(.{
        .image = shared_img,
        .start_usage = .{ .stage = .{ .color_attachment_output_bit = true }, .access = .{ .color_attachment_write_bit = true }, .layout = .color_attachment_optimal },
        .end_usage = .{ .stage = .{ .all_transfer_bit = true }, .access = .{ .transfer_read_bit = true }, .layout = .present_src_khr },
    });
    _ = try rg2.addBuffer(.{
        .buffer = shared_buf, .offset = 0, .size = 32,
        .start_usage = .{ .stage = .{ .vertex_input_bit = true }, .access = .{ .memory_read_bit = true } },
        .end_usage = .{ .stage = .{ .vertex_input_bit = true }, .access = .{ .memory_read_bit = true } },
    });
    const pb = try rg2.addGraphicsPass(.{
        .pipeline = undefined,
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1, .height = 1 } },
    });
    try pb.draw(.{ .vertex_count = 5 });

    try rg1.merge(&rg2);

    try std.testing.expectEqual(@as(usize, 1), rg1.images.items.len);
    try std.testing.expectEqual(@as(usize, 1), rg1.buffers.items.len);
    try std.testing.expectEqual(@as(usize, 2), rg1.passes.items.len);

    try std.testing.expectEqual(shared_img, rg1.images.items[0].image);
    try std.testing.expectEqual(.undefined, rg1.images.items[0].start_usage.layout);
    try std.testing.expectEqual(.present_src_khr, rg1.images.items[0].end_usage.layout);

    try std.testing.expectEqual(shared_buf, rg1.buffers.items[0].buffer);
    try std.testing.expect(rg1.buffers.items[0].end_usage.stage.vertex_input_bit);

    const gp0 = &rg1.passes.items[0].graphics;
    const gp1 = &rg1.passes.items[1].graphics;
    try std.testing.expectEqual(@as(u32, 3), gp0.draws.items[0].vertex_count);
    try std.testing.expectEqual(@as(u32, 5), gp1.draws.items[0].vertex_count);
}

test "merge: empty other is a no-op" {
    const alloc = std.testing.allocator;

    var rg1 = RenderGraph.init(alloc);
    defer rg1.deinit();

    const img: vk.Image = @enumFromInt(1);
    _ = try rg1.addImage(.{
        .image = img,
        .start_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
        .end_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
    });

    var empty = RenderGraph.init(alloc);

    try rg1.merge(&empty);

    try std.testing.expectEqual(@as(usize, 1), rg1.images.items.len);
    try std.testing.expectEqual(@as(usize, 0), rg1.buffers.items.len);
    try std.testing.expectEqual(@as(usize, 0), rg1.passes.items.len);
}

test "merge: refs are correctly rebased through dedup" {
    const alloc = std.testing.allocator;

    var rg1 = RenderGraph.init(alloc);
    defer rg1.deinit();

    const shared: vk.Image = @enumFromInt(99);
    const other_img: vk.Image = @enumFromInt(88);
    const view: vk.ImageView = @enumFromInt(77);

    const a = try rg1.addImage(.{
        .image = shared,
        .start_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
        .end_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
    });
    _ = try rg1.addGraphicsPass(.{
        .pipeline = undefined,
        .color_attachments = &.{.{ .image = a, .view = view }},
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1, .height = 1 } },
    });

    var rg2 = RenderGraph.init(alloc);

    const b = try rg2.addImage(.{
        .image = shared,
        .start_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
        .end_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
    });
    const c = try rg2.addImage(.{
        .image = other_img,
        .start_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
        .end_usage = .{ .stage = .{}, .access = .{}, .layout = .undefined },
    });
    const p2 = try rg2.addGraphicsPass(.{
        .pipeline = undefined,
        .color_attachments = &.{ .{ .image = b, .view = view }, .{ .image = c, .view = view } },
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1, .height = 1 } },
    });
    try p2.readImage(c, .{ .stage = .{}, .access = .{}, .layout = .undefined });

    try rg1.merge(&rg2);

    try std.testing.expectEqual(@as(usize, 2), rg1.images.items.len);
    try std.testing.expectEqual(shared, rg1.images.items[0].image);
    try std.testing.expectEqual(other_img, rg1.images.items[1].image);

    const merged = &rg1.passes.items[1].graphics;
    try std.testing.expectEqual(@as(u32, 0), merged.color_attachments[0].image.index);
    try std.testing.expectEqual(@as(u32, 1), merged.color_attachments[1].image.index);
    try std.testing.expectEqual(@as(u32, 1), merged.reads_images.items[0].ref.index);
}

test "merge: indirect BufferRef is rebased" {
    const alloc = std.testing.allocator;

    var rg1 = RenderGraph.init(alloc);
    defer rg1.deinit();

    const buf: Buffer = .{ .handle = @enumFromInt(10) };
    _ = try rg1.addBuffer(.{
        .buffer = buf, .offset = 0, .size = 256,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });

    var rg2 = RenderGraph.init(alloc);

    const ind_buf = try rg2.addBuffer(.{
        .buffer = buf, .offset = 0, .size = 256,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });
    const p = try rg2.addGraphicsPass(.{
        .pipeline = undefined,
        .render_area = .{ .offset = .{ .x = 0, .y = 0 }, .extent = .{ .width = 1, .height = 1 } },
    });
    p.drawIndirect(.{ .buffer = ind_buf, .draw_count = 10 });

    try rg1.merge(&rg2);

    try std.testing.expectEqual(@as(usize, 1), rg1.buffers.items.len);
    const merged = &rg1.passes.items[0].graphics;
    try std.testing.expect(merged.indirect != null);
    try std.testing.expectEqual(@as(u32, 0), merged.indirect.?.buffer.index);
}

test "transfer: fillBuffer records write with transfer barrier" {
    const alloc = std.testing.allocator;

    var rg = RenderGraph.init(alloc);
    defer rg.deinit();

    const vb: Buffer = .{ .handle = @enumFromInt(100) };

    const buf = try rg.addBuffer(.{
        .buffer = vb, .offset = 0, .size = 256,
        .start_usage = .{ .stage = .{ .all_commands_bit = true }, .access = .{ .memory_write_bit = true } },
        .end_usage = .{ .stage = .{ .vertex_input_bit = true }, .access = .{ .memory_read_bit = true } },
    });

    const tp = try rg.addTransferPass(.{});
    try tp.fillBuffer(.{ .buffer = buf, .offset = 0, .size = 256, .value = 0 });

    {
        const transfer = &rg.passes.items[0].transfer;
        try std.testing.expectEqual(@as(usize, 1), transfer.fills.items.len);
        try std.testing.expectEqual(@as(usize, 1), transfer.writes_buffers.items.len);
        try std.testing.expectEqual(@as(u32, 0), transfer.fills.items[0].buffer.index);
        try std.testing.expectEqual(@as(u64, 0), transfer.fills.items[0].offset);
        try std.testing.expectEqual(@as(u64, 256), transfer.fills.items[0].size);
        try std.testing.expectEqual(@as(u32, 0), transfer.fills.items[0].value);
        try std.testing.expect(transfer.writes_buffers.items[0].usage.stage.all_transfer_bit);
        try std.testing.expect(transfer.writes_buffers.items[0].usage.access.transfer_write_bit);
    }
}

test "transfer: collectPassRequirementsTransfer merges buffer writes" {
    const alloc = std.testing.allocator;

    var rg = RenderGraph.init(alloc);
    defer rg.deinit();

    const vb: Buffer = .{ .handle = @enumFromInt(100) };

    const buf = try rg.addBuffer(.{
        .buffer = vb, .offset = 0, .size = 256,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });

    const tp_handle = try rg.addTransferPass(.{});
    try tp_handle.readBuffer(buf, .{
        .stage = .{ .vertex_input_bit = true },
        .access = .{ .memory_read_bit = true },
    });
    try tp_handle.fillBuffer(.{ .buffer = buf, .offset = 0, .size = 256, .value = 42 });

    var pass_images = std.AutoArrayHashMapUnmanaged(u32, ImageUsage){};
    defer pass_images.deinit(alloc);
    var pass_buffers = std.AutoArrayHashMapUnmanaged(u32, BufferUsage){};
    defer pass_buffers.deinit(alloc);

    const tp = &rg.passes.items[0].transfer;
    try collectPassRequirementsTransfer(&pass_images, &pass_buffers, tp, alloc);

    try std.testing.expectEqual(@as(usize, 0), pass_images.count());
    try std.testing.expectEqual(@as(usize, 1), pass_buffers.count());
    const merged = pass_buffers.get(0).?;
    try std.testing.expect(merged.stage.vertex_input_bit);
    try std.testing.expect(merged.stage.all_transfer_bit);
    try std.testing.expect(merged.access.memory_read_bit);
    try std.testing.expect(merged.access.transfer_write_bit);
}

test "merge: transfer pass refs are rebased through dedup" {
    const alloc = std.testing.allocator;

    var rg1 = RenderGraph.init(alloc);
    defer rg1.deinit();

    const buf: Buffer = .{ .handle = @enumFromInt(10) };
    _ = try rg1.addBuffer(.{
        .buffer = buf, .offset = 0, .size = 256,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });

    var rg2 = RenderGraph.init(alloc);

    const fill_buf = try rg2.addBuffer(.{
        .buffer = buf, .offset = 0, .size = 256,
        .start_usage = .{ .stage = .{}, .access = .{} },
        .end_usage = .{ .stage = .{}, .access = .{} },
    });
    const tp = try rg2.addTransferPass(.{});
    try tp.fillBuffer(.{ .buffer = fill_buf, .offset = 0, .size = 256, .value = 0 });

    try rg1.merge(&rg2);

    try std.testing.expectEqual(@as(usize, 1), rg1.buffers.items.len);
    try std.testing.expectEqual(@as(usize, 1), rg1.passes.items.len);
    try std.testing.expect(rg1.passes.items[0] == .transfer);
    const merged = &rg1.passes.items[0].transfer;
    try std.testing.expectEqual(@as(usize, 1), merged.fills.items.len);
    try std.testing.expectEqual(@as(u32, 0), merged.fills.items[0].buffer.index);
}
