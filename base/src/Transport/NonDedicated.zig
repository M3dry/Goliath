//! Single-queue upload backend. The worker only moves finalized CPU tasks into
//! a ring buffer; `drain` stages, records, and submits them on the graphics queue.
const std = @import("std");
const vk = @import("vulkan");
const zprobe = @import("zprobe");
const root = @import("../root.zig");

const Buffer = @import("../Buffer.zig");
const Interface = @import("Interface.zig");
const RingBuffer = @import("../util.zig").RingBuffer;

const Self = @This();
const Allocator = std.mem.Allocator;
const BackendCtx = root.Ctx.Query(&.{ .device, .vma_allocator, .graphics_family, .transport_family, .graphics_queue, .graphics_submit_lock, .io, .dedicated_transport });

const initial_staging_size: u64 = 8 * 1024 * 1024;
const slot_count = 2;
const ReadyQueue = RingBuffer(Task);

const Owned = struct {
    free_fn: Interface.FreeFn,
    ctx: ?*anyopaque,
};

const ImageInfo = struct {
    image: vk.Image,
    layers: vk.ImageSubresourceLayers,
    offset: vk.Offset3D,
    extent: vk.Extent3D,
    current_layout: vk.ImageLayout,
    new_layout: vk.ImageLayout,
};

const Destination = union(enum) {
    buffer: struct { handle: vk.Buffer, offset: u64, size: u64 },
    image: ImageInfo,
};

const Task = struct {
    dst: Destination,
    src: [*]const u8,
    size: u64,
    owning: ?Owned,
    dst_stage: vk.PipelineStageFlags2,
    dst_access: vk.AccessFlags2,
    first_use: bool,
    ticket_id: u32,
    group_id: ?u64 = null,
    staging_offset: u64 = 0,
};

const TicketEntry = struct {
    generation: u32 = 0,
    timeline: u64 = 0,
    processing: bool = false,
    submitted: bool = false,
};

const Group = struct {
    id: u64,
    ticket_id: u32,
    finalized: bool = false,
};

const Slot = struct {
    staging: Buffer,
    cmd_buf: vk.CommandBuffer,
    fence: vk.Fence,
    submitted: bool = false,
};

const vtable: Interface.VTable = .{
    .init = init,
    .deinit = deinit,
    .upload_buffer = uploadBuffer,
    .upload_image = uploadImage,
    .begin_group = beginGroup,
    .group_upload_buffer = groupUploadBuffer,
    .group_upload_image = groupUploadImage,
    .is_ready = isReady,
    .is_group_ready = isGroupReady,
    .unqueue_group = unqueueGroup,
    .cancel_group = cancelGroup,
    .wait_on = waitOn,
    .unqueue = unqueue,
    .drain = drain,
    .get_timeline = getTimeline,
};

interface: Interface = .{ .vtable = &vtable, .staging = undefined },
alloc: Allocator = undefined,
ctx: BackendCtx = undefined,
timeline_semaphore: vk.Semaphore = .null_handle,
timeline_value: u64 = 0,
queue_mutex: std.Io.Mutex = .init,
queue_condition: std.Io.Condition = .init,
stop_worker: bool = false,
worker: ?std.Thread = null,
tasks: std.ArrayListUnmanaged(Task) = .empty,
ready_tasks: ReadyQueue = .empty,
draining: std.ArrayListUnmanaged(Task) = .empty,
tickets: std.ArrayListUnmanaged(TicketEntry) = .empty,
groups: std.ArrayListUnmanaged(Group) = .empty,
next_group_id: u64 = 0,
slots: [slot_count]Slot = undefined,
slot_index: u32 = 0,
pre_buffer_barriers: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty,
post_buffer_barriers: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty,
pre_image_barriers: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty,
post_image_barriers: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty,
command_pool: vk.CommandPool = .null_handle,

fn fromInterface(interface: *Interface) *Self {
    return @fieldParentPtr("interface", interface);
}

fn fromConstInterface(interface: *const Interface) *const Self {
    return @fieldParentPtr("interface", interface);
}

