const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;

const Buffer = @import("Buffer.zig");
const root = @import("root.zig");
const Ctx = root.Ctx;

const Allocator = std.mem.Allocator;
const RingBuffer = @import("util/ring_buffer.zig").RingBuffer;

const FormatInfo = struct {
    bytes_per_block: u32,
};

fn getFormatInfo(format: vk.Format) !FormatInfo {
    return switch (format) {
        .r8_unorm => .{ .bytes_per_block = 1 },
        .r8g8_unorm => .{ .bytes_per_block = 2 },
        .r8g8b8_unorm => .{ .bytes_per_block = 3 },
        .r8g8b8a8_unorm, .r8g8b8a8_srgb => .{ .bytes_per_block = 4 },
        .b8g8r8a8_unorm, .b8g8r8a8_srgb => .{ .bytes_per_block = 4 },
        .r16_unorm => .{ .bytes_per_block = 2 },
        .r16g16_unorm => .{ .bytes_per_block = 4 },
        .r16g16b16_unorm => .{ .bytes_per_block = 6 },
        .r16g16b16a16_unorm => .{ .bytes_per_block = 8 },
        .bc1_rgba_unorm_block => .{ .bytes_per_block = 8 },
        .r32g32b32_sfloat, .r32g32b32_uint => .{ .bytes_per_block = 12 },
        .r32g32b32a32_sfloat, .r32g32b32a32_uint => .{ .bytes_per_block = 16 },
        .r32_sfloat, .r32_uint, .r32_sint => .{ .bytes_per_block = 4 },
        .r16_sfloat => .{ .bytes_per_block = 2 },
        .r16g16_sfloat => .{ .bytes_per_block = 4 },
        .r16g16b16a16_sfloat => .{ .bytes_per_block = 8 },
        .a2b10g10r10_unorm_pack32 => .{ .bytes_per_block = 4 },
        else => error.UnsupportedFormat,
    };
}

pub const FreeFn = *const fn (ctx: ?*anyopaque, ptr: *anyopaque) void;

const Owned = struct {
    free_fn: FreeFn,
    ctx: ?*anyopaque,
};

pub const Ticket = struct {
    value: u64,

    pub const none: Ticket = .{ .value = std.math.maxInt(u64) };

    const id_mask = 0x00000000FFFFFFFF;
    const gen_mask = 0xFFFFFFFF00000000;
    const gen_shift = 32;

    pub fn init(generation: u32, id_: u32) Ticket {
        return .{ .value = (@as(u64, id_) & id_mask) | ((@as(u64, generation) & 0xFFFFFFFF) << gen_shift) };
    }

    pub fn id(self: Ticket) u32 {
        return @truncate(self.value & id_mask);
    }

    pub fn gen(self: Ticket) u32 {
        return @truncate((self.value & gen_mask) >> gen_shift);
    }

    pub fn isValid(self: Ticket) bool {
        return self.value != none.value;
    }
};

const TaskDst = union(enum) {
    buffer_dst: struct {
        src_size: u32,
        buffer: vk.Buffer,
        offset: u32,
        initial_offset: u32,
    },
    image_dst: struct {
        image: vk.Image,
        subresource: vk.ImageSubresourceLayers,
        /// whole upload's layer range; the pre-copy transition covers this once
        full_layers: vk.ImageSubresourceLayers,
        current_layout: vk.ImageLayout,
        initial_base_array_layer: u32,
        offset: vk.Offset3D,
        extent: vk.Extent3D,
        src_row_length: u32,
        format: vk.Format,
        new_layout: vk.ImageLayout,
    },
};

