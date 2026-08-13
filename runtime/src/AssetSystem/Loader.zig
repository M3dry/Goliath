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
};

io: std.Io,

locations_path_prefix: std.Io.Dir,
locations: []Location,

pub fn init(io: std.Io, asset_root: []const u8) !Loader {
    const self: Loader = .{
        .io = io,
        .locations_path_prefix = try std.Io.Dir.openDir(.cwd(), io, asset_root, .{}),
        .locations = &.{},
    };

    return self;
}

pub fn populate_locations(self: *Loader) void {
    _ = self;
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

test {
    std.testing.refAllDecls(@This());
}