fn formatBytes(format: vk.Format) error{UnsupportedFormat}!u32 {
    return switch (format) {
        .r8_unorm => 1,
        .r8g8_unorm, .r16_unorm, .r16_sfloat => 2,
        .r8g8b8_unorm => 3,
        .r16g16_unorm, .r32_sfloat, .r32_uint, .r32_sint, .a2b10g10r10_unorm_pack32, .r8g8b8a8_unorm, .r8g8b8a8_srgb, .b8g8r8a8_unorm, .b8g8r8a8_srgb => 4,
        .r16g16b16_unorm => 6,
        .r16g16b16a16_unorm, .r16g16b16a16_sfloat => 8,
        .r32g32b32_sfloat, .r32g32b32_uint => 12,
        .r32g32b32a32_sfloat, .r32g32b32a32_uint => 16,
        .bc1_rgba_unorm_block => 8,
        else => error.UnsupportedFormat,
    };
}

fn imageDataSize(format: vk.Format, dimension: vk.Extent3D, layers: u32) (error{ UnsupportedFormat, OutOfMemory })!u64 {
    if (format == .bc1_rgba_unorm_block) {
        const blocks_w = (@as(u64, dimension.width) + 3) / 4;
        const blocks_h = (@as(u64, dimension.height) + 3) / 4;
        const blocks = std.math.mul(u64, blocks_w, blocks_h) catch return error.OutOfMemory;
        const block_layers = std.math.mul(u64, blocks, dimension.depth) catch return error.OutOfMemory;
        const all_layers = std.math.mul(u64, block_layers, layers) catch return error.OutOfMemory;

        return std.math.mul(u64, all_layers, 8) catch return error.OutOfMemory;
    }

    const bpp = try formatBytes(format);
    const pixels = std.math.mul(u64, dimension.width, dimension.height) catch return error.OutOfMemory;
    const volume = std.math.mul(u64, pixels, dimension.depth) catch return error.OutOfMemory;
    const all_layers = std.math.mul(u64, volume, layers) catch return error.OutOfMemory;

    return std.math.mul(u64, all_layers, bpp) catch return error.OutOfMemory;
}

fn init(interface: *Interface, ctx: *root.Ctx, alloc: Allocator) Interface.InitError!void {
    const self = fromInterface(interface);
    self.ctx = .from(ctx);
    self.alloc = alloc;
    self.stop_worker = false;
    self.worker = null;
    self.ready_tasks = try ReadyQueue.initCapacity(alloc, 64);
    errdefer self.ready_tasks.deinit(alloc);

    self.timeline_semaphore = try self.ctx.view.device.createSemaphore(&.{
        .p_next = &vk.SemaphoreTypeCreateInfo{ .initial_value = 0, .semaphore_type = .timeline },
    }, null);
    errdefer self.ctx.view.device.destroySemaphore(self.timeline_semaphore, null);

    self.slots = undefined;
    var staging_count: usize = 0;
    errdefer for (self.slots[0..staging_count]) |*slot| slot.staging.deinitNow(.from(ctx));
    for (&self.slots) |*slot| {
        slot.staging = try Buffer.init(.from(self.ctx), .graphics, "Transport staging", initial_staging_size, .{ .transfer_src_bit = true }, .cpu_to_gpu_staging);
        staging_count += 1;
        if (slot.staging.mapped == null) return error.StagingBufferNotMapped;
    }

    const pool = try self.ctx.view.device.createCommandPool(&.{ .flags = .{ .reset_command_buffer_bit = true }, .queue_family_index = self.ctx.view.graphics_family }, null);
    errdefer self.ctx.view.device.destroyCommandPool(pool, null);
    var command_buffers: [slot_count]vk.CommandBuffer = undefined;
    try self.ctx.view.device.allocateCommandBuffers(&.{ .command_pool = pool, .level = .primary, .command_buffer_count = slot_count }, &command_buffers);

    var fences_created: usize = 0;
    errdefer for (self.slots[0..fences_created]) |slot| self.ctx.view.device.destroyFence(slot.fence, null);
    for (&self.slots, 0..) |*slot, i| {
        slot.cmd_buf = command_buffers[i];
        slot.fence = try self.ctx.view.device.createFence(&.{ .flags = .{ .signaled_bit = true } }, null);
        slot.submitted = false;
        fences_created += 1;
    }

    self.command_pool = pool;
    self.worker = try std.Thread.spawn(.{}, workerThread, .{self});
    zprobe.event(.info, "non dedicated transport initialized", .{ .slot_count = slot_count, .staging_size = initial_staging_size });
}

