const std = @import("std");
const mem = std.mem;
const Io = std.Io;

pub fn main(init: std.process.Init) !void {
    const alloc = init.gpa;
    const io = init.io;
    const args = init.minimal.args.vector;

    if (args.len < 3) {
        std.debug.print("usage: {s} <shader_src_dir> <output_file>\n", .{args[0]});
        std.process.exit(1);
    }
    const src_dir_path = mem.span(args[1]);
    const out_file_path = mem.span(args[2]);

    const src_dir = try Io.Dir.openDirAbsolute(io, src_dir_path, .{ .iterate = true });
    defer Io.Dir.close(src_dir, io);

    var walker = try Io.Dir.walk(src_dir, alloc);
    defer walker.deinit();

    const Entry = struct {
        name: []const u8,
        stage: []const u8,
        full_path: []const u8,
    };

    var entries: std.ArrayListUnmanaged(Entry) = .empty;

    while (try walker.next(io)) |walk_entry| {
        if (walk_entry.kind != .file) continue;
        if (!mem.endsWith(u8, walk_entry.path, ".glsl")) continue;
        if (mem.startsWith(u8, walk_entry.path, "library/")) continue;

        const dir = std.fs.path.dirname(walk_entry.path) orelse continue;
        const stage = stageFromDirname(dir) orelse continue;
        const stem = walk_entry.path[0 .. walk_entry.path.len - 5];
        const ident = try sanitizeIdentifier(alloc, stem);

        try entries.append(alloc, .{
            .name = ident,
            .stage = try alloc.dupe(u8, stage),
            .full_path = try alloc.dupe(u8, walk_entry.path),
        });
    }

    var out_file = try Io.Dir.createFileAbsolute(io, out_file_path, .{});
    defer out_file.close(io);

    var hex_buf: [1024]u8 = undefined;

    if (entries.items.len == 0) {
        try out_file.writeStreamingAll(io,
            \\pub const ShaderID = enum {};
            \\pub fn get(comptime id: ShaderID) []const u32 {
            \\    _ = id;
            \\    @compileError("no shaders compiled");
            \\}
            \\
        );
        return;
    }

    try out_file.writeStreamingAll(io,
        \\const std = @import("std");
        \\
        \\pub const ShaderID = enum {
        \\
    );

    for (entries.items) |e| {
        const line = try std.fmt.bufPrint(&hex_buf, "    {s},\n", .{e.name});
        try out_file.writeStreamingAll(io, line);
    }

    try out_file.writeStreamingAll(io,
        \\};
        \\
        \\pub fn get(comptime id: ShaderID) []const u32 {
        \\    return switch (id) {
        \\
    );

    for (entries.items) |e| {
        const compiled = try compileShader(alloc, io, src_dir_path, e.full_path, e.stage);

        const header = try std.fmt.bufPrint(&hex_buf, "        .{s} => &[_]u32{{", .{e.name});
        try out_file.writeStreamingAll(io, header);

        for (compiled, 0..) |word, i| {
            if (i % 8 == 0) try out_file.writeStreamingAll(io, "\n            ");
            const hx = try std.fmt.bufPrint(&hex_buf, "0x{x:0>8}, ", .{word});
            try out_file.writeStreamingAll(io, hx);
        }

        try out_file.writeStreamingAll(io, "\n        },\n");
    }

    try out_file.writeStreamingAll(io,
        \\    };
        \\}
        \\
    );
}

fn sanitizeIdentifier(alloc: mem.Allocator, path: []const u8) ![]u8 {
    var result = try alloc.dupe(u8, path);
    for (result, 0..) |c, i| {
        if (c == '/' or c == '\\' or c == '-' or c == '.') {
            result[i] = '_';
        }
    }
    return result;
}

fn stageFromDirname(dirname: []const u8) ?[]const u8 {
    if (mem.eql(u8, dirname, "vertex")) return "vertex";
    if (mem.eql(u8, dirname, "fragment")) return "fragment";
    if (mem.eql(u8, dirname, "compute")) return "compute";
    return null;
}

fn compileShader(alloc: mem.Allocator, io: Io, src_dir: []const u8, rel_path: []const u8, stage: []const u8) ![]const u32 {
    const full_path = try std.fs.path.join(alloc, &.{ src_dir, rel_path });
    defer alloc.free(full_path);

    const full_src_dir = try std.fs.path.resolve(alloc, &.{src_dir});
    defer alloc.free(full_src_dir);

    const stage_flag = try std.fmt.allocPrint(alloc, "-fshader-stage={s}", .{stage});
    defer alloc.free(stage_flag);

    const result = try std.process.run(alloc, io, .{
        .argv = &.{
            "glslc",
            "-I",
            full_src_dir,
            stage_flag,
            "-o",
            "-",
            full_path,
        },
        .stdout_limit = .unlimited,
        .stderr_limit = .unlimited,
    });

    switch (result.term) {
        .exited => |code| {
            if (code != 0) {
                std.debug.print("glslc failed for '{s}' (exit {d}):\n{s}\n", .{ rel_path, code, result.stderr });
                std.process.exit(1);
            }
        },
        else => {
            std.debug.print("glslc terminated abnormally for '{s}'\n{s}\n", .{ rel_path, result.stderr });
            std.process.exit(1);
        },
    }

    const word_count = result.stdout.len / @sizeOf(u32);
    const words = try alloc.alloc(u32, word_count);
    const dest_u8: []u8 = @ptrCast(words);
    @memcpy(dest_u8, result.stdout);
    alloc.free(result.stdout);
    return words;
}
