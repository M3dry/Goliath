const std = @import("std");
const zglfw = @import("zglfw");

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