fn deinit(interface: *Interface) void {
    const self = fromInterface(interface);
    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    self.stop_worker = true;
    self.queue_condition.broadcast(self.ctx.view.io);
    self.queue_mutex.unlock(self.ctx.view.io);

    if (self.worker) |worker| worker.join();
    for (&self.slots) |*slot| {
        if (slot.submitted) _ = self.ctx.view.device.waitForFences(&.{slot.fence}, .true, std.math.maxInt(u64)) catch {};
    }

    for (&self.slots) |*slot| {
        slot.staging.deinitNow(.from(self.ctx));
        self.ctx.view.device.destroyFence(slot.fence, null);
    }

    if (self.command_pool != .null_handle) self.ctx.view.device.destroyCommandPool(self.command_pool, null);
    if (self.timeline_semaphore != .null_handle) self.ctx.view.device.destroySemaphore(self.timeline_semaphore, null);

    for (self.tasks.items) |task| freeTask(task);

    var ready_iter = self.ready_tasks.iterate();
    while (ready_iter.next()) |task| freeTask(task.*);

    for (self.draining.items) |task| freeTask(task);

    self.tasks.deinit(self.alloc);
    self.ready_tasks.deinit(self.alloc);
    self.draining.deinit(self.alloc);
    self.tickets.deinit(self.alloc);
    self.groups.deinit(self.alloc);
    self.pre_buffer_barriers.deinit(self.alloc);

    self.post_buffer_barriers.deinit(self.alloc);
    self.pre_image_barriers.deinit(self.alloc);
    self.post_image_barriers.deinit(self.alloc);

    zprobe.event(.info, "non dedicated transport deinitialized", .{});
}

fn freeTask(task: Task) void {
    if (task.owning) |owned| owned.free_fn(owned.ctx, @ptrCast(@constCast(task.src)));
}

fn createTicketLocked(self: *Self) Interface.QueueError!Interface.Ticket {
    const id: u32 = @intCast(self.tickets.items.len);

    try self.tickets.append(self.alloc, .{});
    return .{ .gen = 0, .id = id };
}

fn validEntry(self: *Self, ticket: Interface.Ticket) ?*TicketEntry {
    if (!ticket.isValid() or ticket.id >= self.tickets.items.len) return null;

    const entry = &self.tickets.items[ticket.id];
    if (entry.generation != ticket.gen) return null;

    return entry;
}

fn uploadBuffer(
    interface: *Interface,
    priority: bool,
    src: []const u8,
    owning: ?Interface.FreeFn,
    owning_ctx: ?*anyopaque,
    dst: vk.Buffer,
    dst_offset: u32,
    dst_stage: vk.PipelineStageFlags2,
    dst_access: vk.AccessFlags2,
    first_use: bool,
) Interface.UploadError!Interface.Ticket {
    const self = fromInterface(interface);

    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    try self.tasks.ensureUnusedCapacity(self.alloc, 1);

    const ticket = try self.createTicketLocked();
    const task: Task = .{
        .dst = .{ .buffer = .{ .handle = dst, .offset = dst_offset, .size = src.len } },
        .src = src.ptr,
        .size = src.len,
        .owning = if (owning) |free_fn| .{ .free_fn = free_fn, .ctx = owning_ctx } else null,
        .dst_stage = dst_stage,
        .dst_access = dst_access,
        .first_use = first_use,
        .ticket_id = ticket.id,
    };

    if (priority) self.tasks.insertAssumeCapacity(0, task) else self.tasks.appendAssumeCapacity(task);
    self.queue_condition.broadcast(self.ctx.view.io);

    return ticket;
}

fn uploadImage(
    interface: *Interface,
    priority: bool,
    format: vk.Format,
    dimension: vk.Extent3D,
    src: []const u8,
    owning: ?Interface.FreeFn,
    owning_ctx: ?*anyopaque,
    dst: vk.Image,
    dst_layers: vk.ImageSubresourceLayers,
    dst_offset: vk.Offset3D,
    current_layout: vk.ImageLayout,
    new_layout: vk.ImageLayout,
    dst_stage: vk.PipelineStageFlags2,
    dst_access: vk.AccessFlags2,
    first_use: bool,
) Interface.ImageUploadError!Interface.Ticket {
    const self = fromInterface(interface);

    const size = try imageDataSize(format, dimension, dst_layers.layer_count);
    std.debug.assert(src.len >= size);

    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    try self.tasks.ensureUnusedCapacity(self.alloc, 1);

    const ticket = try self.createTicketLocked();
    const task: Task = .{
        .dst = .{ .image = .{
            .image = dst,
            .layers = dst_layers,
            .offset = dst_offset,
            .extent = dimension,
            .current_layout = current_layout,
            .new_layout = new_layout,
        } },
        .src = src.ptr,
        .size = size,
        .owning = if (owning) |free_fn| .{ .free_fn = free_fn, .ctx = owning_ctx } else null,
        .dst_stage = dst_stage,
        .dst_access = dst_access,
        .first_use = first_use,
        .ticket_id = ticket.id,
    };

    if (priority) self.tasks.insertAssumeCapacity(0, task) else self.tasks.appendAssumeCapacity(task);
    self.queue_condition.broadcast(self.ctx.view.io);

    return ticket;
}

