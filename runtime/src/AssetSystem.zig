const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;

pub const TextureRegistry = @import("AssetSystem/TextureRegistry.zig");
const Loader = @import("AssetSystem/Loader.zig");
const resolver = @import("AssetSystem/resolver.zig");

const types = @import("AssetSystem/Types.zig");
const Gid = types.Gid;
const Kind = types.Kind;
const Entry = types.Entry;

const AssetSystem = @This();

alloc: Allocator,
loader: Loader,
threaded_io: std.Io.Threaded,
io: std.Io,

entries: std.MultiArrayList(Entry) = .empty,
free_entries: std.ArrayList(u32) = .empty,

texture_reg: TextureRegistry,
// registires for the other asset kinds...

pub const ManifestReader = union(enum) {
    reader: *std.Io.Reader,
    default: struct {
        path_prefix: []const u8,
    },
};

/// allocator needs to be thread safe
pub fn init(gc: *const base.GraphicsCtx, transport: *base.Transport, alloc: Allocator, tmp_alloc: Allocator, manifest_reader: ManifestReader) !AssetSystem {
    var threaded_io = std.Io.Threaded.init(alloc, .{
        .disable_memory_mapping = false,
        .concurrent_limit = .unlimited,
    });
    errdefer threaded_io.deinit();
    const io = threaded_io.io();

    var arena_alloc = std.heap.ArenaAllocator.init(tmp_alloc);
    defer arena_alloc.deinit();
    const tmp_arena_alloc = arena_alloc.allocator();

    const manifest = switch (manifest_reader) {
        .default => |d| Manifest{
            .loader = .{
                .path_prefix = d.path_prefix,
            },
        },
        .reader => |r| blk: {
            var json_reader = std.json.Reader.init(tmp_arena_alloc, r);
            defer json_reader.deinit();

            break :blk try std.json.parseFromTokenSourceLeaky(Manifest, tmp_arena_alloc, &json_reader, .{ .ignore_unknown_fields = true });
        },
    };

    var loader = try Loader.init(io, alloc, &manifest.loader);
    errdefer loader.deinit(alloc);

    var texture_reg = try TextureRegistry.init(gc, transport, alloc);
    errdefer texture_reg.deinitNow(gc);

    var entries: std.MultiArrayList(Entry) = .empty;
    errdefer entries.deinit(alloc);
    errdefer {
        const slice = entries.slice();
        for (slice.items(.dense), slice.items(.name), slice.items(.deps), slice.items(.rdeps)) |dense, name, *deps, *rdeps | {
            if (dense == Entry.none.dense) continue;

            alloc.free(name);
            deps.necessary.deinit(alloc);
            deps.gids.deinit(alloc);
            rdeps.deinit(alloc);
        }
    }
    try entries.ensureTotalCapacity(alloc, manifest.entries.len);

    for (manifest.entries) |manifest_entry| {
        const gid = manifest_entry.gid;
        while (entries.len <= gid.slot) {
            try entries.append(alloc, .none);
        }

        if (entries.get(gid.slot).isNone()) {
            var entry: Entry = .{
                .generation = gid.gen,
                .kind = manifest_entry.kind,
                .cold_asset = manifest_entry.cold_asset,
            };

            entry.name = try alloc.dupe(u8, manifest_entry.name);
            errdefer alloc.free(entry.name);

            try entry.deps.necessary.ensureCapacity(alloc, manifest_entry.deps.len);
            errdefer entry.deps.necessary.deinit(alloc);

            try entry.deps.gids.ensureCapacity(alloc, manifest_entry.deps.len);
            errdefer entry.deps.gids.deinit(alloc);

            for (0.., manifest_entry.deps) |i, dep| {
                entry.deps.gids.addOneAssumeCapacity().* = dep.gid;
                entry.deps.necessary.setAssumeCapacity(i);
            }

            try entry.rdeps.ensureCapacity(alloc, manifest_entry.rdeps.len);
            errdefer entry.rdeps.deinit(alloc);

            for (manifest_entry.rdeps) |rdep| {
                entry.rdeps.addOneAssumeCapacity().* = rdep;
            }

            entries.set(gid.slot, entry);
        } else return error.DuplicateGidEntry;
    }

    var free_entries: std.ArrayList(u32) = .empty;
    errdefer free_entries.deinit(alloc);

    const slice = entries.slice();
    const dense = slice.items(.dense);
    for (dense, 0..) |*d, i| {
        if (d.* == Entry.none.dense) {
            try free_entries.append(alloc, @intCast(i));
        }
    }

    const gens = slice.items(.generation);
    for (slice.items(.deps), slice.items(.rdeps)) |*deps, *rdeps| {
        for (deps.gids.items()) |gid| {
            if (gid.slot >= entries.len) return error.DanglingDependency;
            if (dense[gid.slot] == Entry.none.dense) return error.DanglingDependency;
            if (gens[gid.slot] != gid.gen) return error.DanglingDependency;
        }

        for (rdeps.items()) |gid| {
            if (gid.slot >= entries.len) return error.DanglingDependency;
            if (dense[gid.slot] == Entry.none.dense) return error.DanglingDependency;
            if (gens[gid.slot] != gid.gen) return error.DanglingDependency;
        }
    }

    return .{
        .alloc = alloc,
        .loader = loader,
        .threaded_io = threaded_io,
        .io = io,
        .entries = entries,
        .free_entries = free_entries,
        .texture_reg = texture_reg,
    };

    // UPDATE NOTE: let's not upload MeshDesc and LODEntries at load,
    //              instead acquire(gid of kind mesh & model) populates the MeshDesc and lod entries gpu buffers
    //              this makes it possible to say upload only meshes for the current scenes, and the GPU requests from them - since we already do batching via the command buffer we can do the whole mesh desc upload in one pass
    //              then calling acquire(gid of kind geometry) will upload the geometry and patch up all of it's rdeps that are on the GPU - if afterhand a meshdesc gets added, it points it's lods to the geometry if it's uploaded
    // CONSEQUENCE: need to a "contract" param in the deps that says if that dependency is a hard one or not - i.e. do we need to immediately load the asset - mesh's geometry deps will be marked as soft eg
    // TODO: upload metadata to the GPU (Mesh&LOD descs)
    //       this opens the possibility of the GPU requesting assets based on visibility
    //       uploaded LOD descs have -1 for material schema and instance, once geometry is loaded it gets patched to the right indices along with the geometry pointer
    //          thus stable GPU side material schema and instance indices aren't needed
}

