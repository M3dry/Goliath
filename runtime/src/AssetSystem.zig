const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;

pub const TextureRegistry = @import("AssetSystem/TextureRegistry.zig");
pub const MeshRegistry = @import("AssetSystem/MeshRegistry.zig");
pub const GeometryRegistry = @import("AssetSystem/GeometryRegistry.zig");
pub const MaterialRegistry = @import("AssetSystem/MaterialRegistry.zig");
const Loader = @import("AssetSystem/Loader.zig");
const resolver = @import("AssetSystem/resolver.zig");

const types = @import("AssetSystem/Types.zig");
pub const Gid = types.Gid;
pub const Kind = types.Kind;
const Entry = types.Entry;

const AssetSystem = @This();

alloc: Allocator,
loader: Loader,
threaded_io: std.Io.Threaded,

entries: std.MultiArrayList(Entry) = .empty,
free_entries: std.ArrayList(u32) = .empty,

texture_reg: TextureRegistry,
mesh_reg: MeshRegistry,
geometry_reg: GeometryRegistry,
material_reg: MaterialRegistry,
// registires for the other asset kinds...

pending_patches: std.ArrayList(types.Patch) = .empty,

pub fn io(self: *AssetSystem) std.Io {
    return self.threaded_io.io();
}

pub const ManifestReader = union(enum) {
    reader: *std.Io.Reader,
    default: struct {
        path_prefix: []const u8,
    },
};

