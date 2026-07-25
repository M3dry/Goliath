const std = @import("std");

const Entry = struct {
    hash: u64,
    lib_hash: u64,
};

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    const io = init.io;
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const alloc = arena.allocator();

    var out_path: []const u8 = "";
    var src_dir: []const u8 = "";
    var lib_dir: ?[]const u8 = null;
    var entry_files: std.ArrayList([]const u8) = .empty;

    const args = init.minimal.args.vector;
    var i: usize = 1;
    while (i < args.len) : (i += 1) {
        const arg = std.mem.span(args[i]);
        if (std.mem.eql(u8, arg, "--out")) {
            i += 1;
            if (i >= args.len) return error.MissingArg;
            out_path = std.mem.span(args[i]);
        } else if (std.mem.eql(u8, arg, "--src")) {
            i += 1;
            if (i >= args.len) return error.MissingArg;
            src_dir = std.mem.span(args[i]);
        } else if (std.mem.eql(u8, arg, "--lib")) {
            i += 1;
            if (i >= args.len) return error.MissingArg;
            lib_dir = std.mem.span(args[i]);
        } else if (std.mem.eql(u8, arg, "--lib-src")) {
            i += 1;
            if (i >= args.len) return error.MissingArg;
        } else {
            try entry_files.append(alloc, std.mem.span(args[i]));
        }
    }

    if (out_path.len == 0) return error.MissingOutPath;
    if (src_dir.len == 0) return error.MissingSrcDir;

    const actual_lib = lib_dir orelse blk: {
        break :blk try std.fs.path.join(alloc, &.{ src_dir, "library" });
    };
    const lib_hash = computeLibHash(io, alloc, actual_lib) catch 0;

    const cache_dir = try std.fs.path.join(alloc, &.{ src_dir, ".slang_cache" });
    std.Io.Dir.cwd().createDirPath(io, cache_dir) catch {};

    var manifest = std.StringHashMap(Entry).init(alloc);
    try loadManifest(io, alloc, &manifest, cache_dir);

    var entry_names = std.ArrayList([]const u8).empty;
    var entry_spv_paths = std.ArrayList([]const u8).empty;
    var any_changed = false;

    for (entry_files.items) |entry_path| {
        const name = try deriveName(alloc, src_dir, entry_path);

        const content = std.Io.Dir.cwd().readFileAlloc(io, entry_path, alloc, std.Io.Limit.limited(1 << 20)) catch |err| {
            std.log.err("failed to read {s}: {s}", .{ entry_path, @errorName(err) });
            return err;
        };
        const hash = std.hash.Wyhash.hash(0, content);

        const spv_path = try std.fs.path.join(alloc, &.{ cache_dir, name });
        const spv_path_with_ext = try std.fmt.allocPrint(alloc, "{s}.spv", .{spv_path});

        const cached = manifest.get(name);
        if (cached) |_| {
            if (cached.?.hash == hash and cached.?.lib_hash == lib_hash and fileExists(io, spv_path_with_ext)) {
                try entry_names.append(alloc, try alloc.dupe(u8, name));
                try entry_spv_paths.append(alloc, try alloc.dupe(u8, spv_path_with_ext));
                continue;
            }
        }

        any_changed = true;

        const argv = &.{
            "slangc",
            entry_path,
            "-target", "spirv",
            "-o", spv_path_with_ext,
            "-I", actual_lib,
            "-fvk-use-entrypoint-name",
            "-fvk-use-scalar-layout",
        };

        var child = std.process.spawn(io, .{
            .argv = argv,
            .stdin = .ignore,
            .stdout = .pipe,
            .stderr = .pipe,
        }) catch |err| {
            std.log.err("failed to spawn slangc for {s}: {s}", .{ entry_path, @errorName(err) });
            return err;
        };

        var buf: [4096]u8 = undefined;

        var stderr_reader = child.stderr.?.reader(io, &buf);
        const stderr = try stderr_reader.interface.allocRemaining(alloc, .unlimited);

        const term = child.wait(io) catch |err| {
            std.log.err("failed to wait slangc for {s}: {s}", .{ entry_path, @errorName(err) });
            return err;
        };

        switch (term) {
            .exited => |code| {
                if (code != 0) {
                    std.log.err("slangc failed for {s} (exit {d}):\n{s}", .{ entry_path, code, stderr });
                    return error.SlangcCompileError;
                }
            },
            else => {
                std.log.err("slangc terminated abnormally for {s}", .{entry_path});
                return error.SlangcCompileError;
            },
        }

        const name_dup = try alloc.dupe(u8, name);
        try manifest.put(name_dup, .{ .hash = hash, .lib_hash = lib_hash });

        try entry_names.append(alloc, try alloc.dupe(u8, name));
        try entry_spv_paths.append(alloc, try alloc.dupe(u8, spv_path_with_ext));
    }

    if (any_changed) try saveManifest(io, alloc, &manifest, cache_dir);

    try generateShadersZig(alloc, io, out_path, entry_names.items, entry_spv_paths.items);
}

