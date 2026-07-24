const std = @import("std");

pub fn compileAndEmbedShaders(b: *std.Build, shader_src: std.Build.LazyPath, mod_name: []const u8) !*std.Build.Module {
    const shader_dir_abs = resolveShaderSrcPath(b, shader_src) catch {
        return b.addModule(mod_name, .{
            .root_source_file = b.addWriteFiles().add("shaders.zig", emptyModuleSource()),
        });
    };

    const io = b.graph.io;
    var shader_dir = std.Io.Dir.openDirAbsolute(io, shader_dir_abs, .{ .iterate = true }) catch {
        return b.addModule(mod_name, .{
            .root_source_file = b.addWriteFiles().add("shaders.zig", emptyModuleSource()),
        });
    };
    defer shader_dir.close(io);

    var walker = shader_dir.walk(b.allocator) catch {
        return b.addModule(mod_name, .{
            .root_source_file = b.addWriteFiles().add("shaders.zig", emptyModuleSource()),
        });
    };
    defer walker.deinit();

    var file_count: usize = 0;
    var captured_stdouts: std.ArrayList(std.Build.LazyPath) = .empty;
    var entry_names: std.ArrayList([]const u8) = .empty;

    while (walker.next(io) catch null) |walk_entry| {
        if (walk_entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, walk_entry.path, ".glsl")) continue;
        if (std.mem.startsWith(u8, walk_entry.path, "library/")) continue;

        const dirname_or_null = std.fs.path.dirname(walk_entry.path);
        const dirname = dirname_or_null orelse continue;
        const stage = shaderStage(dirname) orelse continue;

        const stem = walk_entry.path[0 .. walk_entry.path.len - 5];
        var name_buf: [256]u8 = undefined;
        var name_len: usize = 0;
        for (stem) |c| {
            if (name_len >= name_buf.len) break;
            if (c == '/' or c == '\\' or c == '-' or c == '.') {
                name_buf[name_len] = '_';
            } else {
                name_buf[name_len] = c;
            }
            name_len += 1;
        }
        if (name_len == 0) continue;

        const input_lp: std.Build.LazyPath = switch (shader_src) {
            .src_path => |sp| .{ .src_path = .{
                .owner = sp.owner,
                .sub_path = b.pathJoin(&.{ sp.sub_path, walk_entry.path }),
            } },
            else => continue,
        };

        const stage_flag = b.fmt("-fshader-stage={s}", .{stage});
        const glslc_run = b.addSystemCommand(&.{
            "glslc",
            "-g",
            "-I",
            shader_dir_abs,
            stage_flag,
            "-o",
            "-",
        });
        glslc_run.addFileArg(input_lp);
        glslc_run.expectExitCode(0);
        const stdout_lp = glslc_run.captureStdOut(.{});

        try captured_stdouts.append(b.allocator, stdout_lp);
        try entry_names.append(b.allocator, b.dupe(name_buf[0..name_len]));
        file_count += 1;
    }

    if (file_count == 0) {
        return b.addModule(mod_name, .{
            .root_source_file = b.addWriteFiles().add("shaders.zig", emptyModuleSource()),
        });
    }

    var names_buf: std.ArrayList(u8) = .empty;
    for (entry_names.items) |name| {
        try names_buf.appendSlice(b.allocator, name);
        try names_buf.append(b.allocator, '\n');
    }
    const names_lp = b.addWriteFiles().add("shader_names.txt", names_buf.items);

    const gen_source =
        \\const std = @import("std");
        \\
        \\pub fn main(init: std.process.Init) !void {
        \\    const alloc = init.gpa;
        \\    const io = init.io;
        \\    const args = init.minimal.args.vector;
        \\
        \\    if (args.len < 4) {
        \\        std.process.exit(1);
        \\    }
        \\
        \\    const out_path = std.mem.span(args[1]);
        \\    const names_path = std.mem.span(args[2]);
        \\    const spv_args = args[3..];
        \\
        \\    const names_data = try std.Io.Dir.readFileAlloc(
        \\        std.Io.Dir.cwd(), io, names_path, alloc, std.Io.Limit.limited(1 << 20),
        \\    );
        \\    defer alloc.free(names_data);
        \\
        \\    var names_list = try std.ArrayList([]const u8).initCapacity(alloc, spv_args.len);
        \\    defer names_list.deinit(alloc);
        \\    var iter = std.mem.splitScalar(u8, names_data, '\n');
        \\    while (iter.next()) |line| {
        \\        if (line.len > 0) {
        \\            try names_list.append(alloc, line);
        \\        }
        \\    }
        \\
        \\    if (names_list.items.len != spv_args.len) {
        \\        std.process.exit(1);
        \\    }
        \\
        \\    var out_file = try std.Io.Dir.createFileAbsolute(io, out_path, .{});
        \\    defer out_file.close(io);
        \\
        \\    try out_file.writeStreamingAll(io,
        \\        \\pub const ShaderID = enum {
        \\        \\
        \\    );
        \\
        \\    for (names_list.items) |name| {
        \\        try out_file.writeStreamingAll(io, "    ");
        \\        try out_file.writeStreamingAll(io, name);
        \\        try out_file.writeStreamingAll(io, ",\n");
        \\    }
        \\
        \\    try out_file.writeStreamingAll(io,
        \\        \\};
        \\        \\
        \\        \\pub fn get(comptime id: ShaderID) []const u32 {
        \\        \\    return switch (id) {
        \\        \\
        \\    );
        \\
        \\    var hex_buf: [32]u8 = undefined;
        \\    for (names_list.items, spv_args) |name, spv_path| {
        \\        const resolved = std.mem.span(spv_path);
        \\        var file = try std.Io.Dir.openFile(std.Io.Dir.cwd(), io, resolved, .{});
        \\        defer file.close(io);
        \\        const len = try file.length(io);
        \\        const data = try alloc.alloc(u8, @intCast(len));
        \\        defer alloc.free(data);
        \\        _ = try file.readPositionalAll(io, data, 0);
        \\
        \\        const word_count = data.len / @sizeOf(u32);
        \\        const words = try alloc.alloc(u32, word_count);
        \\        defer alloc.free(words);
        \\        {
        \\            const dest: [*]u8 = @ptrCast(words.ptr);
        \\            @memcpy(dest[0..data.len], data);
        \\        }
        \\
        \\        try out_file.writeStreamingAll(io, "        .");
        \\        try out_file.writeStreamingAll(io, name);
        \\        try out_file.writeStreamingAll(io, " => &[_]u32{\n");
        \\
        \\        for (words, 0..) |word, j| {
        \\            if (j % 8 == 0) try out_file.writeStreamingAll(io, "            ");
        \\            const hex = try std.fmt.bufPrint(&hex_buf, "0x{x:0>8}, ", .{word});
        \\            try out_file.writeStreamingAll(io, hex);
        \\            if (j % 8 == 7) try out_file.writeStreamingAll(io, "\n");
        \\        }
        \\        if (words.len % 8 != 0) try out_file.writeStreamingAll(io, "\n");
        \\        try out_file.writeStreamingAll(io, "        },\n");
        \\    }
        \\
        \\    try out_file.writeStreamingAll(io,
        \\        \\    };
        \\        \\}
        \\        \\
        \\    );
        \\}
    ;

    const gen_source_lp = b.addWriteFiles().add("gen_shaders.zig", gen_source);
    const gen_exe = b.addExecutable(.{
        .name = "gen_shaders",
        .root_module = b.createModule(.{
            .root_source_file = gen_source_lp,
            .target = b.graph.host,
        }),
    });

    const run_gen = b.addRunArtifact(gen_exe);
    const out_lp = run_gen.addOutputFileArg("shaders.zig");
    run_gen.addFileArg(names_lp);
    for (captured_stdouts.items) |spv_lp| {
        run_gen.addFileArg(spv_lp);
    }

    return b.addModule(mod_name, .{
        .root_source_file = out_lp,
    });
}

fn emptyModuleSource() []const u8 {
    return "pub const ShaderID = enum {};" ++
        "pub fn get(comptime id: ShaderID) []const u32 {" ++
        "    _ = id;" ++
        "    @compileError(\"no shaders compiled\");" ++
        "}";
}

fn shaderStage(dirname: []const u8) ?[]const u8 {
    if (std.mem.eql(u8, dirname, "vertex")) return "vertex";
    if (std.mem.eql(u8, dirname, "fragment")) return "fragment";
    if (std.mem.eql(u8, dirname, "compute")) return "compute";
    return null;
}

fn resolveShaderSrcPath(b: *std.Build, shader_src: std.Build.LazyPath) ![]const u8 {
    return switch (shader_src) {
        .src_path => |sp| b.pathJoin(&.{
            sp.owner.build_root.path orelse return error.PathUnavailable,
            sp.sub_path,
        }),
        .cwd_relative => |p| b.dupe(p),
        else => error.PathUnavailable,
    };
}

