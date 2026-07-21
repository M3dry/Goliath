const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const base_dep = b.dependency("base", .{
        .target = target,
        .optimize = optimize,
        .shader_src = b.path("shaders"),
        .shader_mod_name = "shaders",
    });
    const shaders_mod = base_dep.module("shaders");

    const runtime_mod = b.dependency("runtime", .{
        .target = target,
        .optimize = optimize,
    }).module("runtime");

    const exe_opts: std.Build.ExecutableOptions = .{
        .name = "Demo",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "shaders", .module = shaders_mod },
                .{ .name = "runtime", .module = runtime_mod },
            },
        }),
    };

    const exe = b.addExecutable(exe_opts);
    b.installArtifact(exe);

    const exe_check = b.addExecutable(exe_opts);
    const check = b.step("check", "Check compilation");
    check.dependOn(&exe_check.step);

    const run_step = b.step("run", "Run the app");
    const run_cmd = b.addRunArtifact(exe);
    run_step.dependOn(&run_cmd.step);
    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
}
