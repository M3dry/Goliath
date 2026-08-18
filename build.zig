const std = @import("std");
const build_shaders = @import("build_shaders.zig");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const shaders = build_shaders.shadersStep(b, "shaders");
    b.getInstallStep().dependOn(shaders.step);

    const rebuild_assets = b.option(bool, "asset_system_rebuild", "Rebuild the asset system manifest from the model textures (wipes testing_asset_system)") orelse false;
    const build_options = b.addOptions();
    build_options.addOption(bool, "asset_system_rebuild", rebuild_assets);
    const build_options_mod = build_options.createModule();

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
                .{ .name = "shaders", .module = shaders.mod },
                .{ .name = "runtime", .module = runtime_mod },
                .{ .name = "build_options", .module = build_options_mod },
            },
        }),
    };

    const exe = b.addExecutable(exe_opts);
    exe.use_llvm = true;
    b.installArtifact(exe);

    const exe_check = b.addExecutable(exe_opts);
    const check = b.step("check", "Check compilation");
    check.dependOn(&exe_check.step);

    const run_step = b.step("run", "Run the app");
    const run_cmd = b.addRunArtifact(exe);
    run_step.dependOn(&run_cmd.step);
    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| run_cmd.addArgs(args);

    const test_step = b.step("test", "Run tests");
    const exe_tests = b.addTest(.{ .root_module = exe.root_module });
    test_step.dependOn(&b.addRunArtifact(exe_tests).step);
}