fn beginGroup(interface: *Interface) Interface.GroupCreateError!Interface.GroupUpload {
    const self = fromInterface(interface);

    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    try self.groups.ensureUnusedCapacity(self.alloc, 1);

    const ticket = try self.createTicketLocked();
    const id = self.next_group_id;

    self.next_group_id +%= 1;
    self.groups.appendAssumeCapacity(.{ .id = id, .ticket_id = ticket.id });
    self.queue_condition.broadcast(self.ctx.view.io);

    return .{ .transport = interface, .group_id = id, .ticket = ticket };
}

fn findGroup(self: *Self, id: u64) ?*Group {
    for (self.groups.items) |*group| if (group.id == id) return group;

    return null;
}

fn groupUploadBuffer(interface: *Interface, group_upload: *Interface.GroupUpload, end: bool, priority: bool, src: []const u8, owning: ?Interface.FreeFn, owning_ctx: ?*anyopaque, dst: vk.Buffer, dst_offset: u32, dst_stage: vk.PipelineStageFlags2, dst_access: vk.AccessFlags2, first_use: bool) Interface.GroupUploadError!?Interface.GroupTicket {
    const self = fromInterface(interface);

    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    const group = self.findGroup(group_upload.group_id) orelse return error.GroupAlreadyEnded;
    if (group.finalized) return error.GroupAlreadyEnded;

    try self.tasks.ensureUnusedCapacity(self.alloc, 1);
    const task: Task = .{
        .dst = .{ .buffer = .{ .handle = dst, .offset = dst_offset, .size = src.len } },
        .src = src.ptr,
        .size = src.len,
        .owning = if (owning) |free_fn| .{ .free_fn = free_fn, .ctx = owning_ctx } else null,
        .dst_stage = dst_stage,
        .dst_access = dst_access,
        .first_use = first_use,
        .ticket_id = group.ticket_id,
        .group_id = group.id,
    };

    if (priority) self.tasks.insertAssumeCapacity(0, task) else self.tasks.appendAssumeCapacity(task);
    if (end) self.queue_condition.broadcast(self.ctx.view.io);
    if (!end) return null;

    group.finalized = true;
    return .{ .value = @bitCast(group_upload.ticket) };
}

fn groupUploadImage(interface: *Interface, group_upload: *Interface.GroupUpload, end: bool, priority: bool, format: vk.Format, dimension: vk.Extent3D, src: []const u8, owning: ?Interface.FreeFn, owning_ctx: ?*anyopaque, dst: vk.Image, dst_layers: vk.ImageSubresourceLayers, dst_offset: vk.Offset3D, current_layout: vk.ImageLayout, new_layout: vk.ImageLayout, dst_stage: vk.PipelineStageFlags2, dst_access: vk.AccessFlags2, first_use: bool) Interface.GroupImageUploadError!?Interface.GroupTicket {
    const self = fromInterface(interface);

    const size = try imageDataSize(format, dimension, dst_layers.layer_count);
    std.debug.assert(src.len >= size);

    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    const group = self.findGroup(group_upload.group_id) orelse return error.GroupAlreadyEnded;
    if (group.finalized) return error.GroupAlreadyEnded;

    try self.tasks.ensureUnusedCapacity(self.alloc, 1);

    const task: Task = .{
        .dst = .{ .image = .{ .image = dst, .layers = dst_layers, .offset = dst_offset, .extent = dimension, .current_layout = current_layout, .new_layout = new_layout } },
        .src = src.ptr,
        .size = size,
        .owning = if (owning) |free_fn| .{ .free_fn = free_fn, .ctx = owning_ctx } else null,
        .dst_stage = dst_stage,
        .dst_access = dst_access,
        .first_use = first_use,
        .ticket_id = group.ticket_id,
        .group_id = group.id,
    };

    if (priority) self.tasks.insertAssumeCapacity(0, task) else self.tasks.appendAssumeCapacity(task);
    if (end) self.queue_condition.broadcast(self.ctx.view.io);
    if (!end) return null;

    group.finalized = true;
    return .{ .value = @bitCast(group_upload.ticket) };
}