fn fileExists(io: std.Io, path: []const u8) bool {
    const f = std.Io.Dir.cwd().openFile(io, path, .{}) catch return false;
    f.close(io);
    return true;
}

fn deriveName(alloc: std.mem.Allocator, src_dir: []const u8, entry_path: []const u8) ![]const u8 {
    const rel = if (std.mem.startsWith(u8, entry_path, src_dir))
        entry_path[src_dir.len..]
    else
        entry_path;

    const trimmed = if (rel.len > 0 and (rel[0] == '/' or rel[0] == '\\'))
        rel[1..]
    else
        rel;

    var name = std.ArrayList(u8).empty;

    for (trimmed) |c| {
        if (c == '.') break;
        if (c == '/' or c == '\\' or c == '-') {
            try name.append(alloc, '_');
        } else {
            try name.append(alloc, c);
        }
    }

    return name.toOwnedSlice(alloc);
}

fn loadManifest(io: std.Io, alloc: std.mem.Allocator, manifest: *std.StringHashMap(Entry), cache_dir: []const u8) !void {
    const manifest_path = try std.fs.path.join(alloc, &.{ cache_dir, "manifest.json" });

    const data = std.Io.Dir.cwd().readFileAlloc(io, manifest_path, alloc, std.Io.Limit.limited(1 << 20)) catch |err| switch (err) {
        error.FileNotFound => return,
        else => return err,
    };
    if (data.len == 0) return;

    const parsed = try std.json.parseFromSlice(std.json.Value, alloc, data, .{});
    defer parsed.deinit();
    const root = parsed.value;

    const entries_obj = root.object.get("entries") orelse return;
    const entries_map = entries_obj.object;

    var it = entries_map.iterator();
    while (it.next()) |kv| {
        const entry_obj = kv.value_ptr.*.object;
        const hash_val = entry_obj.get("hash") orelse continue;
        const hash = switch (hash_val) {
            .integer => |v| @as(u64, @intCast(v)),
            .number_string => |s| std.fmt.parseInt(u64, s, 10) catch continue,
            else => continue,
        };
        const lib_hash_val = entry_obj.get("lib_hash") orelse continue;
        const lib_hash = switch (lib_hash_val) {
            .integer => |v| @as(u64, @intCast(v)),
            .number_string => |s| std.fmt.parseInt(u64, s, 10) catch 0,
            else => 0,
        };
        try manifest.put(try alloc.dupe(u8, kv.key_ptr.*), .{ .hash = hash, .lib_hash = lib_hash });
    }
}

