//! Common public transport API and its type-erased backend vtable.
const std = @import("std");
const vk = @import("vulkan");
const root = @import("../root.zig");
const zprobe = @import("zprobe");

const Staging = @import("Staging.zig");

pub const FreeFn = *const fn (ctx: ?*anyopaque, ptr: *anyopaque) void;

/// Errors produced while adding upload tasks to backend-owned queues.
pub const QueueError = error{OutOfMemory};
pub const UploadError = QueueError || ReadinessError;
pub const ImageUploadError = UploadError || error{UnsupportedFormat};
pub const GroupUploadError = UploadError || error{GroupAlreadyEnded};
pub const GroupImageUploadError = ImageUploadError || error{GroupAlreadyEnded};
pub const GroupCreateError = UploadError;

/// Errors returned by Vulkan calls used for querying GPU completion or
/// submitting/draining command buffers.
pub const GpuError = error{
    OutOfHostMemory,
    OutOfDeviceMemory,
    DeviceLost,
    ValidationFailed,
    Unknown,
    InvalidVideoStdParametersKHR,
};

/// Initialization additionally creates staging resources and starts a worker.
pub const InitError = error{
    OutOfHostMemory,
    OutOfDeviceMemory,
    DeviceLost,
    ValidationFailed,
    Unknown,
    OutOfMemory,
    VmaBufferCreateFailed,
    StagingBufferNotMapped,
    BackendMismatch,
    ThreadQuotaExceeded,
    SystemResources,
    LockedMemoryLimitExceeded,
    Unexpected,
};

pub const ReadinessError = error{
    OutOfHostMemory,
    OutOfDeviceMemory,
    DeviceLost,
    ValidationFailed,
    Unknown,
};
pub const DrainError = GpuError || error{ OutOfMemory, VmaBufferCreateFailed, StagingBufferNotMapped };
pub const TimelineError = GpuError;

pub const Ticket = packed struct(u64) {
    gen: u32 = std.math.maxInt(u32),
    id: u32 = std.math.maxInt(u32),

    pub const none: Ticket = .{};

    pub fn isValid(self: Ticket) bool {
        return !std.meta.eql(self, none);
    }
};

pub const GroupTicket = struct {
    value: u64,

    pub const none: GroupTicket = .{ .value = std.math.maxInt(u64) };
};