fn isReady(interface: *Interface, ticket: Interface.Ticket) Interface.ReadinessError!bool {
    const self = fromInterface(interface);
    self.queue_mutex.lockUncancelable(self.ctx.view.io);

    const entry = self.validEntry(ticket);
    if (entry == null) {
        const stale = ticket.isValid() and ticket.id < self.tickets.items.len and self.tickets.items[ticket.id].generation > ticket.gen;
        self.queue_mutex.unlock(self.ctx.view.io);
        return stale;
    }

    const timeline = entry.?.timeline;
    self.queue_mutex.unlock(self.ctx.view.io);

    if (timeline == 0) return false;
    const current = self.ctx.view.device.getSemaphoreCounterValue(self.timeline_semaphore) catch |err| return mapReadinessError(err);

    return current >= timeline;
}

fn mapReadinessError(err: Interface.GpuError) Interface.ReadinessError {
    return switch (err) {
        error.OutOfHostMemory => error.OutOfHostMemory,
        error.OutOfDeviceMemory => error.OutOfDeviceMemory,
        error.DeviceLost => error.DeviceLost,
        error.ValidationFailed => error.ValidationFailed,
        else => error.Unknown,
    };
}

fn isGroupReady(interface: *Interface, ticket: Interface.GroupTicket) Interface.ReadinessError!bool {
    return isReady(interface, @bitCast(ticket.value));
}

fn invalidateTicketLocked(self: *Self, ticket_id: u32) void {
    if (ticket_id >= self.tickets.items.len) return;

    const entry = &self.tickets.items[ticket_id];
    entry.generation +%= 1;
    entry.timeline = 0;
    entry.processing = false;
    entry.submitted = false;

    self.queue_condition.broadcast(self.ctx.view.io);
}

fn unqueueGroup(interface: *Interface, ticket: Interface.GroupTicket, free_src: bool) void {
    const self = fromInterface(interface);
    self.removeTasks(@bitCast(ticket.value), free_src);
}

fn cancelGroup(interface: *Interface, group_id: u64, ticket: Interface.Ticket, free_src: bool) void {
    const self = fromInterface(interface);

    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    if (self.findGroup(group_id)) |group| {
        if (group.finalized) return;

        var i: usize = 0;
        while (i < self.tasks.items.len) {
            if (self.tasks.items[i].group_id == group_id) {
                const task = self.tasks.orderedRemove(i);
                if (free_src) freeTask(task);
            } else i += 1;
        }

        i = 0;
        while (i < self.ready_tasks.len) {
            const task = self.ready_tasks.get(i).?.*;
            if (task.group_id == group_id) {
                _ = self.ready_tasks.orderedRemove(i);
                if (free_src) freeTask(task);
            } else i += 1;
        }

        _ = self.groups.swapRemove(indexOfGroup(self.groups.items, group_id).?);
        self.invalidateTicketLocked(ticket.id);
    }
}

fn indexOfGroup(groups: []Group, id: u64) ?usize {
    for (groups, 0..) |group, i| if (group.id == id) return i;

    return null;
}

fn removeTasks(self: *Self, ticket: Interface.Ticket, free_src: bool) void {
    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    const entry = self.validEntry(ticket) orelse return;
    if (entry.processing or entry.submitted) return;

    var i: usize = 0;
    var removed = false;
    while (i < self.tasks.items.len) {
        if (self.tasks.items[i].ticket_id == ticket.id) {
            const task = self.tasks.orderedRemove(i);
            removed = true;
            if (free_src) freeTask(task);
        } else i += 1;
    }

    i = 0;
    while (i < self.ready_tasks.len) {
        const task = self.ready_tasks.get(i).?.*;
        if (task.ticket_id == ticket.id) {
            _ = self.ready_tasks.orderedRemove(i);
            removed = true;
            if (free_src) freeTask(task);
        } else i += 1;
    }

    if (removed) {
        self.invalidateTicketLocked(ticket.id);
        if (indexOfGroupTicket(self.groups.items, ticket.id)) |group_index| _ = self.groups.swapRemove(group_index);
    }
}

fn indexOfGroupTicket(groups: []Group, ticket_id: u32) ?usize {
    for (groups, 0..) |group, i| if (group.ticket_id == ticket_id) return i;
    return null;
}

fn unqueue(interface: *Interface, ticket: Interface.Ticket, free_src: bool) void {
    fromInterface(interface).removeTasks(ticket, free_src);
}