pub fn deinit(self: *AssetSystem, destroy_queue: *base.DestroyQueue) void {
    self.loader.deinit(self.alloc);
    self.threaded_io.deinit();

    for (self.entries.items(.name)) |name| {
        self.alloc.free(name);
    }
    self.entries.deinit(self.alloc);
    self.free_entries.deinit(self.alloc);

    self.texture_reg.deinit(destroy_queue);
}

pub const KindData = union(Kind) {
    texture: TextureRegistry.IngestTexture,
    sampled_texture: TextureRegistry.IngestSampledTexture,
    material_schema: void,
    material_instance: void,
    geometry: void,
    mesh: void,
    model: void,
    skeleton: void,
};

pub fn finalizeAsset(self: *AssetSystem, entry: Entry, dupe_entry_name: bool) !Gid {
    if (entry.isNone()) return error.EntryIsNone;

    var e = entry;
    // TODO: verify that deps of entry are valid
    // TODO: udpate rdeps of deps

    if (dupe_entry_name) {
        e.name = try self.alloc.dupe(u8, entry.name);
    }
    errdefer if (dupe_entry_name) self.alloc.free(e.name);

    if (self.free_entries.pop()) |slot| {
        e.generation = self.entries.items(.generation)[slot] + 1;
        self.entries.set(slot, e);

        return Gid{ .gen = e.generation, .slot = slot };
    } else {
        e.generation = 0;
        try self.entries.append(self.alloc, e);

        return Gid{
            .gen = e.generation,
            .slot = @intCast(self.entries.len - 1),
        };
    }
}

fn makeTargetPath(self: *AssetSystem, target_path: []const u8) ![]u8 {
    var target_exists = true;
    var increment: usize = 0;
    var path: []const u8 = &.{};
    var buf: []u8 = &.{};
    while (true) {
        path = if (increment == 0) target_path else blk: {
            if (increment == 1) buf = try self.alloc.alloc(u8, target_path.len + 20); // 20 digits in 2^64
            break :blk try std.fmt.bufPrint(buf, "{s}-{}", .{target_path, increment});
        };

        target_exists = false;
        self.loader.locations_path_prefix.access(self.io, target_path, .{}) catch |e| switch (e) {
            error.FileNotFound => target_exists = true,
            else => return e,
        };

        if (!target_exists) break;
        increment += 1;
    }

    const ret = try self.alloc.dupe(u8, path);
    self.alloc.free(buf);
    return ret;

}

