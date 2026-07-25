const std = @import("std");

pub const Shaders = struct {
    step: *std.Build.Step,
    mod: *std.Build.Module,
};

pub fn shadersStep(b: *std.Build, shader_dir: []const u8) Shaders {
    const exe = b.dependency("slang_build", .{}).artifact("slang_build");
    const cmd = b.addRunArtifact(exe);

    cmd.addArg("--out");
    const out_lp = cmd.addOutputFileArg(b.pathJoin(&.{ shader_dir, "shaders.zig" }));
    cmd.addArg("--src");

    const dir_path = b.path(shader_dir);
    const resolved = resolveDirPath(b, dir_path) catch @panic("failed to resolve shader dir");
    cmd.addArg(resolved);

    const io = b.graph.io;
    var dir = std.Io.Dir.openDirAbsolute(io, resolved, .{ .iterate = true }) catch @panic("failed to open shader dir");
    defer dir.close(io);

    var walker = dir.walk(b.allocator) catch @panic("OOM");
    defer walker.deinit();

    while (walker.next(io) catch null) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, entry.path, ".slang")) continue;
        const is_lib = std.mem.startsWith(u8, entry.path, "library/");
        const input_lp = switch (dir_path) {
            .src_path => |sp| std.Build.LazyPath{ .src_path = .{
                .owner = sp.owner,
                .sub_path = b.pathJoin(&.{ sp.sub_path, entry.path }),
            } },
            else => continue,
        };
        if (is_lib) cmd.addArg("--lib-src");
        cmd.addFileArg(input_lp);
    }

    const mod = b.addModule("shaders", .{
        .root_source_file = out_lp,
    });

    const step = b.step("shaders", "Compile Slang shaders");
    step.dependOn(&cmd.step);

    return .{ .step = step, .mod = mod };
}

pub fn resolveDirPath(b: *std.Build, lp: std.Build.LazyPath) ![]const u8 {
    return switch (lp) {
        .src_path => |sp| b.pathJoin(&.{
            sp.owner.build_root.path orelse return error.PathUnavailable,
            sp.sub_path,
        }),
        .cwd_relative => |p| b.dupe(p),
        else => error.PathUnavailable,
    };
}