fn waitOn(interface: *Interface, tickets: []const Interface.Ticket) vk.SemaphoreSubmitInfo {
    const self = fromInterface(interface);

    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    var value: u64 = 0;
    for (tickets) |ticket| {
        while (self.validEntry(ticket)) |entry| {
            if (entry.timeline != 0) {
                value = @max(value, entry.timeline);
                break;
            }

            self.queue_condition.waitUncancelable(self.ctx.view.io, &self.queue_mutex);
        }
    }

    return .{ .semaphore = self.timeline_semaphore, .value = value, .stage_mask = .{ .all_commands_bit = true }, .device_index = 0 };
}

fn getTimeline(interface: *const Interface) Interface.TimelineError!u64 {
    const self = fromConstInterface(interface);
    return self.ctx.view.device.getSemaphoreCounterValue(self.timeline_semaphore);
}

fn ensureStaging(self: *Self, slot: *Slot, required: u64) Interface.DrainError!void {
    if (required <= slot.staging.size) return;

    var capacity = slot.staging.size;
    while (capacity < required) capacity = std.math.mul(u64, capacity, 2) catch return error.OutOfMemory;

    var replacement = try Buffer.init(.from(self.ctx), .graphics, "Transport staging", capacity, .{ .transfer_src_bit = true }, .cpu_to_gpu_staging);

    if (replacement.mapped == null) {
        replacement.deinitNow(.from(self.ctx));
        return error.StagingBufferNotMapped;
    }

    slot.staging.deinitNow(.from(self.ctx));
    slot.staging = replacement;
}

fn captureTasks(self: *Self) Interface.DrainError!u64 {
    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    defer self.queue_mutex.unlock(self.ctx.view.io);

    var batch_timeline: u64 = 0;
    if (self.draining.items.len == 0) {
        try self.draining.ensureUnusedCapacity(self.alloc, self.ready_tasks.len);

        while (self.ready_tasks.popFirst()) |task| {
            self.draining.appendAssumeCapacity(task);

            if (task.ticket_id < self.tickets.items.len) {
                const entry = &self.tickets.items[task.ticket_id];
                entry.processing = true;
                batch_timeline = @max(batch_timeline, entry.timeline);
            }
        }
    } else {
        for (self.draining.items) |task| if (task.ticket_id < self.tickets.items.len) {
            batch_timeline = @max(batch_timeline, self.tickets.items[task.ticket_id].timeline);
        };
    }

    return batch_timeline;
}

fn workerThread(self: *Self) !void {
    _ = try zprobe.threadInit("TRANSPORT");
    defer zprobe.threadDeinit();

    zprobe.event(.debug, "transport worker started", .{});
    while (true) {
        self.queue_mutex.lockUncancelable(self.ctx.view.io);
        if (self.stop_worker) {
            self.queue_mutex.unlock(self.ctx.view.io);
            zprobe.event(.debug, "transport worker stopped", .{});
            return;
        }

        var eligible: usize = 0;
        for (self.tasks.items) |task| {
            if (task.group_id) |id| {
                const group = self.findGroup(id) orelse continue;
                if (!group.finalized) continue;
            }

            eligible += 1;
        }

        if (eligible == 0) {
            self.queue_condition.waitUncancelable(self.ctx.view.io, &self.queue_mutex);
            self.queue_mutex.unlock(self.ctx.view.io);
            continue;
        }

        self.ready_tasks.ensureUnusedCapacity(self.alloc, eligible) catch {
            self.queue_mutex.unlock(self.ctx.view.io);
            std.Thread.yield() catch {};
            continue;
        };
        self.timeline_value +%= 1;

        const batch_timeline = self.timeline_value;
        var i: usize = 0;
        while (i < self.tasks.items.len) {
            const task = self.tasks.items[i];
            if (task.group_id) |id| {
                const group = self.findGroup(id) orelse {
                    i += 1;
                    continue;
                };

                if (!group.finalized) {
                    i += 1;
                    continue;
                }
            }

            self.ready_tasks.appendAssumeCapacity(self.tasks.orderedRemove(i));

            if (task.ticket_id < self.tickets.items.len) {
                self.tickets.items[task.ticket_id].timeline = batch_timeline;
                self.tickets.items[task.ticket_id].submitted = false;
            }
        }

        self.queue_condition.broadcast(self.ctx.view.io);
        self.queue_mutex.unlock(self.ctx.view.io);

        zprobe.event(.debug, "transport tasks made ready", .{ .task_count = eligible, .timeline = batch_timeline });
    }
}