/// up to the caller to clean up `data` after awaiting/cancelling the future
/// cleanup after cancelling via `ingest_asset_cleanup`
/// after awaiting asset needs to be finalized via `finalize_asset` to get a Gid
pub fn ingestAsset(self: *AssetSystem, data: KindData, target_path: []const u8) !std.Io.Future(types.IngestError!Entry) {
    const path = try makeTargetPath(self, target_path);
    const loc = self.loader.newLocation(self.alloc, path) catch |e| { self.alloc.free(path); return e; };
    errdefer self.loader.removeLocation(self.alloc, loc) catch {};

    const location: types.IngestLocation = .{
        .loc = loc,
        .path = self.loader.locations.items[loc].path,
        .prefix_dir = self.loader.locations_path_prefix,
    };

    return try switch (data) {
        .texture => |d| self.io.concurrent(TextureRegistry.ingestTexture, .{ self.io, location, d }),
        .sampled_texture => |d| self.io.concurrent(TextureRegistry.ingestSampledTexture, .{ &self.texture_reg, &self.loader, d }),
        .material_schema => unreachable,
        .material_instance => unreachable,
        .geometry => unreachable,
        .mesh => unreachable,
        .model => unreachable,
        .skeleton => unreachable,
    };
}

/// only to be called on an entry returned from `ingest_asset` to cancel the ingestion
pub fn ingestAssetCleanup(self: *AssetSystem, e: Entry) !void {
    const loc = e.cold_asset.location;
    try self.loader.removeLocation(self.alloc, loc);
}

pub const GidError = error{
    IdOutOfRange,
    GenerationMismatch,
};

pub const CommandBuffer = struct {
    const Op = struct {
        delta: i32,
        dense: u32,
        kind: Kind,
    };

    alloc: Allocator,
    ops: std.AutoHashMapUnmanaged(Gid, Op) = .empty,
    visited: std.AutoHashMapUnmanaged(Gid, void) = .empty, // per-walk dedup scratch
    order: std.ArrayList(Gid) = .empty, // post-order, deduped across walks

    pub fn init(alloc: Allocator) CommandBuffer {
        return .{
            .alloc = alloc,
        };
    }

    pub fn deinit(self: *CommandBuffer) void {
        self.ops.deinit(self.alloc);
        self.visited.deinit(self.alloc);
        self.order.deinit(self.alloc);
    }

    pub fn request(self: *CommandBuffer, system: *const AssetSystem, gid: Gid) (GidError || error{ OutOfMemory })!void {
        try self.walk(system, gid, 1);
    }

    pub fn release(self: *CommandBuffer, system: *const AssetSystem, gid: Gid) (GidError || error{ OutOfMemory })!void {
        try self.walk(system, gid, -1);
    }

    fn walk(self: *CommandBuffer, system: *const AssetSystem, gid: Gid, step: i32) (GidError || error{ OutOfMemory })!void {
        defer self.visited.clearRetainingCapacity();
        try self.walkRec(system.entries.slice(), gid, step);
    }

    fn walkRec(self: *CommandBuffer, slice: std.MultiArrayList(Entry).Slice, gid: Gid, step: i32) (GidError || error{ OutOfMemory })!void {
        if (self.visited.contains(gid)) return;

        if (gid.slot >= slice.len) return GidError.IdOutOfRange;
        if (slice.items(.generation)[gid.slot] != gid.gen) return GidError.GenerationMismatch;

        try self.visited.put(self.alloc, gid, {});

        const dense = slice.items(.dense)[gid.slot];
        const gop = try self.ops.getOrPut(self.alloc, gid);
        const is_new = !gop.found_existing;
        if (is_new) {
            gop.value_ptr.* = .{
                .delta = 0,
                .dense = dense,
                .kind = slice.items(.kind)[gid.slot],
            };
        }
        gop.value_ptr.delta += step;

        var deps = &slice.items(.deps)[gid.slot];
        for (deps.gids.items(), 0..) |dep, ix| {
            if (!deps.necessary.isSet(ix)) continue;

            try self.walkRec(slice, dep, step);
        }

        if (is_new) try self.order.append(self.alloc, gid);
    }
};

