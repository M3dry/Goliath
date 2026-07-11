const std = @import("std");
const base = @import("base");

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    var ctx = try base.Ctx.init(gpa, "Demo", .{
        .resizable = false,
        .size = .fullscreen,
    });
    defer ctx.deinit(gpa);

    while (!ctx.window.shouldClose()) {
        base.zglfw.pollEvents();
    }
}