const Task = struct {
    dst: TaskDst,
    src: [*]const u8,
    src_offset: u32,
    full_src_size: u32,
    parent_ticket_id: u32,
    ticket_id: u32,
    owning: ?Owned,
    dst_stage: vk.PipelineStageFlags2,
    dst_access: vk.AccessFlags2,
    /// true => the target resource was created after its last destruction and has
    /// never been written by any queue; no ownership handoff is performed for it
    first_use: bool,

    fn isLast(self: *const Task) bool {
        return self.ticket_id == self.parent_ticket_id;
    }

    fn requiredSize(self: *const Task) !u32 {
        return switch (self.dst) {
            .buffer_dst => |dst| dst.src_size,
            .image_dst => |dst| {
                const info = try getFormatInfo(dst.format);
                return dst.extent.width * dst.extent.height * dst.extent.depth * info.bytes_per_block;
            },
        };
    }

    fn uploadToStaging(self: *const Task, out: [*]u8) !void {
        switch (self.dst) {
            .buffer_dst => |dst| {
                @memcpy(out[0..dst.src_size], (self.src + self.src_offset)[0..dst.src_size]);
            },
            .image_dst => |dst| {
                const info = try getFormatInfo(dst.format);
                const texel_size = info.bytes_per_block;
                const packed_row = dst.extent.width * texel_size;
                const src_row = dst.src_row_length * texel_size;
                const src_slab = src_row * dst.extent.height;
                const base = self.src + self.src_offset;
                for (0..dst.extent.depth) |z| {
                    for (0..dst.extent.height) |y| {
                        const src_start = base[z * src_slab + y * src_row ..];
                        const dst_start = out[(z * dst.extent.height + y) * packed_row ..];
                        @memcpy(dst_start[0..packed_row], src_start[0..packed_row]);
                    }
                }
            },
        }
        if (self.owning) |owned| {
            owned.free_fn(owned.ctx, @ptrCast(@constCast(self.src)));
        }
    }

    fn split(self: *Task, budget: u32, rest: *std.ArrayListUnmanaged(Task), transport: *Self) !bool {
        switch (self.dst) {
            .buffer_dst => |*dst| {
                if (dst.src_size <= budget) return true;

                const remainder = Task{
                    .dst = .{ .buffer_dst = .{
                        .src_size = dst.src_size - budget,
                        .buffer = dst.buffer,
                        .offset = dst.offset + budget,
                        .initial_offset = dst.initial_offset,
                    } },
                    .src = self.src,
                    .src_offset = self.src_offset + budget,
                    .full_src_size = self.full_src_size,
                    .parent_ticket_id = self.parent_ticket_id,
                    .ticket_id = self.ticket_id,
                    .owning = self.owning,
                    .dst_stage = self.dst_stage,
                    .dst_access = self.dst_access,
                    .first_use = self.first_use,
                };
                try rest.append(transport.alloc, remainder);

                dst.src_size = budget;
                self.ticket_id = Ticket.none.id();
                self.owning = null;

                return false;
            },
            .image_dst => |*dst| {
                const info = try getFormatInfo(dst.format);
                const texel_size = info.bytes_per_block;
                if (budget < texel_size) return true;

                const max_texels = budget / texel_size;
                const w = @min(dst.extent.width, std.math.sqrt(max_texels));
                const h = @min(dst.extent.height, if (w > 0) max_texels / w else 1);
                if (w == 0 or h == 0) return true;

                const w1 = dst.extent.width - w;
                const h1 = h;
                const w2 = w + w1;
                const h2 = dst.extent.height - h;

                const sub1_offset = self.src_offset + w * texel_size;
                const sub2_offset = self.src_offset + h * dst.src_row_length * texel_size;

                const sub1_active = h1 > 0 and w1 > 0;
                const sub2_active = h2 > 0 and w2 > 0;

                if (!sub1_active and !sub2_active) return true;

                // Last-queued sub-task inherits the original ticket_id (isLast identity)
                const original_ticket_id = self.ticket_id;
                const sub2_is_last = sub2_active;

                const sub1 = Task{
                    .dst = .{ .image_dst = .{
                        .image = dst.image,
                        .subresource = dst.subresource,
                        .full_layers = dst.full_layers,
                        .current_layout = dst.current_layout,
                        .initial_base_array_layer = dst.initial_base_array_layer,
                        .offset = .{ .x = dst.offset.x + @as(i32, @intCast(w)), .y = dst.offset.y, .z = dst.offset.z },
                        .extent = .{ .width = w1, .height = h1, .depth = dst.extent.depth },
                        .src_row_length = dst.src_row_length,
                        .format = dst.format,
                        .new_layout = dst.new_layout,
                    } },
                    .src = self.src,
                    .src_offset = sub1_offset,
                    .full_src_size = self.full_src_size,
                    .parent_ticket_id = self.parent_ticket_id,
                    .ticket_id = if (sub2_is_last) Ticket.none.id() else original_ticket_id,
                    .owning = if (sub2_is_last) null else self.owning,
                    .dst_stage = self.dst_stage,
                    .dst_access = self.dst_access,
                    .first_use = self.first_use,
                };
                const sub2 = Task{
                    .dst = .{ .image_dst = .{
                        .image = dst.image,
                        .subresource = dst.subresource,
                        .full_layers = dst.full_layers,
                        .current_layout = dst.current_layout,
                        .initial_base_array_layer = dst.initial_base_array_layer,
                        .offset = .{ .x = dst.offset.x, .y = dst.offset.y + @as(i32, @intCast(h)), .z = dst.offset.z },
                        .extent = .{ .width = w2, .height = h2, .depth = dst.extent.depth },
                        .src_row_length = dst.src_row_length,
                        .format = dst.format,
                        .new_layout = dst.new_layout,
                    } },
                    .src = self.src,
                    .src_offset = sub2_offset,
                    .full_src_size = self.full_src_size,
                    .parent_ticket_id = self.parent_ticket_id,
                    .ticket_id = if (sub2_is_last) original_ticket_id else Ticket.none.id(),
                    .owning = if (sub2_is_last) self.owning else null,
                    .dst_stage = self.dst_stage,
                    .dst_access = self.dst_access,
                    .first_use = self.first_use,
                };

                self.ticket_id = Ticket.none.id();
                self.owning = null;
                dst.extent.width = w;
                dst.extent.height = h;

                if (sub1_active) try rest.append(transport.alloc, sub1);
                if (sub2_active) try rest.append(transport.alloc, sub2);

                return false;
            },
        }
    }

    fn recordCopy(self: *const Task, cmd_buf: vk.CommandBuffer, dev: vk.DeviceProxy, staging_buf: vk.Buffer, staging_offset: u32) void {
        switch (self.dst) {
            .buffer_dst => |dst| {
                const region = vk.BufferCopy{
                    .src_offset = staging_offset,
                    .dst_offset = dst.offset,
                    .size = dst.src_size,
                };
                dev.cmdCopyBuffer(cmd_buf, staging_buf, dst.buffer, (&region)[0..1]);
            },
            .image_dst => |dst| {
                const region = vk.BufferImageCopy{
                    .buffer_offset = staging_offset,
                    .buffer_row_length = 0,
                    .buffer_image_height = 0,
                    .image_subresource = dst.subresource,
                    .image_offset = dst.offset,
                    .image_extent = dst.extent,
                };
                dev.cmdCopyBufferToImage(cmd_buf, staging_buf, dst.image, .transfer_dst_optimal, (&region)[0..1]);
            },
        }
    }
};

const TicketEntry = struct {
    generation: u32,
    timeline: u64,
    used: bool,
};

const PendingSubmission = struct {
    wait_semaphore: vk.Semaphore = .null_handle,
    cmd_buf: vk.CommandBuffer = .null_handle,
    buffer_barriers: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty,
    image_barriers: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty,
    ticket_ids: std.ArrayListUnmanaged(u32) = .empty,
    timeline_value: u64 = 0,
    valid: bool = false,
};

const ResourceOwner = enum { graphics_owned, transport_owned };

const Self = @This();

const staging_buffer_size = 8_000_000;
const num_frames = 2;

alloc: Allocator,
has_dedicated_transport: bool,

dev: vk.DeviceProxy,
transport_queue: vk.Queue,
graphics_queue: vk.Queue,
vma_alloc: vma.VmaAllocator,
transport_family: u32,
graphics_family: u32,

/// serializes every submit on the graphics queue (frame submit/present in
/// root.zig, drain's non-dedicated submits, and worker-side ownership
/// handoffs); queue handles are externally synchronized
graphics_submit_lock: std.Io.Mutex,
barriers_cmd_buf: vk.CommandBuffer,
barriers_fence: vk.Fence,

owner_mutex: std.Io.Mutex,
buffer_owners: std.AutoHashMapUnmanaged(vk.Buffer, ResourceOwner),
image_owners: std.AutoHashMapUnmanaged(vk.Image, ResourceOwner),

staging_buffers: [num_frames]Buffer,
staging_ptrs: [num_frames][*]u8,
flush_staging: bool,

cmd_pool: vk.CommandPool,
current_cmd_buf: u32,
cmd_bufs: [num_frames]vk.CommandBuffer,
cmd_buf_fences: [num_frames]vk.Fence,

timeline_semaphore: vk.Semaphore,
timeline_counter: u64,
finished_timeline: u64,

current_task_queue: u32,
task_queues: [num_frames]RingBuffer(Task),
task_queue_lock: std.Io.Mutex,

full_upload_lock: std.Io.Mutex,

io: std.Io,
stop_worker: bool,
worker: ?std.Thread,

ticket_mutex: std.Io.Mutex,
ticket_timelines: std.ArrayListUnmanaged(TicketEntry),
free_tickets: std.ArrayListUnmanaged(Ticket),
ticket_condition: std.Io.Condition = std.Io.Condition.init,

pending_mutex: std.Io.Mutex,
pending_submissions: std.ArrayListUnmanaged(PendingSubmission),

drain_cmd_pool: vk.CommandPool,
drain_cmd_buf: vk.CommandBuffer,
drain_fence: vk.Fence,
last_wait_semaphore: vk.Semaphore = .null_handle,