pub fn submit(self: *AssetSystem, gc: *const base.GraphicsCtx, destroy_queue: *base.DestroyQueue, transport: *base.Transport, cmds: *CommandBuffer) !void {
    const slice = self.entries.slice();
    const cold_assets = slice.items(.cold_asset);
    const deps = slice.items(.deps);
    const dense = slice.items(.dense);
    const kind = slice.items(.kind);

    var it = cmds.ops.iterator();
    while (it.next()) |kv| {
        const op = kv.value_ptr;
        if (op.dense != std.math.maxInt(u32)) continue;
        if (op.delta == 0) continue;
        std.debug.assert(op.delta > 0); // implies a double release

        op.dense = switch (op.kind) {
            .texture => try self.texture_reg.newTexture(),
            .sampled_texture => try self.texture_reg.newSampledTexture(gc, destroy_queue),
            .material_schema => unreachable,
            .material_instance => unreachable,
            .geometry => unreachable,
            .mesh => unreachable,
            .model => unreachable,
            .skeleton => unreachable,
        };

        dense[kv.key_ptr.slot] = op.dense;
    }

    var resolve_buf: std.ArrayList(resolver.ResolvedEntry) = .empty;
    defer resolve_buf.deinit(self.alloc);

    for (cmds.order.items) |gid| {
        const op = cmds.ops.get(gid).?;
        if (op.delta < 0) {
            const delta: u32 = @intCast(-op.delta);
            if (try switch (op.kind) {
                .texture => self.texture_reg.releaseTexture(destroy_queue, transport, op.dense, delta),
                .sampled_texture => self.texture_reg.releaseSampledTexture(gc, destroy_queue, op.dense, delta),
                .material_schema => unreachable,
                .material_instance => unreachable,
                .geometry => unreachable,
                .mesh => unreachable,
                .model => unreachable,

                .skeleton => unreachable,
            } == .released) {
                dense[gid.slot] = std.math.maxInt(u32);
            }
        } else if (op.delta > 0) {
            const delta: u32 = @intCast(op.delta);
            const cold = &cold_assets[gid.slot];

            resolve_buf.clearRetainingCapacity();
            for (deps[gid.slot].gids.items()) |dep_gid| {
                try resolve_buf.append(self.alloc, .{
                    .gid = dep_gid,
                    .kind = kind[dep_gid.slot],
                    .dense = dense[dep_gid.slot],
                });
            }

            try switch (op.kind) {
                .texture => self.texture_reg.acquireTexture(gc, transport, &self.loader, resolve_buf.items, cold, op.dense, delta),
                .sampled_texture => self.texture_reg.acquireSampledTexture(gc, transport, &self.loader, resolve_buf.items, cold, op.dense, delta),
                .material_schema => unreachable,
                .material_instance => unreachable,
                .geometry => unreachable, // need to look up rdeps, and dispatch the corresponding patch calls for them - need to check dense
                .mesh => unreachable, // loads just the MeshDesc&LODEntries
                .model => unreachable,

                .skeleton => unreachable,
            };
        } else continue;
    }

    cmds.ops.clearRetainingCapacity();
    cmds.order.clearRetainingCapacity();
}

const ManifestEntry = struct {
    const Dep = struct {
        gid: Gid,
        necessary: bool,
    };

    name: []const u8,

    gid: Gid,
    kind: Kind,

    cold_asset: Loader.ColdAsset,

    deps: []Dep,
    rdeps: []Gid,
};

const Manifest = struct {
    entries: []ManifestEntry = &.{},
    loader: Loader.Manifest,
};

pub fn save_manifest(self: *const AssetSystem, jws: *std.json.Stringify) !void {
    try jws.beginObject();

    try jws.objectFieldRaw("\"entries\"");
    try jws.beginArray();
    const slice = self.entries.slice();
    for (0.., slice.items(.generation), slice.items(.name), slice.items(.kind), slice.items(.cold_asset), slice.items(.deps), slice.items(.rdeps)) |i, gen, name, kind, cold_asset, *deps, *rdeps| {
        if (self.entries.get(i).isNone()) continue;

        try jws.objectFieldRaw("\"name\"");
        try jws.write(name);

        try jws.objectFieldRaw("\"gid\"");
        try jws.write(Gid{ .gen = gen, .slot = @intCast(i) });

        try jws.objectFieldRaw("\"kind\"");
        try jws.write(kind);

        try jws.objectFieldRaw("\"cold_asset\"");
        try jws.write(cold_asset);

        try jws.objectFieldRaw("\"deps\"");
        try jws.beginArray();
        for (0.., deps.gids.items()) |necessary_ix, gid| {
            try jws.beginObject();

            try jws.objectFieldRaw("\"gid\"");
            try jws.write(gid);

            try jws.objectFieldRaw("\"necessary\"");
            try jws.write(deps.necessary.isSet(necessary_ix));

            try jws.endObject();
        }
        try jws.endArray();

        try jws.objectFieldRaw("\"rdeps\"");
        try jws.write(rdeps.items());
    }
    try jws.endArray();

    try jws.objectFieldRaw("\"loader\"");
    try jws.write(self.loader);

    try jws.endObject();
}

test {
    std.testing.refAllDecls(@This());
}