fn saveManifest(io: std.Io, alloc: std.mem.Allocator, manifest: *std.StringHashMap(Entry), cache_dir: []const u8) !void {
    const manifest_path = try std.fs.path.join(alloc, &.{ cache_dir, "manifest.json" });

    var entries_map = try std.json.ObjectMap.init(alloc, &.{}, &.{});
    var it = manifest.iterator();
    while (it.next()) |entry| {
        var entry_obj = try std.json.ObjectMap.init(alloc, &.{}, &.{});
        const hash_str = try std.fmt.allocPrint(alloc, "{d}", .{entry.value_ptr.hash});
        try entry_obj.put(alloc, "hash", std.json.Value{ .number_string = hash_str });
        const lib_hash_str = try std.fmt.allocPrint(alloc, "{d}", .{entry.value_ptr.lib_hash});
        try entry_obj.put(alloc, "lib_hash", std.json.Value{ .number_string = lib_hash_str });
        try entries_map.put(alloc, try alloc.dupe(u8, entry.key_ptr.*), std.json.Value{ .object = entry_obj });
    }

    var root_obj = try std.json.ObjectMap.init(alloc, &.{}, &.{});
    try root_obj.put(alloc, "version", std.json.Value{ .integer = 1 });
    try root_obj.put(alloc, "entries", std.json.Value{ .object = entries_map });

    const value = std.json.Value{ .object = root_obj };

    const out_file = try std.Io.Dir.cwd().createFile(io, manifest_path, .{});
    defer out_file.close(io);

    var buf: [4096]u8 = undefined;
    var w = out_file.writer(io, &buf);
    try std.json.Stringify.value(value, .{ .whitespace = .indent_2 }, &w.interface);
    try w.flush();
}

fn computeLibHash(io: std.Io, alloc: std.mem.Allocator, lib_dir: []const u8) !u64 {
    var hasher = std.hash.Wyhash.init(0);
    var dir = try std.Io.Dir.openDirAbsolute(io, lib_dir, .{ .iterate = true });
    defer dir.close(io);
    var walker = try dir.walk(alloc);
    defer walker.deinit();
    while (try walker.next(io)) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, entry.path, ".slang")) continue;
        const full_path = try std.fs.path.join(alloc, &.{ lib_dir, entry.path });
        const content = try std.Io.Dir.cwd().readFileAlloc(io, full_path, alloc, std.Io.Limit.limited(1 << 20));
        hasher.update(content);
    }
    return hasher.final();
}

fn generateShadersZig(alloc: std.mem.Allocator, io: std.Io, out_path: []const u8, names: []const []const u8, spv_paths: []const []const u8) !void {
    var out_file = try std.Io.Dir.createFileAbsolute(io, out_path, .{});
    defer out_file.close(io);

    var wbuf: [4096]u8 = undefined;
    var w = out_file.writer(io, &wbuf);
    defer w.flush() catch {};

    try w.interface.writeAll(
        \\const std = @import("std");
        \\
        \\pub const ShaderID = enum {
        \\
    );

    for (names) |name| {
        try w.interface.print("    {s},\n", .{name});
    }

    try w.interface.writeAll(
        \\};
        \\
        \\pub fn get(comptime id: ShaderID) []const u32 {
        \\    return switch (id) {
        \\
    );

    for (names, spv_paths) |name, spv_path| {
        const file = std.Io.Dir.cwd().openFile(io, spv_path, .{}) catch |err| {
            std.log.err("failed to open cached spv {s}: {s}", .{ spv_path, @errorName(err) });
            return err;
        };
        defer file.close(io);

        const len = file.length(io) catch |err| {
            std.log.err("failed to get size of {s}: {s}", .{ spv_path, @errorName(err) });
            return err;
        };
        const data = try alloc.alloc(u8, @intCast(len));
        defer alloc.free(data);
        _ = try file.readPositionalAll(io, data, 0);

        const words = std.mem.bytesAsSlice(u32, data);

        try w.interface.writeAll("        .");
        try w.interface.writeAll(name);
        try w.interface.writeAll(" => &[_]u32{\n");

        var hex_buf: [32]u8 = undefined;
        for (words, 0..) |word, j| {
            if (j % 8 == 0) try w.interface.writeAll("            ");
            const hex = try std.fmt.bufPrint(&hex_buf, "0x{x:0>8}, ", .{word});
            try w.interface.writeAll(hex);
            if (j % 8 == 7) try w.interface.writeAll("\n");
        }
        if (words.len % 8 != 0) try w.interface.writeAll("\n");
        try w.interface.writeAll("        },\n");
    }

    try w.interface.writeAll(
        \\    };
        \\}
        \\
    );
}
