const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const base_dep = b.dependency("base", .{
        .target = target,
        .optimize = optimize,
    });
    const base_mod = base_dep.module("base");

    const zmesh_dep = b.dependency("zmesh", .{});

    const mw_dep = b.dependency("MemoryMapWriter", .{});

    const mod = b.addModule("runtime", .{
        .root_source_file = b.path("src/root.zig"),
        .imports = &.{
            .{ .name = "base", .module = base_mod },
            .{ .name = "zmesh", .module = zmesh_dep.module("root") },
            .{ .name = "MemoryMapWriter", .module = mw_dep.module("root") },
        },
        .target = target,
        .optimize = optimize,
    });

    mod.linkLibrary(zmesh_dep.artifact("zmesh"));

    const check_obj = b.addObject(.{
        .name = "check",
        .root_module = mod,
    });

    const check = b.step("check", "Check compilation");
    check.dependOn(&check_obj.step);

    const mod_tests = b.addTest(.{
        .root_module = mod,
    });

    b.installArtifact(mod_tests);
    const run_mod_tests = b.addRunArtifact(mod_tests);

    const test_step = b.step("test", "Run base tests");
    test_step.dependOn(&run_mod_tests.step);
}