pub fn init(self: *Self, ctx: Ctx.Query(&.{ .device, .vma_allocator, .transport_family, .graphics_family, .transport_queue, .graphics_queue, .dedicated_transport }), alloc: Allocator, io: std.Io) !void {
    self.alloc = alloc;
    self.has_dedicated_transport = ctx.view.dedicated_transport;
    self.dev = ctx.view.device;
    self.transport_queue = ctx.view.transport_queue;
    self.graphics_queue = ctx.view.graphics_queue;
    self.vma_alloc = ctx.view.vma_allocator;
    self.transport_family = ctx.view.transport_family;
    self.graphics_family = ctx.view.graphics_family;
    self.current_cmd_buf = 0;
    self.timeline_counter = 0;
    self.finished_timeline = 0;
    self.current_task_queue = 0;
    self.stop_worker = false;
    self.worker = null;
    self.flush_staging = false;
    self.io = io;
    self.task_queue_lock = std.Io.Mutex.init;
    self.full_upload_lock = std.Io.Mutex.init;
    self.ticket_mutex = std.Io.Mutex.init;
    self.pending_mutex = std.Io.Mutex.init;
    self.graphics_submit_lock = std.Io.Mutex.init;
    self.owner_mutex = std.Io.Mutex.init;
    self.buffer_owners = .empty;
    self.image_owners = .empty;
    self.ticket_timelines = .empty;
    self.free_tickets = .empty;
    self.pending_submissions = .empty;
    for (&self.task_queues) |*q| q.* = .{};

    var staging_created: u32 = 0;
    errdefer for (self.staging_buffers[0..staging_created]) |*buf| buf.deinitNow(.from(ctx));
    for (0..num_frames) |i| {
        self.staging_buffers[i] = try Buffer.init(.from(ctx), .transport, "Transport staging", staging_buffer_size, .{ .transfer_src_bit = true }, .cpu_to_gpu_staging);
        staging_created += 1;
        self.staging_ptrs[i] = self.staging_buffers[i].mapped orelse return error.StagingBufferNotMapped;
    }
    self.flush_staging = !self.staging_buffers[0].coherent;

    self.cmd_pool = try ctx.view.device.createCommandPool(&.{
        .flags = .{ .reset_command_buffer_bit = true },
        .queue_family_index = ctx.view.transport_family,
    }, null);
    errdefer ctx.view.device.destroyCommandPool(self.cmd_pool, null);
    var cmd_bufs: [num_frames]vk.CommandBuffer = undefined;
    try ctx.view.device.allocateCommandBuffers(&.{
        .command_pool = self.cmd_pool,
        .level = .primary,
        .command_buffer_count = num_frames,
    }, &cmd_bufs);
    self.cmd_bufs = cmd_bufs;

    var fences_created: u32 = 0;
    errdefer for (self.cmd_buf_fences[0..fences_created]) |f| ctx.view.device.destroyFence(f, null);
    for (&self.cmd_buf_fences) |*fence| {
        fence.* = try ctx.view.device.createFence(&.{
            .flags = .{ .signaled_bit = true },
        }, null);
        fences_created += 1;
    }
    self.timeline_semaphore = try ctx.view.device.createSemaphore(&.{
        .p_next = &vk.SemaphoreTypeCreateInfo{
            .initial_value = 0,
            .semaphore_type = .timeline,
        },
    }, null);
    errdefer ctx.view.device.destroySemaphore(self.timeline_semaphore, null);

    self.drain_cmd_pool = try ctx.view.device.createCommandPool(&.{
        .flags = .{ .reset_command_buffer_bit = true },
        .queue_family_index = ctx.view.graphics_family,
    }, null);
    errdefer ctx.view.device.destroyCommandPool(self.drain_cmd_pool, null);
    var drain_cmd_buf: vk.CommandBuffer = undefined;
    try ctx.view.device.allocateCommandBuffers(&.{
        .command_pool = self.drain_cmd_pool,
        .level = .primary,
        .command_buffer_count = 1,
    }, (&drain_cmd_buf)[0..1]);
    self.drain_cmd_buf = drain_cmd_buf;

    self.drain_fence = try ctx.view.device.createFence(&.{
        .flags = .{ .signaled_bit = true },
    }, null);
    self.last_wait_semaphore = .null_handle;

    var barriers_cmd_bufs: [1]vk.CommandBuffer = undefined;
    try ctx.view.device.allocateCommandBuffers(&.{
        .command_pool = self.drain_cmd_pool,
        .level = .primary,
        .command_buffer_count = 1,
    }, &barriers_cmd_bufs);
    self.barriers_cmd_buf = barriers_cmd_bufs[0];

    self.barriers_fence = try ctx.view.device.createFence(&.{
        .flags = .{ .signaled_bit = true },
    }, null);

    self.worker = try std.Thread.spawn(.{}, workerThread, .{ self, Ctx.Query(&.{ .device, .vma_allocator }).from(ctx) });
}

pub fn deinit(self: *Self, ctx: Ctx.Query(&.{ .device, .vma_allocator })) void {
    @atomicStore(bool, &self.stop_worker, true, .monotonic);
    if (self.worker) |w| w.join();

    for (&self.staging_buffers) |*buf| buf.deinitNow(.from(ctx));

    ctx.view.device.destroyCommandPool(self.cmd_pool, null);
    ctx.view.device.destroyCommandPool(self.drain_cmd_pool, null);

    if (self.last_wait_semaphore != .null_handle) {
        // The last drain submission may still be in flight; wait before destroying its semaphore.
        _ = ctx.view.device.waitForFences(&[_]vk.Fence{self.drain_fence}, .true, std.math.maxInt(u64)) catch {};
        ctx.view.device.destroySemaphore(self.last_wait_semaphore, null);
    }

    ctx.view.device.destroyFence(self.drain_fence, null);
    for (self.cmd_buf_fences) |f| ctx.view.device.destroyFence(f, null);

    ctx.view.device.destroySemaphore(self.timeline_semaphore, null);

    for (&self.task_queues) |*q| {
        while (q.popFirst()) |task| {
            if (task.owning) |owned|
                owned.free_fn(owned.ctx, @ptrCast(@constCast(task.src)));
        }
        q.deinit(self.alloc);
    }

    self.ticket_timelines.deinit(self.alloc);
    self.free_tickets.deinit(self.alloc);

    self.buffer_owners.deinit(self.alloc);
    self.image_owners.deinit(self.alloc);

    ctx.view.device.destroyFence(self.barriers_fence, null);

    for (self.pending_submissions.items) |*ps| {
        if (ps.wait_semaphore != .null_handle) {
            if (ps.wait_semaphore == self.last_wait_semaphore)
                self.last_wait_semaphore = .null_handle;
            ctx.view.device.destroySemaphore(ps.wait_semaphore, null);
        }
        ps.buffer_barriers.deinit(self.alloc);
        ps.image_barriers.deinit(self.alloc);
        ps.ticket_ids.deinit(self.alloc);
    }

    self.pending_submissions.deinit(self.alloc);
}

