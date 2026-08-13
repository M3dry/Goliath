const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;
const SmallBuffer = base.util.SmallBuffer;
const SmallBitSet = base.util.SmallBitset;

const TextureRegistry = @import("AssetSystem/TextureRegistry.zig");
const Loader = @import("AssetSystem/Loader.zig");
const resolver = @import("AssetSystem/resolver.zig");

const types = @import("AssetSystem/Types.zig");
const Gid = types.Gid;
const Kind = types.Kind;

// fits into two cachelines
const Deps = struct {
    deps_necessary: SmallBitSet(13),
    gids: SmallBuffer(Gid, 13),
};

const Entry = struct {
    name: []const u8 = &.{},

    generation: u32,
    dense: u32, // == maxInt(u32) => not loaded into registry

    kind: Kind,

    cold_asset: Loader.ColdAsset,

    deps: Deps,
    rdeps: SmallBuffer(Gid, 8),
};

const AssetSystem = @This();

alloc: Allocator,
loader: Loader,

entries: std.MultiArrayList(Entry) = .empty,
free_entries: std.ArrayList(u32) = .empty,

texture_reg: TextureRegistry,
// registires for the other asset kinds...

pub fn init(io: std.Io, gc: *const base.GraphicsCtx, transport: *base.Transport, alloc: Allocator, asset_root: []const u8) !AssetSystem {
    var self: AssetSystem = .{
        .alloc = alloc,
        .loader = try .init(io, asset_root),
        .texture_reg = try .init(gc, transport, alloc),
    };

    // TODO: populate locations - implementation
    self.loader.populate_locations();

    // UPDATE NOTE: let's not upload MeshDesc and LODEntries at load,
    //              instead acquire(gid of kind mesh & model) populates the MeshDesc and lod entries gpu buffers
    //              this makes it possible to say upload only meshes for the current scenes, and the GPU requests from them - since we already do batching via the command buffer we can do the whole mesh desc upload in one pass
    //              then calling acquire(gid of kind geometry) will upload the geometry and patch up all of it's rdeps that are on the GPU - if afterhand a meshdesc gets added, it points it's lods to the geometry if it's uploaded
    // CONSEQUENCE: need to a "contract" param in the deps that says if that dependency is a hard one or not - i.e. do we need to immediately load the asset - mesh's geometry deps will be marked as soft eg
    // TODO: upload metadata to the GPU (Mesh&LOD descs)
    //       this opens the possibility of the GPU requesting assets based on visibility
    //       uploaded LOD descs have -1 for material schema and instance, once geometry is loaded it gets patched to the right indices along with the geometry pointer
    //          thus stable GPU side material schema and instance indices aren't needed

    return self;
}

pub fn deinit(self: *AssetSystem, destroy_queue: *base.DestroyQueue) void {
    self.loader.deinit(self.alloc);

    self.entries.deinit(self.alloc);
    self.free_entries.deinit(self.alloc);

    self.texture_reg.deinit(destroy_queue);
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

    pub fn request(self: *CommandBuffer, system: *const AssetSystem, gid: Gid) error{ GidError, OutOfMemory }!void {
        try self.walk(system, gid, 1);
    }

    pub fn release(self: *CommandBuffer, system: *const AssetSystem, gid: Gid) error{ GidError, OutOfMemory }!void {
        try self.walk(system, gid, -1);
    }

    fn walk(self: *CommandBuffer, system: *const AssetSystem, gid: Gid, step: i32) error{ GidError, OutOfMemory }!void {
        defer self.visited.clearRetainingCapacity();
        try self.walkRec(system.entries.slice(), gid, step);
    }

    fn walkRec(self: *CommandBuffer, slice: std.MultiArrayList(Entry).Slice, gid: Gid, step: i32) error{ GidError, OutOfMemory }!void {
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

        const deps: Deps = slice.items(.deps)[gid.slot];
        for (deps.gids.items(), 0..) |dep, ix| {
            if (!deps.deps_necessary.isSet(ix)) continue;

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
            .texture => try self.texture_reg.new_texture(),
            .sampled_texture => try self.texture_reg.new_sampled_texture(gc, destroy_queue),
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
                .texture => self.texture_reg.release_texture(destroy_queue, transport, op.dense, delta),
                .sampled_texture => self.texture_reg.release_sampled_texture(gc, destroy_queue, op.dense, delta),
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
                .texture => self.texture_reg.acquire_texture(gc, transport, &self.loader, resolve_buf.items, cold, op.dense, delta),
                .sampled_texture => self.texture_reg.acquire_sampled_texture(gc, transport, &self.loader, resolve_buf.items, cold, op.dense, delta),
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

test {
    std.testing.refAllDecls(@This());
}