/// allocator needs to be thread safe
pub fn init(ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .transport, .graphics_queue }), alloc: Allocator, tmp_alloc: Allocator, manifest_reader: ManifestReader) !AssetSystem {
    var threaded_io = std.Io.Threaded.init(alloc, .{
        .disable_memory_mapping = false,
        .concurrent_limit = .unlimited,
    });
    errdefer threaded_io.deinit();
    const init_io = threaded_io.io();

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

    var loader = try Loader.init(init_io, alloc, &manifest.loader);
    errdefer loader.deinit(alloc, init_io);

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

    var texture_reg = try TextureRegistry.init(ctx, alloc);
    errdefer texture_reg.deinitNow(.from(ctx));

    var mesh_reg = MeshRegistry.init();
    errdefer mesh_reg.deinitNow(.from(ctx), alloc);

    var geometry_reg = GeometryRegistry.init();
    errdefer geometry_reg.deinitNow(.from(ctx), alloc);

    var material_reg = MaterialRegistry.init();
    errdefer material_reg.deinitNow(.from(ctx), alloc);

    return .{
        .alloc = alloc,
        .loader = loader,
        .threaded_io = threaded_io,
        .entries = entries,
        .free_entries = free_entries,
        .texture_reg = texture_reg,
        .mesh_reg = mesh_reg,
        .geometry_reg = geometry_reg,
        .material_reg = material_reg,
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

pub fn deinit(self: *AssetSystem, ctx: base.Ctx.Query(&.{ .destroy_queue, .transport })) void {
    self.loader.deinit(self.alloc, self.io());
    self.threaded_io.deinit();

    for (self.entries.items(.name)) |name| {
        self.alloc.free(name);
    }
    self.entries.deinit(self.alloc);
    self.free_entries.deinit(self.alloc);

    self.texture_reg.deinit(.from(ctx));
    self.mesh_reg.deinit(.from(ctx), self.alloc);
    self.geometry_reg.deinit(.from(ctx), self.alloc);
    self.material_reg.deinit(.from(ctx), self.alloc);

    self.pending_patches.deinit(self.alloc);
}

pub const KindData = union(Kind) {
    texture: TextureRegistry.IngestTexture,
    sampled_texture: TextureRegistry.IngestSampledTexture,
    material_schema: MaterialRegistry.IngestSchema,
    material_instance: MaterialRegistry.IngestInstance,
    geometry: GeometryRegistry.IngestGeometry,
    mesh: MeshRegistry.MeshDescBlob,
    model: void,
    skeleton: void,
};

pub fn finalizeAsset(self: *AssetSystem, entry: Entry, dupe_entry_name: bool) !Gid {
    if (entry.isNone()) return error.EntryIsNone;
    const gens = self.entries.items(.generation);

    var e = entry;
    for (e.deps.gids.items()) |gid| {
        if (gid.slot >= self.entries.len) return error.NonExistentGidReferenced;
        if (gens[gid.slot] != gid.gen) return error.StaleGidReferenced;
    }

    if (dupe_entry_name) {
        e.name = try self.alloc.dupe(u8, entry.name);
    }
    errdefer if (dupe_entry_name) self.alloc.free(e.name);

    const gid = if (self.free_entries.pop()) |slot| blk: {
        e.generation = gens[slot] + 1;
        self.entries.set(slot, e);

        break :blk Gid{ .gen = e.generation, .slot = slot };
    } else blk: {
        e.generation = 0;
        try self.entries.append(self.alloc, e);

        break :blk Gid{
            .gen = e.generation,
            .slot = @intCast(self.entries.len - 1),
        };
    };
    errdefer if (self.entries.items(.generation)[gid.slot] == gid.gen) {
        e.deps.necessary.deinit(self.alloc);
        e.deps.gids.deinit(self.alloc);
        e.rdeps.deinit(self.alloc);
        self.entries.set(gid.slot, .none);
    };

    const rdepss = self.entries.items(.rdeps);
    for (e.deps.gids.items()) |dep_gid| {
        const rdeps = &rdepss[dep_gid.slot];
        for (rdeps.items()) |rdep_gid| {
            std.debug.assert(rdep_gid != gid);
        }
        (try rdeps.addOne(self.alloc)).* = gid;
    }

    return gid;
}

fn makeTargetPath(self: *AssetSystem, target_path: []const u8) ![]u8 {
    var target_exists = true;
    var increment: usize = 0;
    var path: []const u8 = &.{};
    var buf: []u8 = &.{};
    defer self.alloc.free(buf);

    while (true) {
        path = if (increment == 0) target_path else blk: {
            if (increment == 1) buf = try self.alloc.alloc(u8, target_path.len + 21); // 20 digits in 2^64 + '-'
            break :blk try std.fmt.bufPrint(buf, "{s}-{}", .{target_path, increment});
        };

        target_exists = true;
        self.loader.locations_path_prefix.access(self.io(), path, .{}) catch |e| switch (e) {
            error.FileNotFound => target_exists = false,
            else => return e,
        };

        if (!target_exists) break;
        increment += 1;
    }

    return self.alloc.dupe(u8, path);

}

/// up to the caller to clean up `data` after awaiting/cancelling the future
/// cleanup after cancelling via `ingest_asset_cleanup`
/// after awaiting asset needs to be finalized via `finalize_asset` to get a Gid
pub fn ingestAsset(self: *AssetSystem, data: KindData, target_path: []const u8) !std.Io.Future(types.IngestError!Entry) {
    const path = try makeTargetPath(self, target_path);
    const loc = self.loader.newLocation(self.alloc, path) catch |e| { self.alloc.free(path); return e; };
    errdefer self.loader.removeLocation(self.alloc, self.io(), loc) catch {};

    const location: types.IngestLocation = .{
        .loc = loc,
        .path = self.loader.locations.items[loc].path,
        .prefix_dir = self.loader.locations_path_prefix,
    };

    const asset_io = self.io();
    const ret = try switch (data) {
        .texture => |d| asset_io.concurrent(TextureRegistry.ingestTexture, .{ asset_io, location, d }),
        .sampled_texture => |d| asset_io.concurrent(TextureRegistry.ingestSampledTexture, .{ self.alloc, asset_io, location, d }),
        .material_schema => |d| asset_io.concurrent(MaterialRegistry.ingestSchema, .{ asset_io, location, d }),
        .material_instance => |d| asset_io.concurrent(MaterialRegistry.ingestInstance, .{ self.alloc, asset_io, location, d }),
        .geometry => |d| asset_io.concurrent(GeometryRegistry.ingest, .{ asset_io, location, d }),
        .mesh => |d| asset_io.concurrent(MeshRegistry.ingest, .{ self.alloc, asset_io, location, d }),
        .model => unreachable,
        .skeleton => unreachable,
    };
    return ret;
}

/// only to be called on an entry returned from `ingest_asset` to cancel the ingestion
pub fn ingestAssetCleanup(self: *AssetSystem, e: *Entry) void {
    const loc = e.cold_asset.location;
    self.loader.removeLocation(self.alloc, self.io(), loc) catch {};

    e.deps.gids.deinit(self.alloc);
    e.deps.necessary.deinit(self.alloc);
    e.rdeps.deinit(self.alloc);
}

pub fn removeAsset(self: *AssetSystem, gid: Gid) void {
    _ = self;
    _ = gid;
}

/// gid of the first non-none entry with the given kind and name
pub fn findEntry(self: *const AssetSystem, kind: Kind, name: []const u8) ?Gid {
    const slice = self.entries.slice();
    for (0.., slice.items(.name), slice.items(.kind)) |i, entry_name, entry_kind| {
        if (entry_kind == kind and std.mem.eql(u8, entry_name, name)) {
            return .{ .gen = slice.items(.generation)[i], .slot = @intCast(i) };
        }
    }
    return null;
}

/// registry index of a requested (acquired) asset; for sampled textures this is
/// the texture pool slot
pub fn denseIndex(self: *const AssetSystem, gid: Gid) u32 {
    return self.entries.items(.dense)[gid.slot];
}

pub fn tick(self: *AssetSystem, ctx: base.Ctx.Query(&.{ .device, .transport, .vma_allocator, .graphics_family, .transport_family, .destroy_queue })) !void {
    const slice = self.entries.slice();
    const dense = slice.items(.dense);
    const kinds = slice.items(.kind);
    const gens = slice.items(.generation);

    var i: usize = 0;
    while (i < self.pending_patches.items.len) {
        const pending = self.pending_patches.items[i];
        {
            const patch_gid, const patch_dense = pending.patch();
            const target_gid = pending.target();
            std.debug.assert(patch_gid.slot < slice.len);
            std.debug.assert(patch_gid.gen == gens[patch_gid.slot]);
            std.debug.assert(patch_dense == dense[patch_gid.slot]);

            std.debug.assert(target_gid.slot < slice.len);
            std.debug.assert(target_gid.gen == gens[target_gid.slot]);
        }

        const remove = switch (pending) {
            .geometry_to_mesh => |patch| blk: {
                std.debug.assert(kinds[patch.patch_gid.slot] == .geometry);
                if (!try self.geometry_reg.isReady(.from(ctx), patch.patch_dense)) break :blk false;

                std.debug.assert(kinds[patch.target_mesh.slot] == .mesh);
                try self.mesh_reg.patch(dense[patch.target_mesh.slot], .{ patch.patch_gid, patch.patch_dense }, &self.geometry_reg, &self.material_reg);

                break :blk true;
            },
            .material_instance_to_mesh => |patch| blk: {
                std.debug.assert(kinds[patch.patch_gid.slot] == .material_instance);

                const schema_dense = self.material_reg.getInstanceSchema(patch.patch_dense);
                const upload_generation = self.material_reg.getUploadGeneration(schema_dense);
                if (upload_generation <= patch.upload_generation) break :blk false;

                std.debug.assert(kinds[patch.target_mesh.slot] == .mesh);
                try self.mesh_reg.patch(dense[patch.target_mesh.slot], .{ patch.patch_gid, patch.patch_dense }, &self.geometry_reg, &self.material_reg);

                break :blk true;
            },
        };

        if (remove) _ = self.pending_patches.swapRemove(i) else i += 1;
    }

    try self.material_reg.tick(.from(ctx));
    try self.texture_reg.tick(.from(ctx));
    try self.mesh_reg.tick(.from(ctx));
}

pub const GidError = error{
    IdOutOfRange,
    GenerationMismatch,
};

// BUG: deinit path doesn't correctly order assets (order for asset deinit path needs to be pre, but currently is post)
//      potential fix: store the root gids in an array, then build the order from this array after collecting the deltas (decide if post or pre based on delta)
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

pub fn submit(self: *AssetSystem, ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .destroy_queue, .transport, .graphics_family, .transport_family }), cmds: *CommandBuffer) !void {
    const slice = self.entries.slice();
    const cold_assets = slice.items(.cold_asset);
    const deps = slice.items(.deps);
    const rdeps = slice.items(.rdeps);
    const dense = slice.items(.dense);
    const kind = slice.items(.kind);

    var it = cmds.ops.iterator();
    while (it.next()) |kv| {
        const op = kv.value_ptr;
        if (op.dense != std.math.maxInt(u32)) continue;
        if (op.delta == 0) continue;
        std.debug.assert(op.delta > 0); // implies a double release

        op.dense = try switch (op.kind) {
            .texture => self.texture_reg.newTexture(),
            .sampled_texture => self.texture_reg.newSampledTexture(.from(ctx)),
            .material_schema => self.material_reg.newSchema(self.alloc),
            .material_instance => self.material_reg.newInstance(self.alloc),
            .geometry => self.geometry_reg.new(self.alloc),
            .mesh => self.mesh_reg.new(self.alloc),
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
            std.debug.assert(op.dense != std.math.maxInt(u32));

            const delta: u32 = @intCast(-op.delta);
            if (try switch (op.kind) {
                .texture => self.texture_reg.releaseTexture(.from(ctx), op.dense, delta),
                .sampled_texture => self.texture_reg.releaseSampledTexture(.from(ctx), op.dense, delta),
                .material_schema => blk: {
                    const status = try self.material_reg.releaseSchema(.from(ctx), self.alloc, op.dense, delta);
                    if (status != .released) break :blk status;

                    for (rdeps[gid.slot].items()) |rdep_gid| {
                        const rdep_dense = dense[rdep_gid.slot];
                        if (rdep_dense == std.math.maxInt(u32)) continue;
                        if (kind[rdep_gid.slot] != .mesh) continue;

                        try self.mesh_reg.patch(rdep_dense, .{ gid, std.math.maxInt(u32) }, &self.geometry_reg, &self.material_reg);
                    }

                    break :blk .released;
                },
                .material_instance => blk: {
                    const status = try self.material_reg.releaseInstance(self.alloc, op.dense, delta);
                    if (status != .released) break :blk status;

                    for (rdeps[gid.slot].items()) |rdep_gid| {
                        const rdep_dense = dense[rdep_gid.slot];
                        if (rdep_dense == std.math.maxInt(u32)) continue;
                        if (kind[rdep_gid.slot] != .mesh) continue;

                        try self.mesh_reg.patch(rdep_dense, .{ gid, std.math.maxInt(u32) }, &self.geometry_reg, &self.material_reg);
                    }

                    break :blk .released;
                },
                .geometry => blk: {
                    const status = try self.geometry_reg.release(.from(ctx), self.alloc, op.dense, delta);
                    if (status != .released) break :blk status;

                    for (rdeps[gid.slot].items()) |rdep_gid| {
                        const rdep_dense = dense[rdep_gid.slot];
                        if (rdep_dense == std.math.maxInt(u32)) continue;
                        if (kind[rdep_gid.slot] != .mesh) continue;

                        try self.mesh_reg.patch(rdep_dense, .{ gid, std.math.maxInt(u32) }, &self.geometry_reg, &self.material_reg);
                    }

                    break :blk .released;
                },
                .mesh => self.mesh_reg.release(self.alloc, op.dense, delta),
                .model => unreachable,

                .skeleton => unreachable,
            } == .released) {
                var i: usize = 0;
                while (i < self.pending_patches.items.len) {
                    if (self.pending_patches.items[i].target() == gid or self.pending_patches.items[i].patch().@"0" == gid) {
                        _ = self.pending_patches.swapRemove(i);
                    } else i += 1;
                }

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

            switch (op.kind) {
                .texture => try self.texture_reg.acquireTexture(.from(ctx), self.io(), &self.loader, cold, op.dense, delta),
                .sampled_texture => try self.texture_reg.acquireSampledTexture(.from(ctx), self.io(), &self.loader, resolve_buf.items, cold, op.dense, delta),
                .material_schema => {
                    if (try self.material_reg.acquireSchema(self.alloc, self.io(), &self.loader, cold, op.dense, delta) == .incr) break;

                    for (rdeps[gid.slot].items()) |rdep_gid| {
                        const rdep_dense = dense[rdep_gid.slot];
                        if (rdep_dense == std.math.maxInt(u32)) continue;
                        if (kind[rdep_gid.slot] != .mesh) continue;

                        // schema is "immediately" uploaded, instance buffer wil == .null_handle which is still valid (no instances uploaded)
                        try self.mesh_reg.patch(rdep_dense, .{ gid, op.dense }, &self.geometry_reg, &self.material_reg);
                    }
                },
                .material_instance => {
                    const upload_generation = try self.material_reg.acquireInstance(self.alloc, self.io(), &self.loader, resolve_buf.items, cold, op.dense, delta) orelse break;

                    for (rdeps[gid.slot].items()) |rdep_gid| {
                        const rdep_dense = dense[rdep_gid.slot];
                        if (rdep_dense == std.math.maxInt(u32)) continue;
                        if (kind[rdep_gid.slot] != .mesh) continue;

                        try self.pending_patches.append(self.alloc, .{
                            .material_instance_to_mesh = .{
                                .upload_generation = upload_generation,
                                .patch_gid = gid,
                                .patch_dense = op.dense,
                                .target_mesh = rdep_gid,
                            },
                        });
                    }
                },
                .geometry => {
                    if (try self.geometry_reg.acquire(.from(ctx), self.alloc, self.io(), &self.loader, cold, op.dense, delta) == .incr) break;

                    for (rdeps[gid.slot].items()) |rdep_gid| {
                        const rdep_dense = dense[rdep_gid.slot];
                        if (rdep_dense == std.math.maxInt(u32)) continue;
                        if (kind[rdep_gid.slot] != .mesh) continue;

                        try self.pending_patches.append(self.alloc, .{
                            .geometry_to_mesh = .{
                                .patch_gid = gid,
                                .patch_dense = op.dense,
                                .target_mesh = rdep_gid,
                            }
                        });
                    }
                },
                .mesh => try self.mesh_reg.acquire(.from(ctx), self.alloc, self.io(), &self.loader, resolve_buf.items, cold, &self.geometry_reg, &self.material_reg, .{
                    .target_mesh = gid,
                    .patches = &self.pending_patches,
                    .alloc = self.alloc,
                }, op.dense, delta),
                .model => unreachable,

                .skeleton => unreachable,
            }
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

        try jws.beginObject();
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

        try jws.endObject();
    }
    try jws.endArray();

    try jws.objectFieldRaw("\"loader\"");
    try jws.write(self.loader);

    try jws.endObject();
}

test {
    std.testing.refAllDecls(@This());
}