fn drain(interface: *Interface) Interface.DrainError!void {
    const self = fromInterface(interface);
    const batch_timeline = try self.captureTasks();
    if (self.draining.items.len == 0) return;
    const batch_count = self.draining.items.len;

    const slot = &self.slots[self.slot_index];
    if (slot.submitted) {
        _ = try self.ctx.view.device.waitForFences(&.{slot.fence}, .true, std.math.maxInt(u64));
        slot.submitted = false;
    }

    var total_size: u64 = 0;
    for (self.draining.items) |*task| {
        total_size = std.math.add(u64, total_size, 15) catch return error.OutOfMemory;
        total_size &= ~@as(u64, 15);
        task.staging_offset = total_size;
        total_size = std.math.add(u64, total_size, task.size) catch return error.OutOfMemory;
    }

    try self.ensureStaging(slot, total_size);

    const staging_ptr = slot.staging.mapped orelse return error.StagingBufferNotMapped;
    for (self.draining.items) |task| {
        @memcpy(staging_ptr[task.staging_offset .. task.staging_offset + task.size], task.src[0..task.size]);
    }

    if (!slot.staging.coherent) slot.staging.flush(.from(self.ctx), 0, total_size);

    zprobe.event(.debug, "transport batch staged", .{ .task_count = batch_count, .byte_count = total_size, .timeline = batch_timeline, .slot = self.slot_index });

    self.pre_buffer_barriers.clearRetainingCapacity();
    self.post_buffer_barriers.clearRetainingCapacity();
    self.pre_image_barriers.clearRetainingCapacity();
    self.post_image_barriers.clearRetainingCapacity();

    for (self.draining.items) |task| {
        switch (task.dst) {
            .buffer => |dst| {
                if (!task.first_use) try self.pre_buffer_barriers.append(self.alloc, .{
                    .src_stage_mask = .{ .all_commands_bit = true },
                    .src_access_mask = .{ .memory_read_bit = true, .memory_write_bit = true },
                    .dst_stage_mask = .{ .all_transfer_bit = true },
                    .dst_access_mask = .{ .transfer_write_bit = true },
                    .src_queue_family_index = self.ctx.view.graphics_family,
                    .dst_queue_family_index = self.ctx.view.graphics_family,
                    .buffer = dst.handle,
                    .offset = dst.offset,
                    .size = dst.size,
                });
                try self.post_buffer_barriers.append(self.alloc, .{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = task.dst_stage,
                    .dst_access_mask = task.dst_access,
                    .src_queue_family_index = self.ctx.view.graphics_family,
                    .dst_queue_family_index = self.ctx.view.graphics_family,
                    .buffer = dst.handle,
                    .offset = dst.offset,
                    .size = dst.size,
                });
            },
            .image => |dst| {
                try self.pre_image_barriers.append(self.alloc, .{
                    .src_stage_mask = if (task.first_use) .{} else .{ .all_commands_bit = true },
                    .src_access_mask = if (task.first_use) .{} else .{ .memory_read_bit = true, .memory_write_bit = true },
                    .dst_stage_mask = .{ .all_transfer_bit = true },
                    .dst_access_mask = .{ .transfer_write_bit = true },
                    .old_layout = dst.current_layout,
                    .new_layout = .transfer_dst_optimal,
                    .src_queue_family_index = self.ctx.view.graphics_family,
                    .dst_queue_family_index = self.ctx.view.graphics_family,
                    .image = dst.image,
                    .subresource_range = .{ .aspect_mask = dst.layers.aspect_mask, .base_mip_level = dst.layers.mip_level, .level_count = 1, .base_array_layer = dst.layers.base_array_layer, .layer_count = dst.layers.layer_count },
                });
                try self.post_image_barriers.append(self.alloc, .{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = task.dst_stage,
                    .dst_access_mask = task.dst_access,
                    .old_layout = .transfer_dst_optimal,
                    .new_layout = dst.new_layout,
                    .src_queue_family_index = self.ctx.view.graphics_family,
                    .dst_queue_family_index = self.ctx.view.graphics_family,
                    .image = dst.image,
                    .subresource_range = .{ .aspect_mask = dst.layers.aspect_mask, .base_mip_level = dst.layers.mip_level, .level_count = 1, .base_array_layer = dst.layers.base_array_layer, .layer_count = dst.layers.layer_count },
                });
            },
        }
    }

    try self.ctx.view.device.resetCommandBuffer(slot.cmd_buf, .{});
    try self.ctx.view.device.beginCommandBuffer(slot.cmd_buf, &.{ .flags = .{ .one_time_submit_bit = true } });

    if (self.pre_buffer_barriers.items.len != 0 or self.pre_image_barriers.items.len != 0) self.ctx.view.device.cmdPipelineBarrier2(slot.cmd_buf, &.{
        .buffer_memory_barrier_count = @intCast(self.pre_buffer_barriers.items.len),
        .p_buffer_memory_barriers = self.pre_buffer_barriers.items.ptr,
        .image_memory_barrier_count = @intCast(self.pre_image_barriers.items.len),
        .p_image_memory_barriers = self.pre_image_barriers.items.ptr,
    });

    for (self.draining.items) |task| {
        switch (task.dst) {
            .buffer => |dst| {
                const region = vk.BufferCopy{ .src_offset = task.staging_offset, .dst_offset = dst.offset, .size = dst.size };
                self.ctx.view.device.cmdCopyBuffer(slot.cmd_buf, slot.staging.handle, dst.handle, (&region)[0..1]);
            },
            .image => |dst| {
                const region = vk.BufferImageCopy{ .buffer_offset = task.staging_offset, .buffer_row_length = 0, .buffer_image_height = 0, .image_subresource = dst.layers, .image_offset = dst.offset, .image_extent = dst.extent };
                self.ctx.view.device.cmdCopyBufferToImage(slot.cmd_buf, slot.staging.handle, dst.image, .transfer_dst_optimal, (&region)[0..1]);
            },
        }
    }

    if (self.post_buffer_barriers.items.len != 0 or self.post_image_barriers.items.len != 0) self.ctx.view.device.cmdPipelineBarrier2(slot.cmd_buf, &.{
        .buffer_memory_barrier_count = @intCast(self.post_buffer_barriers.items.len),
        .p_buffer_memory_barriers = self.post_buffer_barriers.items.ptr,
        .image_memory_barrier_count = @intCast(self.post_image_barriers.items.len),
        .p_image_memory_barriers = self.post_image_barriers.items.ptr,
    });

    try self.ctx.view.device.endCommandBuffer(slot.cmd_buf);

    self.queue_mutex.lockUncancelable(self.ctx.view.io);
    {
        defer self.queue_mutex.unlock(self.ctx.view.io);

        try self.ctx.view.device.resetFences(&.{slot.fence});

        self.ctx.view.graphics_submit_lock.lockUncancelable(self.ctx.view.io);
        defer self.ctx.view.graphics_submit_lock.unlock(self.ctx.view.io);

        try self.ctx.view.device.queueSubmit2(self.ctx.view.graphics_queue, (&vk.SubmitInfo2{
            .command_buffer_info_count = 1,
            .p_command_buffer_infos = (&vk.CommandBufferSubmitInfo{ .command_buffer = slot.cmd_buf, .device_mask = 0 })[0..1],
            .signal_semaphore_info_count = 1,
            .p_signal_semaphore_infos = (&vk.SemaphoreSubmitInfo{ .semaphore = self.timeline_semaphore, .value = batch_timeline, .stage_mask = .{ .all_commands_bit = true }, .device_index = 0 })[0..1],
        })[0..1], slot.fence);

        slot.submitted = true;
        for (self.draining.items) |task| if (task.ticket_id < self.tickets.items.len) {
            self.tickets.items[task.ticket_id].processing = false;
            self.tickets.items[task.ticket_id].submitted = true;
        };

        var group_index: usize = 0;
        while (group_index < self.groups.items.len) {
            const group = self.groups.items[group_index];
            if (group.finalized and group.ticket_id < self.tickets.items.len and self.tickets.items[group.ticket_id].timeline != 0) {
                _ = self.groups.swapRemove(group_index);
            } else group_index += 1;
        }
    }

    for (self.draining.items) |task| freeTask(task);

    self.draining.clearRetainingCapacity();
    self.pre_buffer_barriers.clearRetainingCapacity();
    self.post_buffer_barriers.clearRetainingCapacity();
    self.pre_image_barriers.clearRetainingCapacity();
    self.post_image_barriers.clearRetainingCapacity();
    self.slot_index = (self.slot_index + 1) % slot_count;

    zprobe.event(.debug, "transport batch submitted", .{ .task_count = batch_count, .byte_count = total_size, .timeline = batch_timeline, .slot = (self.slot_index + slot_count - 1) % slot_count });
}

test {
    std.testing.refAllDecls(@This());
}
