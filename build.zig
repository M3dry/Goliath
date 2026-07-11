const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const base_dep = b.dependency("base", .{
        .target = target,
        .optimize = optimize,
    });

    const base = base_dep.module("base");
    const base_tests = base_dep.artifact("test");

    const exe_opts: std.Build.ExecutableOptions = .{
        .name = "Demo",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "base", .module = base },
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

    const exe_tests = b.addTest(.{
        .root_module = exe.root_module,
    });
    const run_exe_tests = b.addRunArtifact(exe_tests);

    const run_base_tests = b.addRunArtifact(base_tests);

    const test_step = b.step("test_all", "Run tests");
    test_step.dependOn(&run_exe_tests.step);
    test_step.dependOn(&run_base_tests.step);
}
