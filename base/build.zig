const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const vk_headers_dep = b.dependency("vulkan_headers", .{});

    const vulkan_mod = b.dependency("vulkan", .{
        .registry = vk_headers_dep.path("registry/vk.xml"),
        .optimize = optimize,
        .target = target,
    }).module("vulkan-zig");

    const zglfw_dep = b.dependency("zglfw", .{
        .target = target,
        .optimize = optimize,
        .import_vulkan = true,
    });
    const zglfw_mod = zglfw_dep.module("root");
    zglfw_mod.addImport("vulkan", vulkan_mod);

    const zglfw_lib = zglfw_dep.artifact("glfw");

    const vma_dep = b.dependency("vma", .{});
    const vma_include_path = vma_dep.path("include");

    const write_files = b.addWriteFiles();
    const vma_c_file = write_files.add("vma.cpp",
        \\#define VMA_IMPLEMENTATION
        \\#define VMA_STATIC_VULKAN_FUNCTIONS 0
        \\#include <vk_mem_alloc.h>
    );

    const vma_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });

    vma_mod.addIncludePath(vk_headers_dep.path("include"));
    vma_mod.addIncludePath(vma_include_path);
    vma_mod.addCSourceFile(.{
        .file = vma_c_file,
        .flags = &.{"-std=c++17"},
    });

    const vma_lib = b.addLibrary(.{
        .name = "vma",
        .root_module = vma_mod,
    });

    const zgui = b.dependency("zgui", .{
        .shared = false,
        .with_implot = true,
        .backend = .glfw_vulkan,
        .vulkan_include = "",
    });
    const zgui_imgui = zgui.artifact("imgui");
    zgui_imgui.root_module.addSystemIncludePath(vk_headers_dep.path("include"));
    zgui_imgui.root_module.addCMacro("GLFW_INCLUDE_NONE", "");

    const mod = b.addModule("base", .{
        .root_source_file = b.path("src/root.zig"),
        .imports = &.{
            .{ .name = "vulkan", .module = vulkan_mod },
            .{ .name = "zglfw", .module = zglfw_mod },
            .{ .name = "zgui", .module = zgui.module("root") },
        },
        .target = target,
        .optimize = optimize,
    });

    mod.linkLibrary(zglfw_lib);

    mod.linkLibrary(vma_lib);
    mod.addIncludePath(vma_include_path);

    mod.linkLibrary(zgui_imgui);

    // Shader compilation
    if (b.option(std.Build.LazyPath, "shader_src", "Path to shader source directory")) |shader_src| {
        const mod_name = b.option([]const u8, "shader_mod_name", "Shader module name") orelse "shaders";
        compileAndEmbedShaders(b, shader_src, mod_name);
    }

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

fn compileAndEmbedShaders(b: *std.Build, shader_src: std.Build.LazyPath, mod_name: []const u8) void {
    const src_path = shader_src.getPath(b);

    const gen_tool = b.addExecutable(.{
        .name = "gen_shaders",
        .root_module = b.createModule(.{
            .root_source_file = b.path("gen_shaders.zig"),
            .target = b.resolveTargetQuery(.{}),
        }),
    });

    const run_gen = b.addRunArtifact(gen_tool);
    run_gen.addArg(src_path);
    const out_lp = run_gen.addOutputFileArg("shaders.zig");

    _ = b.addModule(mod_name, .{ .root_source_file = out_lp });
}
