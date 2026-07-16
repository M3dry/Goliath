const std = @import("std");
const vk = @import("vulkan");
const vma = @import("vma.zig").vma;
const GraphicsCtx = @import("graphics_ctx.zig").GraphicsCtx;
const Buffer = @import("buffer.zig").Buffer;
const Allocator = std.mem.Allocator;

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

pub const FreeFn = *const fn (ptr: *anyopaque) void;

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
        initial_base_array_layer: u32,
        offset: vk.Offset3D,
        extent: vk.Extent3D,
        src_row_length: u32,
        format: vk.Format,
        new_layout: vk.ImageLayout,
    },
};


const TaskNode = struct {
    node: std.DoublyLinkedList.Node = .{},
    data: Task,
};

const Task = struct {
    dst: TaskDst,
    src: [*]const u8,
    src_offset: u32,
    full_src_size: u32,
    ticket_id: u32,
    owning: ?FreeFn,
    last: bool,
    dst_stage: vk.PipelineStageFlags2,
    dst_access: vk.AccessFlags2,

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
        if (self.owning) |free_fn| {
            free_fn(@ptrCast(@constCast(self.src)));
        }
    }

    fn split(self: *Task, budget: u32, rest: *std.ArrayListUnmanaged(Task), transport: *Transport) !bool {
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
                    .ticket_id = self.ticket_id,
                    .owning = self.owning,
                    .last = self.last,
                    .dst_stage = self.dst_stage,
                    .dst_access = self.dst_access,
                };
                try rest.append(transport.alloc, remainder);

                dst.src_size = budget;
                self.ticket_id = (try transport.getFreeTicket()).id();
                self.owning = null;
                self.last = false;

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

                const sub1 = Task{
                    .dst = .{ .image_dst = .{
                        .image = dst.image,
                        .subresource = dst.subresource,
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
                    .ticket_id = (try transport.getFreeTicket()).id(),
                    .owning = null,
                    .last = false,
                    .dst_stage = self.dst_stage,
                    .dst_access = self.dst_access,
                };
                const sub2 = Task{
                    .dst = .{ .image_dst = .{
                        .image = dst.image,
                        .subresource = dst.subresource,
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
                    .ticket_id = self.ticket_id,
                    .owning = self.owning,
                    .last = self.last,
                    .dst_stage = self.dst_stage,
                    .dst_access = self.dst_access,
                };

                self.ticket_id = (try transport.getFreeTicket()).id();
                self.owning = null;
                self.last = false;
                dst.extent.width = w;
                dst.extent.height = h;

                if ((h1 == 0 or w1 == 0) and (h2 == 0 or w2 == 0)) return true;

                if (h1 == 0 or w1 == 0) {
                    try rest.append(transport.alloc, sub2);
                } else if (h2 == 0 or w2 == 0) {
                    try rest.append(transport.alloc, sub1);
                } else {
                    try rest.append(transport.alloc, sub1);
                    try rest.append(transport.alloc, sub2);
                }

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

    fn recordReleaseBarrier(self: *const Task, state: *Transport) !void {
        switch (self.dst) {
            .buffer_dst => |dst| {
                try state.transport_buffer_barriers.append(state.alloc, .{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = .{},
                    .dst_access_mask = .{},
                    .src_queue_family_index = state.transport_family,
                    .dst_queue_family_index = state.graphics_family,
                    .buffer = dst.buffer,
                    .offset = dst.initial_offset,
                    .size = dst.initial_offset + self.full_src_size,
                });
            },
            .image_dst => |dst| {
                try state.transport_image_barriers.append(state.alloc, .{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = .{},
                    .dst_access_mask = .{},
                    .old_layout = .transfer_dst_optimal,
                    .new_layout = dst.new_layout,
                    .src_queue_family_index = state.transport_family,
                    .dst_queue_family_index = state.graphics_family,
                    .image = dst.image,
                    .subresource_range = .{
                        .aspect_mask = dst.subresource.aspect_mask,
                        .base_mip_level = dst.subresource.mip_level,
                        .level_count = 1,
                        .base_array_layer = dst.subresource.base_array_layer,
                        .layer_count = 1,
                    },
                });
            },
        }
    }

    fn recordSameQueueBarrier(self: *const Task, state: *Transport) !void {
        switch (self.dst) {
            .buffer_dst => |dst| {
                try state.transport_buffer_barriers.append(state.alloc, .{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = self.dst_stage,
                    .dst_access_mask = self.dst_access,
                    .src_queue_family_index = state.graphics_family,
                    .dst_queue_family_index = state.graphics_family,
                    .buffer = dst.buffer,
                    .offset = dst.initial_offset,
                    .size = dst.initial_offset + self.full_src_size,
                });
            },
            .image_dst => |dst| {
                try state.transport_image_barriers.append(state.alloc, .{
                    .src_stage_mask = .{ .all_transfer_bit = true },
                    .src_access_mask = .{ .transfer_write_bit = true },
                    .dst_stage_mask = self.dst_stage,
                    .dst_access_mask = self.dst_access,
                    .old_layout = .transfer_dst_optimal,
                    .new_layout = dst.new_layout,
                    .src_queue_family_index = state.graphics_family,
                    .dst_queue_family_index = state.graphics_family,
                    .image = dst.image,
                    .subresource_range = .{
                        .aspect_mask = dst.subresource.aspect_mask,
                        .base_mip_level = dst.subresource.mip_level,
                        .level_count = 1,
                        .base_array_layer = dst.subresource.base_array_layer,
                        .layer_count = 1,
                    },
                });
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

pub const Transport = struct {
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

    staging_buffers: [num_frames]Buffer,
    staging_ptrs: [num_frames][*]u8,
    flush_staging: bool,

    cmd_pool: vk.CommandPool,
    current_cmd_buf: u32,
    cmd_bufs: [num_frames]vk.CommandBuffer,
    cmd_buf_fences: [num_frames]vk.Fence,

    transport_graphics_semaphores: [num_frames]vk.Semaphore,
    timeline_semaphore: vk.Semaphore,
    timeline_counter: u64,
    finished_timeline: u64,

    current_task_queue: u32,
    task_queues: [num_frames]std.DoublyLinkedList,
    task_queue_lock: std.Io.Mutex,

    transport_buffer_barriers: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2),
    transport_image_barriers: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2),
    barrier_lock: std.Io.Mutex,
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

    pub fn init(self: *Transport, gc: *const GraphicsCtx, alloc: Allocator, io: std.Io) !void {
        self.alloc = alloc;
        self.has_dedicated_transport = gc.has_dedicated_transport;
        self.dev = gc.dev;
        self.transport_queue = gc.transport_queue;
        self.graphics_queue = gc.graphics_queue;
        self.vma_alloc = gc.vma_alloc;
        self.transport_family = gc.transport_family;
        self.graphics_family = gc.graphics_family;
        self.current_cmd_buf = 0;
        self.timeline_counter = 0;
        self.finished_timeline = 0;
        self.current_task_queue = 0;
        self.stop_worker = false;
        self.worker = null;
        self.flush_staging = false;
        self.io = io;
        self.task_queue_lock = std.Io.Mutex.init;
        self.barrier_lock = std.Io.Mutex.init;
        self.full_upload_lock = std.Io.Mutex.init;
        self.ticket_mutex = std.Io.Mutex.init;
        self.pending_mutex = std.Io.Mutex.init;
        self.ticket_timelines = .empty;
        self.free_tickets = .empty;
        self.pending_submissions = .empty;
        self.transport_buffer_barriers = .empty;
        self.transport_image_barriers = .empty;
        for (&self.task_queues) |*q| q.* = .{};

        var staging_created: u32 = 0;
        errdefer for (self.staging_buffers[0..staging_created]) |*buf| buf.deinitNow(gc.vma_alloc);
        for (0..num_frames) |i| {
            self.staging_buffers[i] = try Buffer.init(gc, .transport, "Transport staging", staging_buffer_size, .{ .transfer_src_bit = true }, true);
            staging_created += 1;
            self.staging_ptrs[i] = self.staging_buffers[i].mapped orelse @panic("staging buffer not mapped");
        }
        self.flush_staging = !self.staging_buffers[0].coherent;

        self.cmd_pool = try gc.dev.createCommandPool(&.{
            .flags = .{ .reset_command_buffer_bit = true },
            .queue_family_index = gc.transport_family,
        }, null);
        errdefer gc.dev.destroyCommandPool(self.cmd_pool, null);
        var cmd_bufs: [num_frames]vk.CommandBuffer = undefined;
        try gc.dev.allocateCommandBuffers(&.{
            .command_pool = self.cmd_pool,
            .level = .primary,
            .command_buffer_count = num_frames,
        }, &cmd_bufs);
        self.cmd_bufs = cmd_bufs;

        var fences_created: u32 = 0;
        errdefer for (self.cmd_buf_fences[0..fences_created]) |f| gc.dev.destroyFence(f, null);
        for (&self.cmd_buf_fences) |*fence| {
            fence.* = try gc.dev.createFence(&.{
                .flags = .{ .signaled_bit = true },
            }, null);
            fences_created += 1;
        }
        var sem_created: u32 = 0;
        errdefer for (self.transport_graphics_semaphores[0..sem_created]) |s| gc.dev.destroySemaphore(s, null);
        for (&self.transport_graphics_semaphores) |*sem| {
            sem.* = try gc.dev.createSemaphore(&.{}, null);
            sem_created += 1;
        }

        self.timeline_semaphore = try gc.dev.createSemaphore(&.{
            .p_next = &vk.SemaphoreTypeCreateInfo{
                .initial_value = 0,
                .semaphore_type = .timeline,
            },
        }, null);
        errdefer gc.dev.destroySemaphore(self.timeline_semaphore, null);

        self.drain_cmd_pool = try gc.dev.createCommandPool(&.{
            .flags = .{ .reset_command_buffer_bit = true },
            .queue_family_index = gc.graphics_family,
        }, null);
        errdefer gc.dev.destroyCommandPool(self.drain_cmd_pool, null);
        var drain_cmd_buf: vk.CommandBuffer = undefined;
        try gc.dev.allocateCommandBuffers(&.{
            .command_pool = self.drain_cmd_pool,
            .level = .primary,
            .command_buffer_count = 1,
        }, (&drain_cmd_buf)[0..1]);
        self.drain_cmd_buf = drain_cmd_buf;

        self.drain_fence = try gc.dev.createFence(&.{
            .flags = .{ .signaled_bit = true },
        }, null);

        self.worker = try std.Thread.spawn(.{}, workerThread, .{ self, gc });
    }

    pub fn deinit(self: *Transport, gc: *const GraphicsCtx) void {
        @atomicStore(bool, &self.stop_worker, true, .monotonic);
        if (self.worker) |w| w.join();

        for (&self.staging_buffers) |*buf| buf.deinitNow(gc.vma_alloc);

        gc.dev.destroyCommandPool(self.cmd_pool, null);
        gc.dev.destroyCommandPool(self.drain_cmd_pool, null);

        gc.dev.destroyFence(self.drain_fence, null);
        for (self.cmd_buf_fences) |f| gc.dev.destroyFence(f, null);

        for (self.transport_graphics_semaphores) |s| gc.dev.destroySemaphore(s, null);
        gc.dev.destroySemaphore(self.timeline_semaphore, null);

        for (&self.task_queues) |*q| {
            while (q.popFirst()) |node| {
                const tn: *TaskNode = @fieldParentPtr("node", node);
                self.alloc.destroy(tn);
            }
        }

        self.transport_buffer_barriers.deinit(self.alloc);
        self.transport_image_barriers.deinit(self.alloc);
        self.ticket_timelines.deinit(self.alloc);
        self.free_tickets.deinit(self.alloc);

        for (self.pending_submissions.items) |*ps| {
            ps.buffer_barriers.deinit(self.alloc);
            ps.image_barriers.deinit(self.alloc);
            ps.ticket_ids.deinit(self.alloc);
        }

        self.pending_submissions.deinit(self.alloc);
    }

    pub fn uploadBuffer(
        self: *Transport,
        priority: bool,
        src: []const u8,
        owning: ?FreeFn,
        dst: vk.Buffer,
        dst_offset: u32,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
    ) !Ticket {
        const ticket = try self.getFreeTicket();
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
            .ticket_id = ticket.id(),
            .owning = owning,
            .last = true,
            .dst_stage = dst_stage,
            .dst_access = dst_access,
        };
        self.task_queue_lock.lockUncancelable(self.io);
        defer self.task_queue_lock.unlock(self.io);
        const q = &self.task_queues[self.current_task_queue];
        const node = try self.alloc.create(TaskNode);
        node.* = .{ .data = task };
        if (priority) q.prepend(&node.node) else q.append(&node.node);
        return ticket;
    }

    pub fn uploadImage(
        self: *Transport,
        priority: bool,
        format: vk.Format,
        dimension: vk.Extent3D,
        src: []const u8,
        owning: ?FreeFn,
        dst: vk.Image,
        dst_layers: vk.ImageSubresourceLayers,
        dst_offset: vk.Offset3D,
        current_layout: vk.ImageLayout,
        new_layout: vk.ImageLayout,
        dst_stage: vk.PipelineStageFlags2,
        dst_access: vk.AccessFlags2,
    ) !Ticket {
        const ticket = try self.getFreeTicket();

        self.full_upload_lock.lockUncancelable(self.io);
        defer self.full_upload_lock.unlock(self.io);

        try self.transport_image_barriers.append(self.alloc, .{
            .src_stage_mask = .{},
            .src_access_mask = .{},
            .dst_stage_mask = .{ .all_transfer_bit = true },
            .dst_access_mask = .{ .transfer_write_bit = true },
            .old_layout = current_layout,
            .new_layout = .transfer_dst_optimal,
            .src_queue_family_index = self.transport_family,
            .dst_queue_family_index = self.transport_family,
            .image = dst,
            .subresource_range = .{
                .aspect_mask = dst_layers.aspect_mask,
                .base_mip_level = dst_layers.mip_level,
                .level_count = 1,
                .base_array_layer = dst_layers.base_array_layer,
                .layer_count = dst_layers.layer_count,
            },
        });

        const info = try getFormatInfo(format);
        const layer_size = dimension.width * dimension.height * dimension.depth * info.bytes_per_block;
        const num_layers = dst_layers.layer_count;
        const total_size = layer_size * num_layers;

        self.task_queue_lock.lockUncancelable(self.io);
        defer self.task_queue_lock.unlock(self.io);
        const q = &self.task_queues[self.current_task_queue];

        var nodes: [128]*TaskNode = undefined;
        const count = @min(@as(usize, num_layers), nodes.len);

        for (0..count) |i| {
            const is_last = i == count - 1;
            const node = try self.alloc.create(TaskNode);
            node.* = .{ .data = .{
                .dst = .{ .image_dst = .{
                    .image = dst,
                    .subresource = .{
                        .aspect_mask = dst_layers.aspect_mask,
                        .mip_level = dst_layers.mip_level,
                        .base_array_layer = dst_layers.base_array_layer + @as(u32, @intCast(i)),
                        .layer_count = 1,
                    },
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
                .ticket_id = ticket.id(),
                .owning = if (is_last) owning else null,
                .last = is_last,
                .dst_stage = dst_stage,
                .dst_access = dst_access,
            } };
            nodes[i] = node;
        }

        if (priority) {
            var i: usize = count;
            while (i > 0) {
                i -= 1;
                q.prepend(&nodes[i].node);
            }
        } else {
            for (0..count) |i| q.append(&nodes[i].node);
        }

        return ticket;
    }

    pub fn isReady(self: *Transport, t: Ticket) !bool {
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

    pub fn waitOn(self: *Transport, tickets: []const Ticket) vk.SemaphoreSubmitInfo {
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

    pub fn unqueue(self: *Transport, t: Ticket, free_src: bool) void {
        const check_queues = struct {
            fn find(tr: *Transport, q: *std.DoublyLinkedList, ticket: Ticket, free_it: bool) bool {
                var it = q.first;
                while (it) |node| {
                    const tn: *TaskNode = @fieldParentPtr("node", node);
                    if (tn.data.ticket_id == ticket.id()) {
                        const gen = blk: {
                            tr.ticket_mutex.lockUncancelable(tr.io);
                            defer tr.ticket_mutex.unlock(tr.io);
                            if (ticket.id() < tr.ticket_timelines.items.len) break :blk tr.ticket_timelines.items[ticket.id()].generation;
                            break :blk 0;
                        };
                        if (gen == ticket.gen()) {
                            q.remove(node);
                            if (free_it and tn.data.owning) |fn_| fn_(@ptrCast(tn.data.src));
                            tr.alloc.destroy(tn);
                        }
                        return true;
                    }
                    it = node.next;
                }
                return false;
            }
        }.find;

        self.full_upload_lock.lockUncancelable(self.io);
        defer self.full_upload_lock.unlock(self.io);
        self.task_queue_lock.lockUncancelable(self.io);
        defer self.task_queue_lock.unlock(self.io);
        _ = check_queues(self, &self.task_queues[self.current_task_queue], t, free_src);
        const other = (self.current_task_queue + 1) % num_frames;
        _ = check_queues(self, &self.task_queues[other], t, free_src);
    }

    pub fn drain(self: *Transport, gc: *const GraphicsCtx) !void {
        self.pending_mutex.lockUncancelable(self.io);
        defer self.pending_mutex.unlock(self.io);

        for (self.pending_submissions.items) |*sub| {
            if (!sub.valid) continue;

            _ = try gc.dev.waitForFences(&[_]vk.Fence{self.drain_fence}, .true, std.math.maxInt(u64));
            try gc.dev.resetFences(&[_]vk.Fence{self.drain_fence});

            try gc.dev.resetCommandBuffer(self.drain_cmd_buf, .{});
            try gc.dev.beginCommandBuffer(self.drain_cmd_buf, &.{ .flags = .{ .one_time_submit_bit = true } });

            if (sub.buffer_barriers.items.len > 0 or sub.image_barriers.items.len > 0) {
                gc.dev.cmdPipelineBarrier2(self.drain_cmd_buf, &.{
                    .buffer_memory_barrier_count = @intCast(sub.buffer_barriers.items.len),
                    .p_buffer_memory_barriers = sub.buffer_barriers.items.ptr,
                    .image_memory_barrier_count = @intCast(sub.image_barriers.items.len),
                    .p_image_memory_barriers = sub.image_barriers.items.ptr,
                });
            }

            try gc.dev.endCommandBuffer(self.drain_cmd_buf);

            if (self.has_dedicated_transport) {
                try gc.dev.queueSubmit2(gc.graphics_queue, (&vk.SubmitInfo2{
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
                try gc.dev.queueSubmit2(gc.graphics_queue, (&vk.SubmitInfo2{
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

    pub fn getTimeline(self: *const Transport) u64 {
        var v: u64 = undefined;
        self.dev.getSemaphoreCounterValue(self.timeline_semaphore, &v);
        return v;
    }

    fn isTimelineReady(self: *Transport, timeline: u64) !bool {
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

    fn getFreeTicket(self: *Transport) !Ticket {
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
};

fn workerThread(self: *Transport, gc: *const GraphicsCtx) !void {
    var cmd_buf_idx: u32 = 0;

    while (!@atomicLoad(bool, &self.stop_worker, .monotonic)) {
        self.full_upload_lock.lockUncancelable(self.io);
        defer self.full_upload_lock.unlock(self.io);

        self.task_queue_lock.lockUncancelable(self.io);
        const process_idx = self.current_task_queue;
        self.current_task_queue = (self.current_task_queue + 1) % Transport.num_frames;
        self.task_queue_lock.unlock(self.io);

        var queue = &self.task_queues[process_idx];
        if (queue.first == null) {
            std.Thread.yield() catch {};
            continue;
        }

        const slot = cmd_buf_idx;
        cmd_buf_idx = (cmd_buf_idx + 1) % Transport.num_frames;

        const cmd_buf = self.cmd_bufs[slot];
        const fence = self.cmd_buf_fences[slot];
        const staging_buf = self.staging_buffers[slot];
        const staging_ptr = self.staging_ptrs[slot];
        const binary_sem = self.transport_graphics_semaphores[slot];

        _ = try gc.dev.waitForFences(&[_]vk.Fence{fence}, .true, std.math.maxInt(u64));
        try gc.dev.resetFences(&[_]vk.Fence{fence});

        try gc.dev.resetCommandBuffer(cmd_buf, .{});
        try gc.dev.beginCommandBuffer(cmd_buf, &.{ .flags = .{ .one_time_submit_bit = true } });

        {
            self.barrier_lock.lockUncancelable(self.io);
            defer self.barrier_lock.unlock(self.io);

            if (self.transport_buffer_barriers.items.len > 0 or self.transport_image_barriers.items.len > 0) {
                gc.dev.cmdPipelineBarrier2(cmd_buf, &.{
                    .buffer_memory_barrier_count = @intCast(self.transport_buffer_barriers.items.len),
                    .p_buffer_memory_barriers = self.transport_buffer_barriers.items.ptr,
                    .image_memory_barrier_count = @intCast(self.transport_image_barriers.items.len),
                    .p_image_memory_barriers = self.transport_image_barriers.items.ptr,
                });
                self.transport_buffer_barriers.clearRetainingCapacity();
                self.transport_image_barriers.clearRetainingCapacity();
            }
        }

        var size: u32 = 0;
        var batch: std.ArrayListUnmanaged(Task) = .empty;
        defer batch.deinit(self.alloc);

        while (queue.first) |first| {
            const budget = Transport.staging_buffer_size - size;
            if (budget == 0) break;

            const task_node: *TaskNode = @fieldParentPtr("node", first);
            var task = task_node.data;

            var rest: std.ArrayListUnmanaged(Task) = .empty;
            defer rest.deinit(self.alloc);

            if (budget < try task.requiredSize()) {
                if (try task.split(budget, &rest, self)) break;

                queue.remove(first);
                self.alloc.destroy(task_node);

                var i: usize = rest.items.len;
                while (i > 0) {
                    i -= 1;
                    const n = try self.alloc.create(TaskNode);
                    n.* = .{ .data = rest.items[i] };
                    queue.prepend(&n.node);
                }
            } else {
                queue.remove(first);
                self.alloc.destroy(task_node);
            }

            try task.uploadToStaging(staging_ptr + size);
            size += try task.requiredSize();
            try batch.append(self.alloc, task);
        }

        if (self.flush_staging) staging_buf.flush(self.vma_alloc, 0, Transport.staging_buffer_size);

        var copy_offset: u32 = 0;
        for (batch.items) |*t| {
            t.recordCopy(cmd_buf, gc.dev, staging_buf.handle, copy_offset);
            copy_offset += try t.requiredSize();
        }

        var ticket_ids: std.ArrayListUnmanaged(u32) = .empty;

        if (self.has_dedicated_transport) {
            var buf_bars: std.ArrayListUnmanaged(vk.BufferMemoryBarrier2) = .empty;
            var img_bars: std.ArrayListUnmanaged(vk.ImageMemoryBarrier2) = .empty;
            var has_graphics_work = false;

            for (batch.items) |*t| {
                if (!t.last) continue;
                has_graphics_work = true;
                try t.recordReleaseBarrier(self);
                try ticket_ids.append(self.alloc, t.ticket_id);

                switch (t.dst) {
                    .buffer_dst => |dst| {
                        try buf_bars.append(self.alloc, .{
                            .src_stage_mask = .{},
                            .src_access_mask = .{},
                            .dst_stage_mask = t.dst_stage,
                            .dst_access_mask = t.dst_access,
                            .src_queue_family_index = self.transport_family,
                            .dst_queue_family_index = self.graphics_family,
                            .buffer = dst.buffer,
                            .offset = dst.initial_offset,
                            .size = dst.initial_offset + t.full_src_size,
                        });
                    },
                    .image_dst => |dst| {
                        try img_bars.append(self.alloc, .{
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
                    },
                }
            }

            {
                self.barrier_lock.lockUncancelable(self.io);
                defer self.barrier_lock.unlock(self.io);
                if (self.transport_buffer_barriers.items.len > 0 or self.transport_image_barriers.items.len > 0) {
                    gc.dev.cmdPipelineBarrier2(cmd_buf, &.{
                        .buffer_memory_barrier_count = @intCast(self.transport_buffer_barriers.items.len),
                        .p_buffer_memory_barriers = self.transport_buffer_barriers.items.ptr,
                        .image_memory_barrier_count = @intCast(self.transport_image_barriers.items.len),
                        .p_image_memory_barriers = self.transport_image_barriers.items.ptr,
                    });
                    self.transport_buffer_barriers.clearRetainingCapacity();
                    self.transport_image_barriers.clearRetainingCapacity();
                }
            }

            try gc.dev.endCommandBuffer(cmd_buf);

            try gc.dev.queueSubmit2(self.transport_queue, (&vk.SubmitInfo2{
                .command_buffer_info_count = 1,
                .p_command_buffer_infos = (&vk.CommandBufferSubmitInfo{
                    .command_buffer = cmd_buf,
                    .device_mask = 0,
                })[0..1],
                .signal_semaphore_info_count = 1,
                .p_signal_semaphore_infos = (&vk.SemaphoreSubmitInfo{
                    .semaphore = binary_sem,
                    .value = 0,
                    .stage_mask = .{ .all_transfer_bit = true },
                    .device_index = 0,
                })[0..1],
            })[0..1], fence);

            if (has_graphics_work) {
                const tv = @atomicRmw(u64, &self.timeline_counter, .Add, 1, .monotonic) + 1;
                self.pending_mutex.lockUncancelable(self.io);
                defer self.pending_mutex.unlock(self.io);

                try self.pending_submissions.append(self.alloc, .{
                    .wait_semaphore = binary_sem,
                    .buffer_barriers = buf_bars,
                    .image_barriers = img_bars,
                    .ticket_ids = ticket_ids,
                    .timeline_value = tv,
                    .valid = true,
                });
            } else {
                buf_bars.deinit(self.alloc);
                img_bars.deinit(self.alloc);
                ticket_ids.deinit(self.alloc);
            }
        } else {
            for (batch.items) |*t| {
                if (!t.last) continue;
                try t.recordSameQueueBarrier(self);
                try ticket_ids.append(self.alloc, t.ticket_id);
            }

            {
                self.barrier_lock.lockUncancelable(self.io);
                defer self.barrier_lock.unlock(self.io);
                if (self.transport_buffer_barriers.items.len > 0 or self.transport_image_barriers.items.len > 0) {
                    gc.dev.cmdPipelineBarrier2(cmd_buf, &.{
                        .buffer_memory_barrier_count = @intCast(self.transport_buffer_barriers.items.len),
                        .p_buffer_memory_barriers = self.transport_buffer_barriers.items.ptr,
                        .image_memory_barrier_count = @intCast(self.transport_image_barriers.items.len),
                        .p_image_memory_barriers = self.transport_image_barriers.items.ptr,
                    });
                    self.transport_buffer_barriers.clearRetainingCapacity();
                    self.transport_image_barriers.clearRetainingCapacity();
                }
            }

            try gc.dev.endCommandBuffer(cmd_buf);

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
