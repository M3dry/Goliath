const std = @import("std");
const vk = @import("vulkan");
const zglfw = @import("zglfw");

const Ctx = @import("root.zig").Ctx;
const GraphicsCtx = @import("GraphicsCtx.zig");

pub const FrameTimer = struct {
    fixed_dt: f64,
    accumulator: f64,
    frame_dt: f64,
    alpha: f64,
    previous_time: f64,

    pub fn init(fixed_dt: f64) FrameTimer {
        return .{
            .fixed_dt = fixed_dt,
            .accumulator = 0.0,
            .frame_dt = 0.0,
            .alpha = 0.0,
            .previous_time = zglfw.getTime(),
        };
    }

    pub fn tick(self: *FrameTimer) void {
        const current_time = zglfw.getTime();
        self.frame_dt = current_time - self.previous_time;
        self.previous_time = current_time;

        if (self.frame_dt > 0.1) self.frame_dt = 0.1;

        self.accumulator += self.frame_dt;
    }

    pub fn shouldUpdate(self: *const FrameTimer) bool {
        return self.accumulator >= self.fixed_dt;
    }

    pub fn consume(self: *FrameTimer) void {
        self.accumulator -= self.fixed_dt;
    }

    pub fn computeAlpha(self: *const FrameTimer) f64 {
        return if (self.fixed_dt > 0.0) self.accumulator / self.fixed_dt else 0.0;
    }
};

pub const GpuTimer = struct {
    pool: vk.QueryPool,
    pass_count: u32,
    current_frame: u32,
    deltas: []u64,
    raw: []u64,

    const slots_per_pass = 2;

    pub fn init(
        gc: *const GraphicsCtx,
        pass_count: u32,
        alloc: std.mem.Allocator,
    ) !GpuTimer {
        const total_slots = pass_count * slots_per_pass * Ctx.frames_in_flight;
        const pool = try gc.dev.createQueryPool(&.{
            .query_type = .timestamp,
            .query_count = total_slots,
        }, null);
        const deltas = try alloc.alloc(u64, pass_count);
        const raw = try alloc.alloc(u64, pass_count * 2);

        return .{
            .pool = pool,
            .pass_count = pass_count,
            .current_frame = 0,
            .deltas = deltas,
            .raw = raw,
        };
    }

    pub fn deinit(self: *GpuTimer, gc: *const GraphicsCtx, alloc: std.mem.Allocator) void {
        gc.dev.destroyQueryPool(self.pool, null);

        alloc.free(self.deltas);
        alloc.free(self.raw);
    }

    pub fn collect(self: *GpuTimer, gc: *const GraphicsCtx) ?[]const u64 {
        if (self.current_frame < Ctx.frames_in_flight) return null;

        const base_slot = self.current_frame * self.pass_count * slots_per_pass;
        const count = self.pass_count * slots_per_pass;
        gc.dev.getQueryPoolResults(
            self.pool,
            base_slot,
            count,
            self.raw.len * @sizeOf(u64),
            self.raw.ptr,
            @sizeOf(u64),
            .{ .@"64_bit" = true, .wait_bit = true },
        ) catch return null;

        for (0..self.pass_count) |i| {
            self.deltas[i] = self.raw[i * 2 + 1] - self.raw[i * 2];
        }

        return self.deltas[0..self.pass_count];
    }

    pub fn reset(self: *GpuTimer, gc: *const GraphicsCtx, cmd_buf: vk.CommandBuffer) void {
        const base_slot = self.current_frame * self.pass_count * slots_per_pass;

        gc.dev.cmdResetQueryPool(cmd_buf, self.pool, base_slot, self.pass_count * slots_per_pass);
    }

    pub fn slot(self: *const GpuTimer, pass_index: u32) u32 {
        return self.current_frame * self.pass_count * slots_per_pass + pass_index * slots_per_pass;
    }

    pub fn endFrame(self: *GpuTimer) void {
        self.current_frame = (self.current_frame + 1) % Ctx.frames_in_flight;
    }
};
