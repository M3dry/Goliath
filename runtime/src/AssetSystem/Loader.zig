const std = @import("std");
const base = @import("base");

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
    keep_loaded: bool,

    pub fn jsonStringify(self: *const Location, jws: anytype) !void {
        try jws.write(ManifestLocation{
            .path = self.path,
            .keep_loaded = self.keep_loaded,
        });
    }
};

io: std.Io,

locations_path_prefix_str: []const u8,
locations_path_prefix: std.Io.Dir,
locations: []Location,

pub fn init(io: std.Io, alloc: Allocator, manifest: *const Manifest) !Loader {
    const prefix_str = try alloc.dupe(u8, manifest.path_prefix);
    errdefer alloc.free(prefix_str);

    const prefix_dir = try std.Io.Dir.openDir(.cwd(), io, manifest.path_prefix, .{});
    errdefer prefix_dir.close(io);

    var self: Loader = .{
        .io = io,
        .locations_path_prefix_str = prefix_str,
        .locations_path_prefix = prefix_dir,
        .locations = &.{},
    };

    self.locations = try alloc.alloc(Location, manifest.locations.len);
    errdefer alloc.free(self.locations);

    var i: usize = 0;
    errdefer for (0..i) |n| {
        alloc.free(self.locations[n].path);
        if (self.locations[n].memory_map) |*m| m.destroy(io);
    };

    for (self.locations, manifest.locations) |*loc, manifest_location| {
        loc.path = try alloc.dupe(u8, manifest_location.path);
        loc.memory_map = null;
        loc.ref_count = 0;
        loc.keep_loaded = manifest_location.keep_loaded;

        i += 1;

        if (loc.keep_loaded) {
            const file = try self.locations_path_prefix.openFile(io, loc.path, .{});
            defer file.close(self.io);

            const m = try file.createMemoryMap(io, .{ .len = try file.length(self.io) });
            errdefer m.destroy(io);

            loc.memory_map = m;
            loc.ref_count += 1;
        }
    }

    return self;
}

pub fn deinit(self: *Loader, alloc: Allocator) void {
    self.locations_path_prefix.close(self.io);

    for (self.locations) |*location| {
        if (location.memory_map) |*m| m.destroy(self.io);
        alloc.free(location.path);
    }
    alloc.free(self.locations);
}

pub fn load(self: *Loader, cold_asset: *const ColdAsset) ![]const u8 {
    const location = &self.locations[cold_asset.location];

    if (location.memory_map) |memory_map| {
        location.ref_count += 1;
        return memory_map.memory[cold_asset.offset..cold_asset.offset + cold_asset.size];
    }

    const file = try self.locations_path_prefix.openFile(self.io, location.path, .{});
    defer file.close(self.io);

    const m = try file.createMemoryMap(self.io, .{ .len = try file.length(self.io) });
    errdefer m.destroy(self.io);

    location.ref_count += 1;
    location.memory_map = m;

    return m.memory[cold_asset.offset..cold_asset.offset + cold_asset.size];
}

const Ctx = struct {
    self: *Loader,
    cold_asset: ColdAsset,
    alloc: Allocator,
};

const FreeFnCtx = struct {
    free_fn: base.Transport.FreeFn = transport_free_fn,
    ctx: *Ctx,

    pub fn deinit(self: *FreeFnCtx) void {
        const alloc = self.ctx.alloc;
        alloc.destroy(self);
    }
};

fn transport_free_fn(anyctx: ?*anyopaque, ptr: *anyopaque) void {
    _ = ptr;

    const ctx: *Ctx = @ptrCast(@alignCast(anyctx.?));
    const alloc = ctx.alloc;

    ctx.self.unload(&ctx.cold_asset);
    alloc.destroy(ctx);
}

pub fn make_transport_unload(self: *Loader, alloc: Allocator, cold_asset: *const ColdAsset) !FreeFnCtx {
    const ctx = try alloc.create(Ctx);

    ctx.self = self;
    ctx.cold_asset = cold_asset.*;
    ctx.alloc = alloc;

    return .{
        .ctx = ctx,
    };
}

pub fn unload(self: *Loader, cold_asset: *const ColdAsset) void {
    const location = &self.locations[cold_asset.location];

    std.debug.assert(location.ref_count != 0);
    location.ref_count -= 1;

    if (location.ref_count != 0) return;

    if (location.memory_map) |*m| {
        m.destroy(self.io);
    } else std.debug.assert(false);

    location.memory_map = null;
}

pub const ManifestLocation = struct {
    path: []const u8,
    keep_loaded: bool,
};

pub const Manifest = struct {
    path_prefix: []const u8,
    locations: []ManifestLocation,
};

pub fn jsonStringify(self: *const Loader, jws: anytype) !void {
    try jws.beginObject();

    try jws.objectFieldRaw("\"path_prefix\"");
    try jws.write(self.locations_path_prefix_str);

    try jws.objectFieldRaw("\"locations\"");
    try jws.write(self.locations);

    try jws.endObject();
}

test {
    std.testing.refAllDecls(@This());
}