/// A group builder bound to the backend that created it. A final upload closes
/// the builder and returns the shared group ticket.
pub const GroupUpload = struct {
    transport: *Interface,
    group_id: u64,
    /// Reserved internally while the group is being built. The public group
    /// completion ticket is returned only by the final upload call.
    ticket: Ticket,
    ended: bool = false,

    /// Discard queued tasks for this group. `free_src` controls whether
    /// task-owned source buffers are released. For finalized groups this
    /// removes the group by its ticket; work already processing or submitted
    /// cannot be cancelled.
    pub fn cancel(self: *GroupUpload, free_src: bool) void {
        var span = zprobe.span("Transport/groupCancel", .{ .group_id = self.group_id, .finalized = self.ended });
        span.enter();
        defer span.exit();

        if (self.ended) {
            self.transport.vtable.unqueue_group(self.transport, .{ .value = @bitCast(self.ticket) }, free_src);
        } else {
            self.transport.vtable.cancel_group(self.transport, self.group_id, self.ticket, free_src);
        }
        self.ended = true;
    }

    pub fn uploadBufferStaged(
        self: *GroupUpload,
        priority: bool,
        src: []const u8,
        dst: vk.Buffer,
        dst_offset: u32,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupUploadError!void {
        var span = zprobe.span("Transport/groupUploadBufferStaged", .{ .group_id = self.group_id, .size = src.len, .priority = priority });
        span.enter();
        defer span.exit();

        try self.uploadBufferStagedImpl(false, priority, src, dst, dst_offset, dst_stage, dst_access, first_use);
    }

    pub fn uploadBufferStagedFinal(
        self: *GroupUpload,
        priority: bool,
        src: []const u8,
        dst: vk.Buffer,
        dst_offset: u32,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupUploadError!GroupTicket {
        var span = zprobe.span("Transport/groupUploadBufferStagedFinal", .{ .group_id = self.group_id, .size = src.len, .priority = priority });
        span.enter();
        defer span.exit();

        return self.uploadBufferStagedImpl(true, priority, src, dst, dst_offset, dst_stage, dst_access, first_use);
    }

    pub fn uploadBuffer(
        self: *GroupUpload,
        priority: bool,
        src: []const u8,
        owning: ?FreeFn,
        owning_ctx: ?*anyopaque,
        dst: vk.Buffer,
        dst_offset: u32,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupUploadError!void {
        var span = zprobe.span("Transport/groupUploadBuffer", .{ .group_id = self.group_id, .size = src.len, .priority = priority });
        span.enter();
        defer span.exit();

        try self.uploadBufferImpl(false, priority, src, owning, owning_ctx, dst, dst_offset, dst_stage, dst_access, first_use);
    }

    pub fn uploadBufferFinal(
        self: *GroupUpload,
        priority: bool,
        src: []const u8,
        owning: ?FreeFn,
        owning_ctx: ?*anyopaque,
        dst: vk.Buffer,
        dst_offset: u32,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupUploadError!GroupTicket {
        var span = zprobe.span("Transport/groupUploadBufferFinal", .{ .group_id = self.group_id, .size = src.len, .priority = priority });
        span.enter();
        defer span.exit();

        return self.uploadBufferImpl(true, priority, src, owning, owning_ctx, dst, dst_offset, dst_stage, dst_access, first_use);
    }

    pub fn uploadImageStaged(
        self: *GroupUpload,
        priority: bool,
        format: vk.Format,
        dimension: vk.Extent3D,
        src: []const u8,
        dst: vk.Image,
        dst_layers: vk.ImageSubresourceLayers,
        dst_offset: vk.Offset3D,
        current_layout: vk.ImageLayout,
        new_layout: vk.ImageLayout,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupImageUploadError!void {
        var span = zprobe.span("Transport/groupUploadImageStaged", .{ .group_id = self.group_id, .size = src.len, .priority = priority, .format = @intFromEnum(format) });
        span.enter();
        defer span.exit();

        try self.uploadImageStagedImpl(false, priority, format, dimension, src, dst, dst_layers, dst_offset, current_layout, new_layout, dst_stage, dst_access, first_use);
    }

    pub fn uploadImageStagedFinal(
        self: *GroupUpload,
        priority: bool,
        format: vk.Format,
        dimension: vk.Extent3D,
        src: []const u8,
        dst: vk.Image,
        dst_layers: vk.ImageSubresourceLayers,
        dst_offset: vk.Offset3D,
        current_layout: vk.ImageLayout,
        new_layout: vk.ImageLayout,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupImageUploadError!GroupTicket {
        var span = zprobe.span("Transport/groupUploadImageStagedFinal", .{ .group_id = self.group_id, .size = src.len, .priority = priority, .format = @intFromEnum(format) });
        span.enter();
        defer span.exit();

        return self.uploadImageStagedImpl(true, priority, format, dimension, src, dst, dst_layers, dst_offset, current_layout, new_layout, dst_stage, dst_access, first_use);
    }

    pub fn uploadImage(
        self: *GroupUpload,
        priority: bool,
        format: vk.Format,
        dimension: vk.Extent3D,
        src: []const u8,
        owning: ?FreeFn,
        owning_ctx: ?*anyopaque,
        dst: vk.Image,
        dst_layers: vk.ImageSubresourceLayers,
        dst_offset: vk.Offset3D,
        current_layout: vk.ImageLayout,
        new_layout: vk.ImageLayout,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupImageUploadError!void {
        var span = zprobe.span("Transport/groupUploadImage", .{ .group_id = self.group_id, .size = src.len, .priority = priority, .format = @intFromEnum(format) });
        span.enter();
        defer span.exit();

        try self.uploadImageImpl(false, priority, format, dimension, src, owning, owning_ctx, dst, dst_layers, dst_offset, current_layout, new_layout, dst_stage, dst_access, first_use);
    }

    pub fn uploadImageFinal(
        self: *GroupUpload,
        priority: bool,
        format: vk.Format,
        dimension: vk.Extent3D,
        src: []const u8,
        owning: ?FreeFn,
        owning_ctx: ?*anyopaque,
        dst: vk.Image,
        dst_layers: vk.ImageSubresourceLayers,
        dst_offset: vk.Offset3D,
        current_layout: vk.ImageLayout,
        new_layout: vk.ImageLayout,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupImageUploadError!GroupTicket {
        var span = zprobe.span("Transport/groupUploadImageFinal", .{ .group_id = self.group_id, .size = src.len, .priority = priority, .format = @intFromEnum(format) });
        span.enter();
        defer span.exit();

        return self.uploadImageImpl(true, priority, format, dimension, src, owning, owning_ctx, dst, dst_layers, dst_offset, current_layout, new_layout, dst_stage, dst_access, first_use);
    }

    fn uploadBufferStagedImpl(
        self: *GroupUpload,
        comptime end: bool,
        priority: bool,
        src: []const u8,
        dst: vk.Buffer,
        dst_offset: u32,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupUploadError!if (end) GroupTicket else void {
        std.debug.assert(!self.ended);

        const res = try self.transport.staging.stage(src);
        errdefer res.free_fn(res.ctx, res.staged.ptr);

        const result = try self.transport.vtable.group_upload_buffer(
            self.transport,
            self,
            end,
            priority,
            res.staged,
            res.free_fn,
            res.ctx,
            dst,
            dst_offset,
            dst_stage,
            dst_access,
            first_use,
        );

        self.ended = result != null;
        if (end) return result.?;
    }

    fn uploadBufferImpl(
        self: *GroupUpload,
        comptime end: bool,
        priority: bool,
        src: []const u8,
        owning: ?FreeFn,
        owning_ctx: ?*anyopaque,
        dst: vk.Buffer,
        dst_offset: u32,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupUploadError!if (end) GroupTicket else void {
        std.debug.assert(!self.ended);

        const result = try self.transport.vtable.group_upload_buffer(
            self.transport,
            self,
            end,
            priority,
            src,
            owning,
            owning_ctx,
            dst,
            dst_offset,
            dst_stage,
            dst_access,
            first_use,
        );

        self.ended = result != null;
        if (end) return result.?;
    }

    fn uploadImageStagedImpl(
        self: *GroupUpload,
        comptime end: bool,
        priority: bool,
        format: vk.Format,
        dimension: vk.Extent3D,
        src: []const u8,
        dst: vk.Image,
        dst_layers: vk.ImageSubresourceLayers,
        dst_offset: vk.Offset3D,
        current_layout: vk.ImageLayout,
        new_layout: vk.ImageLayout,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupImageUploadError!if (end) GroupTicket else void {
        std.debug.assert(!self.ended);

        const res = try self.transport.staging.stage(src);
        errdefer res.free_fn(res.ctx, res.staged.ptr);

        const result = try self.transport.vtable.group_upload_image(
            self.transport,
            self,
            end,
            priority,
            format,
            dimension,
            res.staged,
            res.free_fn,
            res.ctx,
            dst,
            dst_layers,
            dst_offset,
            current_layout,
            new_layout,
            dst_stage,
            dst_access,
            first_use,
        );

        self.ended = result != null;
        if (end) return result.?;
    }

    fn uploadImageImpl(
        self: *GroupUpload,
        comptime end: bool,
        priority: bool,
        format: vk.Format,
        dimension: vk.Extent3D,
        src: []const u8,
        owning: ?FreeFn,
        owning_ctx: ?*anyopaque,
        dst: vk.Image,
        dst_layers: vk.ImageSubresourceLayers,
        dst_offset: vk.Offset3D,
        current_layout: vk.ImageLayout,
        new_layout: vk.ImageLayout,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
        first_use: bool,
    ) GroupImageUploadError!if (end) GroupTicket else void {
        std.debug.assert(!self.ended);

        const result = try self.transport.vtable.group_upload_image(
            self.transport,
            self,
            end,
            priority,
            format,
            dimension,
            src,
            owning,
            owning_ctx,
            dst,
            dst_layers,
            dst_offset,
            current_layout,
            new_layout,
            dst_stage,
            dst_access,
            first_use,
        );

        self.ended = result != null;
        if (end) return result.?;
    }
};

pub const VTable = struct {
    init: *const fn (*Interface, *root.Ctx, std.mem.Allocator) InitError!void,
    deinit: *const fn (*Interface) void,
    upload_buffer: *const fn (*Interface, bool, []const u8, ?FreeFn, ?*anyopaque, vk.Buffer, u32, vk.PipelineStageFlags2, vk.AccessFlags2, bool) UploadError!Ticket,
    upload_image: *const fn (*Interface, bool, vk.Format, vk.Extent3D, []const u8, ?FreeFn, ?*anyopaque, vk.Image, vk.ImageSubresourceLayers, vk.Offset3D, vk.ImageLayout, vk.ImageLayout, vk.PipelineStageFlags2, vk.AccessFlags2, bool) ImageUploadError!Ticket,
    begin_group: *const fn (*Interface) GroupCreateError!GroupUpload,
    group_upload_buffer: *const fn (*Interface, *GroupUpload, bool, bool, []const u8, ?FreeFn, ?*anyopaque, vk.Buffer, u32, vk.PipelineStageFlags2, vk.AccessFlags2, bool) GroupUploadError!?GroupTicket,
    group_upload_image: *const fn (*Interface, *GroupUpload, bool, bool, vk.Format, vk.Extent3D, []const u8, ?FreeFn, ?*anyopaque, vk.Image, vk.ImageSubresourceLayers, vk.Offset3D, vk.ImageLayout, vk.ImageLayout, vk.PipelineStageFlags2, vk.AccessFlags2, bool) GroupImageUploadError!?GroupTicket,
    is_ready: *const fn (*Interface, Ticket) ReadinessError!bool,
    is_group_ready: *const fn (*Interface, GroupTicket) ReadinessError!bool,
    unqueue_group: *const fn (*Interface, GroupTicket, bool) void,
    cancel_group: *const fn (*Interface, u64, Ticket, bool) void,
    wait_on: *const fn (*Interface, []const Ticket) vk.SemaphoreSubmitInfo,
    unqueue: *const fn (*Interface, Ticket, bool) void,
    drain: *const fn (*Interface) DrainError!void,
    get_timeline: *const fn (*const Interface) TimelineError!u64,
};

const Interface = @This();

vtable: *const VTable,
staging: Staging,

pub fn init(self: *Interface, ctx: *root.Ctx, alloc: std.mem.Allocator, staging_alloc: std.mem.Allocator) InitError!void {
    var span = zprobe.span("Transport/init", .{});
    span.enter();
    defer span.exit();

    try self.vtable.init(self, ctx, alloc);
    self.staging = .init(staging_alloc, ctx.io);
}

pub fn deinit(self: *Interface) void {
    var span = zprobe.span("Transport/deinit", .{});
    span.enter();
    defer span.exit();

    self.vtable.deinit(self);
    self.staging.deinit();
}

pub fn uploadBufferStaged(self: *Interface, priority: bool, src: []const u8, dst: vk.Buffer, dst_offset: u32, dst_stage: vk.PipelineStageFlags2, dst_access: vk.AccessFlags2, first_use: bool) UploadError!Ticket {
    var span = zprobe.span("Transport/uploadBufferStaged", .{});
    span.enter();
    defer span.exit();

    const res = try self.staging.stage(src);
    errdefer res.free_fn(res.ctx, res.staged.ptr);
    const ticket = try self.vtable.upload_buffer(self, priority, res.staged, res.free_fn, res.ctx, dst, dst_offset, dst_stage, dst_access, first_use);

    zprobe.event(.debug, "buffer upload queued", .{ .ticket_id = ticket.id, .ticket_generation = ticket.gen, .size = src.len });
    return ticket;
}

pub fn uploadBuffer(self: *Interface, priority: bool, src: []const u8, owning: ?FreeFn, owning_ctx: ?*anyopaque, dst: vk.Buffer, dst_offset: u32, dst_stage: vk.PipelineStageFlags2, dst_access: vk.AccessFlags2, first_use: bool) UploadError!Ticket {
    var span = zprobe.span("Transport/uploadBuffer", .{ .size = src.len, .priority = priority, .destination_offset = dst_offset, .first_use = first_use });
    span.enter();
    defer span.exit();

    const ticket = try self.vtable.upload_buffer(self, priority, src, owning, owning_ctx, dst, dst_offset, dst_stage, dst_access, first_use);

    zprobe.event(.debug, "buffer upload queued", .{ .ticket_id = ticket.id, .ticket_generation = ticket.gen, .size = src.len });
    return ticket;
}

pub fn uploadImageStaged(self: *Interface, priority: bool, format: vk.Format, dimension: vk.Extent3D, src: []const u8, dst: vk.Image, dst_layers: vk.ImageSubresourceLayers, dst_offset: vk.Offset3D, current_layout: vk.ImageLayout, new_layout: vk.ImageLayout, dst_stage: vk.PipelineStageFlags2, dst_access: vk.AccessFlags2, first_use: bool) ImageUploadError!Ticket {
    var span = zprobe.span("Transport/uploadImageStaged", .{});
    span.enter();
    defer span.exit();

    const res = try self.staging.stage(src);
    errdefer res.free_fn(res.ctx, res.staged.ptr);

    const ticket = try self.vtable.upload_image(self, priority, format, dimension, res.staged, res.free_fn, res.ctx, dst, dst_layers, dst_offset, current_layout, new_layout, dst_stage, dst_access, first_use);

    zprobe.event(.debug, "image upload queued", .{ .ticket_id = ticket.id, .ticket_generation = ticket.gen, .size = src.len });
    return ticket;
}

pub fn uploadImage(self: *Interface, priority: bool, format: vk.Format, dimension: vk.Extent3D, src: []const u8, owning: ?FreeFn, owning_ctx: ?*anyopaque, dst: vk.Image, dst_layers: vk.ImageSubresourceLayers, dst_offset: vk.Offset3D, current_layout: vk.ImageLayout, new_layout: vk.ImageLayout, dst_stage: vk.PipelineStageFlags2, dst_access: vk.AccessFlags2, first_use: bool) ImageUploadError!Ticket {
    var span = zprobe.span("Transport/uploadImage", .{ .size = src.len, .priority = priority, .format = @intFromEnum(format), .width = dimension.width, .height = dimension.height, .depth = dimension.depth, .first_use = first_use });
    span.enter();
    defer span.exit();

    const ticket = try self.vtable.upload_image(self, priority, format, dimension, src, owning, owning_ctx, dst, dst_layers, dst_offset, current_layout, new_layout, dst_stage, dst_access, first_use);

    zprobe.event(.debug, "image upload queued", .{ .ticket_id = ticket.id, .ticket_generation = ticket.gen, .size = src.len });
    return ticket;
}

pub fn beginGroup(self: *Interface) GroupCreateError!GroupUpload {
    var span = zprobe.span("Transport/beginGroup", .{});
    span.enter();
    defer span.exit();

    return self.vtable.begin_group(self);
}

pub fn isReady(self: *Interface, ticket: Ticket) ReadinessError!bool {
    var span = zprobe.span("Transport/isReady", .{ .ticket_id = ticket.id, .ticket_generation = ticket.gen });
    span.enter();
    defer span.exit();

    return self.vtable.is_ready(self, ticket);
}

pub fn isGroupReady(self: *Interface, ticket: GroupTicket) ReadinessError!bool {
    var span = zprobe.span("Transport/isGroupReady", .{ .group_ticket = ticket.value });
    span.enter();
    defer span.exit();

    return self.vtable.is_group_ready(self, ticket);
}

pub fn unqueueGroup(self: *Interface, ticket: GroupTicket, free_src: bool) void {
    var span = zprobe.span("Transport/unqueueGroup", .{ .group_ticket = ticket.value, .free_src = free_src });
    span.enter();
    defer span.exit();

    self.vtable.unqueue_group(self, ticket, free_src);
}

pub fn waitOn(self: *Interface, tickets: []const Ticket) vk.SemaphoreSubmitInfo {
    var span = zprobe.span("Transport/waitOn", .{ .ticket_count = tickets.len });
    span.enter();
    defer span.exit();

    return self.vtable.wait_on(self, tickets);
}

pub fn unqueue(self: *Interface, ticket: Ticket, free_src: bool) void {
    var span = zprobe.span("Transport/unqueue", .{ .ticket_id = ticket.id, .ticket_generation = ticket.gen, .free_src = free_src });
    span.enter();
    defer span.exit();

    self.vtable.unqueue(self, ticket, free_src);
}

pub fn drain(self: *Interface) DrainError!void {
    var span = zprobe.span("Transport/drain", .{});
    span.enter();
    defer span.exit();

    return self.vtable.drain(self);
}

pub fn getTimeline(self: *const Interface) TimelineError!u64 {
    var span = zprobe.span("Transport/getTimeline", .{});
    span.enter();
    defer span.exit();

    return self.vtable.get_timeline(self);
}

test {
    std.testing.refAllDecls(@This());
}
