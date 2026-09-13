const std = @import("std");
const base = @import("base");
const zprobe = base.zprobe;

const Loader = @This();
const Allocator = std.mem.Allocator;

pub const ColdAsset = struct {
    location: u32,
    offset: usize,
    size: usize,
};

const Location = struct {
    path: []const u8,
    memory_map: ?std.Io.File.MemoryMap,
    ref_count: u32 = 0,

    pub const none: Location = .{
        .path = &.{},
        .memory_map = null,
        .ref_count = std.math.maxInt(u32),
    };

    pub fn isNone(self: Location) bool {
        return self.ref_count == none.ref_count;
    }
};

locations_path_prefix_str: []const u8,
locations_path_prefix: std.Io.Dir,
locations: std.ArrayList(Location),

free_locations: std.ArrayList(u32),

pub fn init(io: std.Io, alloc: Allocator, manifest: *const Manifest) !Loader {
    const prefix_str = try alloc.dupe(u8, manifest.path_prefix);
    errdefer alloc.free(prefix_str);

    const prefix_dir = try std.Io.Dir.openDir(.cwd(), io, manifest.path_prefix, .{});
    errdefer prefix_dir.close(io);

    zprobe.event(.info, "asset loader initialized", .{ .prefix_path = manifest.path_prefix });

    var self: Loader = .{
        .locations_path_prefix_str = prefix_str,
        .locations_path_prefix = prefix_dir,
        .locations = .empty,
        .free_locations = .empty,
    };
    errdefer self.locations.deinit(alloc);
    errdefer for (self.locations.items) |loc| alloc.free(loc.path);

    try self.locations.ensureUnusedCapacity(alloc, manifest.locations.len);
    for (manifest.locations) |manifest_location| {
        while (self.locations.items.len <= manifest_location.ix) {
            try self.locations.append(alloc, .none);
        }

        if (!self.locations.items[manifest_location.ix].isNone()) {
            return error.DuplicateLocationIndex;
        }

        const path = try alloc.dupe(u8, manifest_location.path);

        self.locations.items[manifest_location.ix] = .{
            .path = path,
            .memory_map = null,
            .ref_count = 0,
        };
    }

    for (self.locations.items, 0..) |location, i| {
        if (location.isNone()) {
            try self.free_locations.append(alloc, @intCast(i));
        }
    }

    return self;
}

pub fn deinit(self: *Loader, alloc: Allocator, io: std.Io) void {
    alloc.free(self.locations_path_prefix_str);
    self.locations_path_prefix.close(io);

    for (self.locations.items) |*location| {
        if (location.memory_map) |*m| m.destroy(io);
        alloc.free(location.path);
    }
    self.locations.deinit(alloc);
    self.free_locations.deinit(alloc);
}

pub fn load(self: *Loader, io: std.Io, cold_asset: *const ColdAsset) ![]const u8 {
    const location = &self.locations.items[cold_asset.location];

    if (location.memory_map) |memory_map| {
        location.ref_count += 1;
        zprobe.event(.debug, "Loader/load", .{ .location = cold_asset.location, .offset = cold_asset.offset, .size = cold_asset.size, .ref_count = location.ref_count, .mapped = true });
        return memory_map.memory[cold_asset.offset .. cold_asset.offset + cold_asset.size];
    }

    const file = try self.locations_path_prefix.openFile(io, location.path, .{});
    defer file.close(io);

    const m = try file.createMemoryMap(io, .{ .len = try file.length(io), .protection = .{ .read = true } });
    errdefer m.destroy(io);

    location.ref_count += 1;
    location.memory_map = m;

    zprobe.event(.debug, "Loader/load", .{ .location = cold_asset.location, .offset = cold_asset.offset, .size = cold_asset.size, .ref_count = location.ref_count, .mapped = false });

    return m.memory[cold_asset.offset .. cold_asset.offset + cold_asset.size];
}

const Ctx = struct {
    self: *Loader,
    io: std.Io,
    cold_asset: ColdAsset,
    alloc: Allocator,
};

const FreeFnCtx = struct {
    free_fn: base.Transport.FreeFn = transport_free_fn,
    ctx: *Ctx,

    /// only cleans up the `ctx` memory
    /// use only for errdefer cleanup, still call loader.unload in a errdefer
    pub fn deinit(self: *FreeFnCtx) void {
        const alloc = self.ctx.alloc;
        alloc.destroy(self);
    }
};

fn transport_free_fn(anyctx: ?*anyopaque, ptr: *anyopaque) void {
    _ = ptr;
    const ctx: *Ctx = @ptrCast(@alignCast(anyctx.?));

    const alloc = ctx.alloc;

    ctx.self.unload(ctx.io, &ctx.cold_asset);
    alloc.destroy(ctx);
}

pub fn make_transport_unload(self: *Loader, io: std.Io, alloc: Allocator, cold_asset: *const ColdAsset) !FreeFnCtx {
    const ctx = try alloc.create(Ctx);

    ctx.self = self;
    ctx.io = io;
    ctx.cold_asset = cold_asset.*;
    ctx.alloc = alloc;

    return .{
        .ctx = ctx,
    };
}

pub fn unload(self: *Loader, io: std.Io, cold_asset: *const ColdAsset) void {
    const location = &self.locations.items[cold_asset.location];

    std.debug.assert(location.ref_count != 0);
    location.ref_count -= 1;

    if (location.ref_count != 0) return;

    if (location.memory_map) |*m| {
        m.destroy(io);
    } else std.debug.assert(false);

    location.memory_map = null;
    zprobe.event(.debug, "Loader/unload", .{ .location = cold_asset.location, .ref_count = location.ref_count, .unmapped = true });
}

/// takes ownership of `path`
pub fn newLocation(self: *Loader, alloc: Allocator, path: []const u8) error{OutOfMemory}!u32 {
    var reused = false;
    const location, const loc_index = if (self.free_locations.pop()) |loc| blk: {
        reused = true;
        break :blk .{ &self.locations.items[loc], loc };
    } else .{ try self.locations.addOne(alloc), @as(u32, @intCast(self.locations.items.len - 1)) };
    location.* = .{
        .path = path,
        .memory_map = null,
        .ref_count = 0,
    };

    zprobe.event(.debug, "Loader/newLocation", .{ .location = loc_index, .path = path, .reused = reused });

    return loc_index;
}

pub fn removeLocation(self: *Loader, alloc: Allocator, io: std.Io, loc: u32) !void {
    var location = &self.locations.items[loc];
    const path = location.path;

    if (location.memory_map) |*m| m.destroy(io);
    self.locations_path_prefix.deleteFile(io, location.path) catch {};
    zprobe.event(.debug, "Loader/removeLocation", .{ .location = loc, .path = path });
    alloc.free(path);

    location.* = .none;
    try self.free_locations.append(alloc, loc);
}

// TODO: serialize also the index
pub const ManifestLocation = struct {
    ix: u32,
    path: []const u8,
};

pub const Manifest = struct {
    path_prefix: []const u8,
    locations: []ManifestLocation = &.{},
};

pub fn jsonStringify(self: *const Loader, jws: anytype) !void {
    try jws.beginObject();

    try jws.objectFieldRaw("\"path_prefix\"");
    try jws.write(self.locations_path_prefix_str);

    try jws.objectFieldRaw("\"locations\"");
    try jws.beginArray();
    for (self.locations.items, 0..) |loc, i| {
        if (loc.isNone()) continue;

        try jws.write(ManifestLocation{
            .ix = @intCast(i),
            .path = loc.path,
        });
    }
    try jws.endArray();

    try jws.endObject();
}

test {
    std.testing.refAllDecls(@This());
}