pub fn uploadBuffer(
    self: *Self,
    priority: bool,
    src: []const u8,
    owning: ?FreeFn,
    owning_ctx: ?*anyopaque,
    dst: vk.Buffer,
    dst_offset: u32,
    dst_stage: vk.PipelineStageFlags2,
    dst_access: vk.AccessFlags2,
    first_use: bool,
) !Ticket {
    const ticket = try self.getFreeTicket();
    const owned = if (owning) |f| Owned{ .free_fn = f, .ctx = owning_ctx } else null;
    const src_len: u32 = @intCast(src.len);
    const task = Task{
        .dst = .{ .buffer_dst = .{
            .src_size = src_len,
            .buffer = dst,
            .offset = dst_offset,
            .initial_offset = dst_offset,
        } },
        .src = src.ptr,
        .src_offset = 0,
        .full_src_size = src_len,
        .parent_ticket_id = ticket.id(),
        .ticket_id = ticket.id(),
        .owning = owned,
        .dst_stage = dst_stage,
        .dst_access = dst_access,
        .first_use = first_use,
    };
    self.task_queue_lock.lockUncancelable(self.io);
    defer self.task_queue_lock.unlock(self.io);
    const q = &self.task_queues[self.current_task_queue];
    if (priority) try q.prepend(self.alloc, task) else try q.append(self.alloc, task);
    return ticket;
}

pub fn uploadImage(
    self: *Self,
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
) !Ticket {
    const info = try getFormatInfo(format);
    const layer_size = dimension.width * dimension.height * dimension.depth * info.bytes_per_block;
    const num_layers = dst_layers.layer_count;
    const total_size = layer_size * num_layers;

    self.task_queue_lock.lockUncancelable(self.io);
    defer self.task_queue_lock.unlock(self.io);
    const q = &self.task_queues[self.current_task_queue];

    const count = @min(@as(usize, num_layers), 128);

    const parent_ticket = try self.getFreeTicket();

    const owned = if (owning) |f| Owned{ .free_fn = f, .ctx = owning_ctx } else null;

    try q.ensureUnusedCapacity(self.alloc, count);
    const full_layers: vk.ImageSubresourceLayers = .{
        .aspect_mask = dst_layers.aspect_mask,
        .mip_level = dst_layers.mip_level,
        .base_array_layer = dst_layers.base_array_layer,
        .layer_count = dst_layers.layer_count,
    };
    if (priority) {
        var i: usize = count;
        while (i > 0) {
            i -= 1;
            const is_last = i == count - 1;
            q.prependOneAssumeCapacity().* = .{
                .dst = .{ .image_dst = .{
                    .image = dst,
                    .subresource = .{
                        .aspect_mask = dst_layers.aspect_mask,
                        .mip_level = dst_layers.mip_level,
                        .base_array_layer = dst_layers.base_array_layer + @as(u32, @intCast(i)),
                        .layer_count = 1,
                    },
                    .full_layers = full_layers,
                    .current_layout = current_layout,
                    .initial_base_array_layer = dst_layers.base_array_layer,
                    .offset = dst_offset,
                    .extent = dimension,
                    .src_row_length = dimension.width,
                    .format = format,
                    .new_layout = new_layout,
                } },
                .src = src.ptr,
                .src_offset = @as(u32, @intCast(i * layer_size)),
                .full_src_size = total_size,
                .parent_ticket_id = parent_ticket.id(),
                .ticket_id = if (is_last) parent_ticket.id() else Ticket.none.id(),
                .owning = if (is_last) owned else null,
                .dst_stage = dst_stage,
                .dst_access = dst_access,
                .first_use = first_use,
            };
        }
    } else {
        for (0..count) |i| {
            const is_last = i == count - 1;
            q.appendAssumeCapacity(.{
                .dst = .{ .image_dst = .{
                    .image = dst,
                    .subresource = .{
                        .aspect_mask = dst_layers.aspect_mask,
                        .mip_level = dst_layers.mip_level,
                        .base_array_layer = dst_layers.base_array_layer + @as(u32, @intCast(i)),
                        .layer_count = 1,
                    },
                    .full_layers = full_layers,
                    .current_layout = current_layout,
                    .initial_base_array_layer = dst_layers.base_array_layer,
                    .offset = dst_offset,
                    .extent = dimension,
                    .src_row_length = dimension.width,
                    .format = format,
                    .new_layout = new_layout,
                } },
                .src = src.ptr,
                .src_offset = @as(u32, @intCast(i * layer_size)),
                .full_src_size = total_size,
                .parent_ticket_id = parent_ticket.id(),
                .ticket_id = if (is_last) parent_ticket.id() else Ticket.none.id(),
                .owning = if (is_last) owned else null,
                .dst_stage = dst_stage,
                .dst_access = dst_access,
                .first_use = first_use,
            });
        }
    }

    return parent_ticket;
}

pub fn isReady(self: *Self, t: Ticket) !bool {
    if (!t.isValid()) return false;
    self.ticket_mutex.lockUncancelable(self.io);
    defer self.ticket_mutex.unlock(self.io);

    if (t.id() >= self.ticket_timelines.items.len) return false;

    const entry = self.ticket_timelines.items[t.id()];
    if (!entry.used) return false;
    if (entry.generation > t.gen()) return true;
    if (entry.timeline == 0) return false;

    return try self.isTimelineReady(entry.timeline);
}

pub fn waitOn(self: *Self, tickets: []const Ticket) vk.SemaphoreSubmitInfo {
    var largest: u64 = 0;
    for (tickets) |t| {
        if (!t.isValid()) continue;
        var timeline: u64 = 0;
        {
            self.ticket_mutex.lockUncancelable(self.io);
            defer self.ticket_mutex.unlock(self.io);
            if (t.id() < self.ticket_timelines.items.len) {
                const e = self.ticket_timelines.items[t.id()];
                if (!e.used or e.generation > t.gen()) continue;
                timeline = e.timeline;
            }
        }
        if (timeline == 0) {
            self.ticket_mutex.lockUncancelable(self.io);
            defer self.ticket_mutex.unlock(self.io);
            while (true) {
                if (t.id() < self.ticket_timelines.items.len) {
                    const e = self.ticket_timelines.items[t.id()];
                    if (!e.used or e.generation > t.gen()) {
                        timeline = std.math.maxInt(u64);
                        break;
                    }
                    if (e.timeline != 0) {
                        timeline = e.timeline;
                        break;
                    }
                } else {
                    timeline = std.math.maxInt(u64);
                    break;
                }
                self.ticket_condition.waitUncancelable(self.io, &self.ticket_mutex);
            }
        }
        if (timeline != std.math.maxInt(u64)) largest = @max(largest, timeline);
    }
    return .{
        .semaphore = self.timeline_semaphore,
        .value = largest,
        .stage_mask = .{ .all_commands_bit = true },
        .device_index = 0,
    };
}

pub fn unqueue(self: *Self, t: Ticket, free_src: bool) void {
    if (!t.isValid()) return;
    if (self.isReady(t) catch true) return;

    self.full_upload_lock.lockUncancelable(self.io);
    defer self.full_upload_lock.unlock(self.io);
    self.task_queue_lock.lockUncancelable(self.io);
    defer self.task_queue_lock.unlock(self.io);

    var removed_any = false;
    for (&self.task_queues) |*q| {
        var i: usize = 0;
        while (i < q.len) {
            const task_ptr = q.get(i).?;
            if (task_ptr.parent_ticket_id == t.id()) {
                const gen = blk: {
                    self.ticket_mutex.lockUncancelable(self.io);
                    defer self.ticket_mutex.unlock(self.io);
                    if (t.id() < self.ticket_timelines.items.len) break :blk self.ticket_timelines.items[t.id()].generation;
                    break :blk 0;
                };
                if (gen == t.gen()) {
                    const owning_fn = task_ptr.owning;
                    _ = q.orderedRemove(i);
                    removed_any = true;
                    if (free_src and owning_fn != null) owning_fn.?.free_fn(owning_fn.?.ctx, @ptrCast(@constCast(task_ptr.src)));
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        }
    }

    if (removed_any) {
        self.ticket_mutex.lockUncancelable(self.io);
        defer self.ticket_mutex.unlock(self.io);
        if (t.id() < self.ticket_timelines.items.len) {
            var entry = &self.ticket_timelines.items[t.id()];
            if (entry.generation == t.gen()) {
                entry.generation +%= 1;
                self.free_tickets.append(self.alloc, Ticket.init(entry.generation, @intCast(t.id()))) catch {};
            }
        }
    }
}

pub fn drain(self: *Self, ctx: Ctx.Query(&.{ .device, .graphics_queue })) !void {
    self.pending_mutex.lockUncancelable(self.io);
    defer self.pending_mutex.unlock(self.io);

    for (self.pending_submissions.items) |*sub| {
        if (!sub.valid) continue;

        _ = try ctx.view.device.waitForFences(&[_]vk.Fence{self.drain_fence}, .true, std.math.maxInt(u64));
        // The fence covers the previous drain submission, so its per-batch semaphore is idle.
        if (self.last_wait_semaphore != .null_handle) {
            ctx.view.device.destroySemaphore(self.last_wait_semaphore, null);
            self.last_wait_semaphore = .null_handle;
        }
        try ctx.view.device.resetFences(&[_]vk.Fence{self.drain_fence});

        try ctx.view.device.resetCommandBuffer(self.drain_cmd_buf, .{});
        try ctx.view.device.beginCommandBuffer(self.drain_cmd_buf, &.{ .flags = .{ .one_time_submit_bit = true } });

        if (sub.buffer_barriers.items.len > 0 or sub.image_barriers.items.len > 0) {
            ctx.view.device.cmdPipelineBarrier2(self.drain_cmd_buf, &.{
                .buffer_memory_barrier_count = @intCast(sub.buffer_barriers.items.len),
                .p_buffer_memory_barriers = sub.buffer_barriers.items.ptr,
                .image_memory_barrier_count = @intCast(sub.image_barriers.items.len),
                .p_image_memory_barriers = sub.image_barriers.items.ptr,
            });
        }

        try ctx.view.device.endCommandBuffer(self.drain_cmd_buf);

        if (self.has_dedicated_transport) {
            try ctx.view.device.queueSubmit2(ctx.view.graphics_queue, (&vk.SubmitInfo2{
                .wait_semaphore_info_count = 1,
                .p_wait_semaphore_infos = (&vk.SemaphoreSubmitInfo{
                    .semaphore = sub.wait_semaphore,
                    .value = 0,
                    .stage_mask = .{ .all_commands_bit = true },
                    .device_index = 0,
                })[0..1],
                .command_buffer_info_count = 1,
                .p_command_buffer_infos = (&vk.CommandBufferSubmitInfo{
                    .command_buffer = self.drain_cmd_buf,
                    .device_mask = 0,
                })[0..1],
                .signal_semaphore_info_count = 1,
                .p_signal_semaphore_infos = (&vk.SemaphoreSubmitInfo{
                    .semaphore = self.timeline_semaphore,
                    .value = sub.timeline_value,
                    .stage_mask = .{ .all_commands_bit = true },
                    .device_index = 0,
                })[0..1],
            })[0..1], self.drain_fence);
        } else {
            try ctx.view.device.queueSubmit2(ctx.view.graphics_queue, (&vk.SubmitInfo2{
                .command_buffer_info_count = 1,
                .p_command_buffer_infos = (&vk.CommandBufferSubmitInfo{
                    .command_buffer = sub.cmd_buf,
                    .device_mask = 0,
                })[0..1],
                .signal_semaphore_info_count = 1,
                .p_signal_semaphore_infos = (&vk.SemaphoreSubmitInfo{
                    .semaphore = self.timeline_semaphore,
                    .value = sub.timeline_value,
                    .stage_mask = .{ .all_commands_bit = true },
                    .device_index = 0,
                })[0..1],
            })[0..1], self.drain_fence);
        }
        if (self.has_dedicated_transport) self.last_wait_semaphore = sub.wait_semaphore;

        {
            self.ticket_mutex.lockUncancelable(self.io);
            defer self.ticket_mutex.unlock(self.io);
            for (sub.ticket_ids.items) |tid| {
                if (tid < self.ticket_timelines.items.len) {
                    self.ticket_timelines.items[tid].timeline = sub.timeline_value;
                }
            }
        }
        self.ticket_condition.broadcast(self.io);
    }

    for (self.pending_submissions.items) |*ps| {
        ps.buffer_barriers.deinit(self.alloc);
        ps.image_barriers.deinit(self.alloc);
        ps.ticket_ids.deinit(self.alloc);
    }
    self.pending_submissions.clearRetainingCapacity();
}

pub fn getTimeline(self: *const Self) u64 {
    var v: u64 = undefined;
    self.dev.getSemaphoreCounterValue(self.timeline_semaphore, &v);
    return v;
}

fn isTimelineReady(self: *Self, timeline: u64) !bool {
    const finished = @atomicLoad(u64, &self.finished_timeline, .monotonic);
    if (finished >= timeline) return true;
    const info = vk.SemaphoreWaitInfo{
        .semaphore_count = 1,
        .p_semaphores = @ptrCast(&self.timeline_semaphore),
        .p_values = @ptrCast(&timeline),
    };
    if (try self.dev.waitSemaphores(&info, 0) == .success) {
        const cur = @atomicLoad(u64, &self.finished_timeline, .monotonic);
        if (timeline > cur) @atomicStore(u64, &self.finished_timeline, timeline, .monotonic);
        return true;
    }
    return false;
}

fn getFreeTicket(self: *Self) !Ticket {
    self.ticket_mutex.lockUncancelable(self.io);
    defer self.ticket_mutex.unlock(self.io);

    if (self.free_tickets.items.len > 0) return self.free_tickets.pop().?;

    for (self.ticket_timelines.items, 0..) |*entry, i| {
        if (!entry.used) continue;
        if (entry.timeline == 0) continue;

        if (try self.isTimelineReady(entry.timeline)) {
            entry.generation +%= 1;
            entry.timeline = 0;
            try self.free_tickets.append(self.alloc, Ticket.init(entry.generation, @intCast(i)));
        }
    }

    if (self.free_tickets.items.len > 0) return self.free_tickets.pop().?;

    const id = @as(u32, @intCast(self.ticket_timelines.items.len));
    try self.ticket_timelines.append(self.alloc, .{ .generation = 0, .timeline = 0, .used = true });

    return Ticket.init(0, id);
}

fn workerThread(self: *Self, ctx: Ctx.Query(&.{ .device, .vma_allocator })) !void {
    var cmd_buf_idx: u32 = 0;

    while (!@atomicLoad(bool, &self.stop_worker, .monotonic)) {
        self.full_upload_lock.lockUncancelable(self.io);
        defer self.full_upload_lock.unlock(self.io);

        self.task_queue_lock.lockUncancelable(self.io);
        const process_idx = self.current_task_queue;
        self.current_task_queue = (self.current_task_queue + 1) % Self.num_frames;
        self.task_queue_lock.unlock(self.io);

        var queue = &self.task_queues[process_idx];
        if (queue.first() == null) {
            std.Thread.yield() catch {};
            continue;
        }

        const slot = cmd_buf_idx;
        cmd_buf_idx = (cmd_buf_idx + 1) % Self.num_frames;

        const cmd_buf = self.cmd_bufs[slot];
        const fence = self.cmd_buf_fences[slot];
        const staging_buf = self.staging_buffers[slot];
        const staging_ptr = self.staging_ptrs[slot];

        _ = try ctx.view.device.waitForFences(&[_]vk.Fence{fence}, .true, std.math.maxInt(u64));
        try ctx.view.device.resetFences(&[_]vk.Fence{fence});

        try ctx.view.device.resetCommandBuffer(cmd_buf, .{});
        try ctx.view.device.beginCommandBuffer(cmd_buf, &.{ .flags = .{ .one_time_submit_bit = true } });

        var size: u32 = 0;
        var batch: std.ArrayListUnmanaged(Task) = .empty;
        defer batch.deinit(self.alloc);

        while (queue.first()) |first_ptr| {
            const budget = Self.staging_buffer_size - size;
            if (budget == 0) break;

            var task = first_ptr.*;

            var rest: std.ArrayListUnmanaged(Task) = .empty;
            defer rest.deinit(self.alloc);

            if (budget < try task.requiredSize()) {
                const split_result = try task.split(budget, &rest, self);
                if (split_result) break;

                _ = queue.popFirst();

                var i: usize = rest.items.len;
                while (i > 0) {
                    i -= 1;
                    try queue.prepend(self.alloc, rest.items[i]);
                }
            } else {
                _ = queue.popFirst();
            }

            try task.uploadToStaging(staging_ptr + size);
            size += try task.requiredSize();
            try batch.append(self.alloc, task);
        }

        if (self.flush_staging) staging_buf.flush(.from(ctx), 0, Self.staging_buffer_size);

        // --- per-batch barrier planning ---
        // handoff: graphics -> transport (release submitted on graphics via
        // submitBarriersLocked, acquire recorded into the transport cmdbuf)
        var handoff_rel_bufs: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty;
        defer handoff_rel_bufs.deinit(self.alloc);
        var handoff_rel_imgs: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty;
        defer handoff_rel_imgs.deinit(self.alloc);
        var pre_acq_bufs: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty;
        defer pre_acq_bufs.deinit(self.alloc);
        var pre_img_bars: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty;
        defer pre_img_bars.deinit(self.alloc);
        // tail: transport -> graphics (release recorded after copies in the
        // transport cmdbuf, acquire submitted on graphics after the batch)
        var tail_rel_bufs: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty;
        defer tail_rel_bufs.deinit(self.alloc);
        var tail_rel_imgs: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty;
        defer tail_rel_imgs.deinit(self.alloc);
        var post_acq_bufs: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty;
        defer post_acq_bufs.deinit(self.alloc);
        var post_img_bars: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty;
        defer post_img_bars.deinit(self.alloc);

        var seen_bufs: std.ArrayListUnmanaged(vk.Buffer) = .empty;
        defer seen_bufs.deinit(self.alloc);
        var seen_imgs: std.ArrayListUnmanaged(vk.Image) = .empty;
        defer seen_imgs.deinit(self.alloc);

        for (batch.items) |*t| {
            switch (t.dst) {
                .buffer_dst => |dst| {
                    const seen = blk: {
                        for (seen_bufs.items) |h| if (h == dst.buffer) break :blk true;
                        break :blk false;
                    };
                    if (seen) continue;
                    try seen_bufs.append(self.alloc, dst.buffer);

                    // exclusive-sharing ownership handshake; same-queue setups
                    // never transfer ownership
                    if (!self.has_dedicated_transport or t.first_use) {
                        if (self.has_dedicated_transport) {
                            self.owner_mutex.lockUncancelable(self.io);
                            try self.buffer_owners.put(self.alloc, dst.buffer, .graphics_owned);
                            self.owner_mutex.unlock(self.io);
                        }
                        continue;
                    }

                    self.owner_mutex.lockUncancelable(self.io);
                    const gop = try self.buffer_owners.getOrPut(self.alloc, dst.buffer);
                    const needs_handoff = !gop.found_existing or gop.value_ptr.* == .graphics_owned;
                    gop.value_ptr.* = .transport_owned;
                    self.owner_mutex.unlock(self.io);

                    if (!needs_handoff) continue;

                    try handoff_rel_bufs.append(self.alloc, .{
                        .src_stage_mask = .{ .all_commands_bit = true },
                        .src_access_mask = .{ .memory_read_bit = true, .memory_write_bit = true },
                        .dst_stage_mask = .{},
                        .dst_access_mask = .{},
                        .src_queue_family_index = self.graphics_family,
                        .dst_queue_family_index = self.transport_family,
                        .buffer = dst.buffer,
                        .offset = dst.initial_offset,
                        .size = t.full_src_size,
                    });
                    try pre_acq_bufs.append(self.alloc, .{
                        .src_stage_mask = .{},
                        .src_access_mask = .{},
                        .dst_stage_mask = .{ .all_transfer_bit = true },
                        .dst_access_mask = .{ .transfer_write_bit = true },
                        .src_queue_family_index = self.graphics_family,
                        .dst_queue_family_index = self.transport_family,
                        .buffer = dst.buffer,
                        .offset = dst.initial_offset,
                        .size = t.full_src_size,
                    });
                },
                .image_dst => |dst| {
                    const seen = blk: {
                        for (seen_imgs.items) |h| if (h == dst.image) break :blk true;
                        break :blk false;
                    };

                    var handoff = false;
                    if (!seen) {
                        try seen_imgs.append(self.alloc, dst.image);

                        if (self.has_dedicated_transport) {
                            if (!t.first_use) {
                                self.owner_mutex.lockUncancelable(self.io);
                                const gop = try self.image_owners.getOrPut(self.alloc, dst.image);
                                handoff = !gop.found_existing or gop.value_ptr.* == .graphics_owned;
                                gop.value_ptr.* = .transport_owned;
                                self.owner_mutex.unlock(self.io);
                            } else {
                                self.owner_mutex.lockUncancelable(self.io);
                                try self.image_owners.put(self.alloc, dst.image, .graphics_owned);
                                self.owner_mutex.unlock(self.io);
                            }
                        }
                    }
                    if (seen and !(self.has_dedicated_transport and handoff)) continue;

                    const src_family = if (handoff) self.graphics_family else if (self.has_dedicated_transport) self.transport_family else self.graphics_family;
                    try pre_img_bars.append(self.alloc, .{
                        .src_stage_mask = if (handoff) .{ .all_commands_bit = true } else .{},
                        .src_access_mask = if (handoff) .{ .memory_read_bit = true, .memory_write_bit = true } else .{},
                        .dst_stage_mask = .{ .all_transfer_bit = true },
                        .dst_access_mask = .{ .transfer_write_bit = true },
                        .old_layout = dst.current_layout,
                        .new_layout = .transfer_dst_optimal,
                        .src_queue_family_index = src_family,
                        .dst_queue_family_index = if (self.has_dedicated_transport) self.transport_family else self.graphics_family,
                        .image = dst.image,
                        .subresource_range = .{
                            .aspect_mask = dst.full_layers.aspect_mask,
                            .base_mip_level = dst.full_layers.mip_level,
                            .level_count = 1,
                            .base_array_layer = dst.full_layers.base_array_layer,
                            .layer_count = dst.full_layers.layer_count,
                        },
                    });
                    if (handoff) {
                        try handoff_rel_imgs.append(self.alloc, .{
                            .src_stage_mask = .{ .all_commands_bit = true },
                            .src_access_mask = .{ .memory_read_bit = true, .memory_write_bit = true },
                            .dst_stage_mask = .{},
                            .dst_access_mask = .{},
                            .old_layout = .undefined,
                            .new_layout = .undefined,
                            .src_queue_family_index = self.graphics_family,
                            .dst_queue_family_index = self.transport_family,
                            .image = dst.image,
                            .subresource_range = .{
                                .aspect_mask = dst.full_layers.aspect_mask,
                                .base_mip_level = dst.full_layers.mip_level,
                                .level_count = 1,
                                .base_array_layer = dst.full_layers.base_array_layer,
                                .layer_count = dst.full_layers.layer_count,
                            },
                        });
                    }

                    if (t.isLast()) {
                        try tail_rel_imgs.append(self.alloc, .{
                            .src_stage_mask = .{ .all_transfer_bit = true },
                            .src_access_mask = .{ .transfer_write_bit = true },
                            .dst_stage_mask = .{},
                            .dst_access_mask = .{},
                            .old_layout = .transfer_dst_optimal,
                            .new_layout = dst.new_layout,
                            .src_queue_family_index = self.transport_family,
                            .dst_queue_family_index = self.graphics_family,
                            .image = dst.image,
                            .subresource_range = .{
                                .aspect_mask = dst.subresource.aspect_mask,
                                .base_mip_level = dst.subresource.mip_level,
                                .level_count = 1,
                                .base_array_layer = dst.subresource.base_array_layer,
                                .layer_count = 1,
                            },
                        });
                        try post_img_bars.append(self.alloc, .{
                            .src_stage_mask = .{},
                            .src_access_mask = .{},
                            .dst_stage_mask = t.dst_stage,
                            .dst_access_mask = t.dst_access,
                            .old_layout = .transfer_dst_optimal,
                            .new_layout = dst.new_layout,
                            .src_queue_family_index = self.transport_family,
                            .dst_queue_family_index = self.graphics_family,
                            .image = dst.image,
                            .subresource_range = .{
                                .aspect_mask = dst.subresource.aspect_mask,
                                .base_mip_level = dst.subresource.mip_level,
                                .level_count = 1,
                                .base_array_layer = dst.subresource.base_array_layer,
                                .layer_count = 1,
                            },
                        });
                    }
                },
            }
        }

        // --- record transport cmdbuf: pre barriers -> copies -> tail releases ---
        if (pre_acq_bufs.items.len > 0 or pre_img_bars.items.len > 0) {
            ctx.view.device.cmdPipelineBarrier2(cmd_buf, &.{
                .buffer_memory_barrier_count = @intCast(pre_acq_bufs.items.len),
                .p_buffer_memory_barriers = pre_acq_bufs.items.ptr,
                .image_memory_barrier_count = @intCast(pre_img_bars.items.len),
                .p_image_memory_barriers = pre_img_bars.items.ptr,
            });
        }

        var copy_offset: u32 = 0;
        for (batch.items) |*t| {
            t.recordCopy(cmd_buf, ctx.view.device, staging_buf.handle, copy_offset);
            copy_offset += try t.requiredSize();
        }

        var ticket_ids: std.ArrayListUnmanaged(u32) = .empty;

        var has_graphics_work = false;

        if (self.has_dedicated_transport) {
            // submitBarriersLocked only borrows .items; the list itself is
            // released here (the non-dedicated branch transfers it to
            // pending_submissions instead)
            defer ticket_ids.deinit(self.alloc);
            for (batch.items) |*t| {
                if (!t.isLast()) continue;
                has_graphics_work = true;
                try ticket_ids.append(self.alloc, t.ticket_id);

                switch (t.dst) {
                    .buffer_dst => |dst| {
                        try tail_rel_bufs.append(self.alloc, .{
                            .src_stage_mask = .{ .all_transfer_bit = true },
                            .src_access_mask = .{ .transfer_write_bit = true },
                            .dst_stage_mask = .{},
                            .dst_access_mask = .{},
                            .src_queue_family_index = self.transport_family,
                            .dst_queue_family_index = self.graphics_family,
                            .buffer = dst.buffer,
                            .offset = dst.initial_offset,
                            .size = t.full_src_size,
                        });
                        try post_acq_bufs.append(self.alloc, .{
                            .src_stage_mask = .{},
                            .src_access_mask = .{},
                            .dst_stage_mask = t.dst_stage,
                            .dst_access_mask = t.dst_access,
                            .src_queue_family_index = self.transport_family,
                            .dst_queue_family_index = self.graphics_family,
                            .buffer = dst.buffer,
                            .offset = dst.initial_offset,
                            .size = t.full_src_size,
                        });
                    },
                    .image_dst => {},
                }
            }

            if (tail_rel_bufs.items.len > 0 or tail_rel_imgs.items.len > 0) {
                ctx.view.device.cmdPipelineBarrier2(cmd_buf, &.{
                    .buffer_memory_barrier_count = @intCast(tail_rel_bufs.items.len),
                    .p_buffer_memory_barriers = tail_rel_bufs.items.ptr,
                    .image_memory_barrier_count = @intCast(tail_rel_imgs.items.len),
                    .p_image_memory_barriers = tail_rel_imgs.items.ptr,
                });
            }

            try ctx.view.device.endCommandBuffer(cmd_buf);

            // ownership handoff: release on graphics before the transport batch is queued
            if (handoff_rel_bufs.items.len > 0 or handoff_rel_imgs.items.len > 0) {
                self.graphics_submit_lock.lockUncancelable(self.io);
                defer self.graphics_submit_lock.unlock(self.io);
                const vh = @atomicRmw(u64, &self.timeline_counter, .Add, 1, .monotonic) + 1;
                try self.submitBarriersLocked(ctx.view.device, handoff_rel_bufs.items, handoff_rel_imgs.items, vh - 1, vh, &.{});
            }

            const vt_trans = @atomicRmw(u64, &self.timeline_counter, .Add, 1, .monotonic) + 1;
            {
                var wait_infos: [1]vk.SemaphoreSubmitInfo = .{.{
                    .semaphore = self.timeline_semaphore,
                    .value = vt_trans - 1,
                    .stage_mask = .{ .all_commands_bit = true },
                    .device_index = 0,
                }};
                _ = &wait_infos;

                try ctx.view.device.queueSubmit2(self.transport_queue, (&vk.SubmitInfo2{
                    .wait_semaphore_info_count = 1,
                    .p_wait_semaphore_infos = &wait_infos,
                    .command_buffer_info_count = 1,
                    .p_command_buffer_infos = (&vk.CommandBufferSubmitInfo{
                        .command_buffer = cmd_buf,
                        .device_mask = 0,
                    })[0..1],
                    .signal_semaphore_info_count = 1,
                    .p_signal_semaphore_infos = (&vk.SemaphoreSubmitInfo{
                        .semaphore = self.timeline_semaphore,
                        .value = vt_trans,
                        .stage_mask = .{ .all_transfer_bit = true },
                        .device_index = 0,
                    })[0..1],
                })[0..1], fence);
            }

            if (has_graphics_work) {
                const vt_tail = @atomicRmw(u64, &self.timeline_counter, .Add, 1, .monotonic) + 1;
                self.graphics_submit_lock.lockUncancelable(self.io);
                defer self.graphics_submit_lock.unlock(self.io);
                try self.submitBarriersLocked(ctx.view.device, post_acq_bufs.items, post_img_bars.items, vt_trans, vt_tail, ticket_ids.items);
            }
        } else {
            for (batch.items) |*t| {
                if (!t.isLast()) continue;
                has_graphics_work = true;
                try ticket_ids.append(self.alloc, t.ticket_id);

                switch (t.dst) {
                    .buffer_dst => |dst| {
                        try tail_rel_bufs.append(self.alloc, .{
                            .src_stage_mask = .{ .all_transfer_bit = true },
                            .src_access_mask = .{ .transfer_write_bit = true },
                            .dst_stage_mask = .{},
                            .dst_access_mask = .{},
                            .src_queue_family_index = self.graphics_family,
                            .dst_queue_family_index = self.graphics_family,
                            .buffer = dst.buffer,
                            .offset = dst.initial_offset,
                            .size = t.full_src_size,
                        });
                    },
                    .image_dst => {},
                }
            }

            if (tail_rel_bufs.items.len > 0 or tail_rel_imgs.items.len > 0) {
                ctx.view.device.cmdPipelineBarrier2(cmd_buf, &.{
                    .buffer_memory_barrier_count = @intCast(tail_rel_bufs.items.len),
                    .p_buffer_memory_barriers = tail_rel_bufs.items.ptr,
                    .image_memory_barrier_count = @intCast(tail_rel_imgs.items.len),
                    .p_image_memory_barriers = tail_rel_imgs.items.ptr,
                });
            }

            try ctx.view.device.endCommandBuffer(cmd_buf);

            const tv = @atomicRmw(u64, &self.timeline_counter, .Add, 1, .monotonic) + 1;
            {
                self.pending_mutex.lockUncancelable(self.io);
                defer self.pending_mutex.unlock(self.io);

                try self.pending_submissions.append(self.alloc, .{
                    .cmd_buf = cmd_buf,
                    .ticket_ids = ticket_ids,
                    .timeline_value = tv,
                    .valid = true,
                });
            }
        }
    }
}

/// records `barriers` into the reusable graphics-side command buffer and submits
/// it; caller must hold graphics_submit_lock
fn submitBarriersLocked(
    self: *Self,
    dev: vk.DeviceProxy,
    buffer_barriers: []const vk.BufferMemoryBarrier2,
    image_barriers: []const vk.ImageMemoryBarrier2,
    wait_timeline_value: u64,
    signal_timeline_value: u64,
    ticket_ids: []const u32,
) !void {
    _ = try dev.waitForFences(&[_]vk.Fence{self.barriers_fence}, .true, std.math.maxInt(u64));
    try dev.resetFences(&[_]vk.Fence{self.barriers_fence});
    try dev.resetCommandBuffer(self.barriers_cmd_buf, .{});
    try dev.beginCommandBuffer(self.barriers_cmd_buf, &.{ .flags = .{ .one_time_submit_bit = true } });

    if (buffer_barriers.len > 0 or image_barriers.len > 0) {
        dev.cmdPipelineBarrier2(self.barriers_cmd_buf, &.{
            .buffer_memory_barrier_count = @intCast(buffer_barriers.len),
            .p_buffer_memory_barriers = buffer_barriers.ptr,
            .image_memory_barrier_count = @intCast(image_barriers.len),
            .p_image_memory_barriers = image_barriers.ptr,
        });
    }

    try dev.endCommandBuffer(self.barriers_cmd_buf);

    var waits: [1]vk.SemaphoreSubmitInfo = .{.{
        .semaphore = self.timeline_semaphore,
        .value = wait_timeline_value,
        .stage_mask = .{ .all_commands_bit = true },
        .device_index = 0,
    }};
    _ = &waits;

    try dev.queueSubmit2(self.graphics_queue, (&vk.SubmitInfo2{
        .wait_semaphore_info_count = 1,
        .p_wait_semaphore_infos = &waits,
        .command_buffer_info_count = 1,
        .p_command_buffer_infos = (&vk.CommandBufferSubmitInfo{
            .command_buffer = self.barriers_cmd_buf,
            .device_mask = 0,
        })[0..1],
        .signal_semaphore_info_count = 1,
        .p_signal_semaphore_infos = (&vk.SemaphoreSubmitInfo{
            .semaphore = self.timeline_semaphore,
            .value = signal_timeline_value,
            .stage_mask = .{ .all_commands_bit = true },
            .device_index = 0,
        })[0..1],
    })[0..1], self.barriers_fence);

    {
        self.ticket_mutex.lockUncancelable(self.io);
        defer self.ticket_mutex.unlock(self.io);
        for (ticket_ids) |tid| {
            if (tid < self.ticket_timelines.items.len) {
                self.ticket_timelines.items[tid].timeline = signal_timeline_value;
            }
        }
        self.ticket_condition.broadcast(self.io);
    }
}
