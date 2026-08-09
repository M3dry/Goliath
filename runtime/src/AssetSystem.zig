const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;
const SmallBuffer = base.util.SmallBuffer;

const TextureRegistry = @import("AssetSystem/TextureRegistry.zig");

const Loader = @import("AssetSystem/Loader.zig");

pub const Gid = packed struct(u64) {
    gen: u32,
    slot: u32,
};

// mesh get loaded at init into GPU buffers, omitting the geometry and material
pub const Kind = enum(u8) {
    texture,
    sampled_texture, // sampler description + texture
    material_schema, // reflection data on how to read a material_instance
    material_instance, // n x sampled_texture + material schema
    geometry, // pure geometry buffer with offsets
    mesh, // n x (geometry + material instance) - for each LOD (LODs aren't shared, thus no asset handle for them)
    model, // n x (mesh + transform + ?(skeleton + skin(s))) - skin not stored on mesh because skins are tied to a specific skeleton

    skeleton, // contains animations - no need to have animations be a separate asset since they're specific to a skeleton
};

const Entry = struct {
    name: []const u8 = &.{},

    generation: u32,
    dense: u32, // == maxInt(u32) => not loaded into registry

    kind: Kind,

    cold_asset: Loader.ColdAsset,

    // 8 * sizeof(Gid) = 64B => can fit deps into a cache line
    deps: SmallBuffer(Gid, 8),
    rdeps: SmallBuffer(Gid, 8),
};

const AssetSystem = @This();

alloc: Allocator,
loader: Loader,

entries: std.MultiArrayList(Entry) = .empty,
free_entries: std.ArrayList(u32) = .empty,

texture_reg: TextureRegistry,
// registires for the other asset kinds...

pub fn init(io: std.Io, gc: *const base.GraphicsCtx, alloc: Allocator, asset_root: []const u8) !AssetSystem {
    var self: AssetSystem = .{
        .alloc = alloc,
        .loader = try .init(io, asset_root),
        .texture_reg = try .init(gc, alloc),
    };

    // TODO: populate locations - implementation
    self.loader.populate_locations();

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

    pub fn init(alloc: Allocator) CommandBuffer {
        return .{
            .alloc = alloc,
        };
    }

    pub fn deinit(self: *CommandBuffer) void {
        self.ops.deinit(self.alloc);
        self.visited.deinit(self.alloc);
    }

    pub fn request(self: *CommandBuffer, system: *const AssetSystem, gid: Gid) error{GidError, OutOfMemory}!void {
        try self.walk(system, gid, 1);
    }

    pub fn release(self: *CommandBuffer, system: *const AssetSystem, gid: Gid) error{GidError, OutOfMemory}!void {
        try self.walk(system, gid, -1);
    }

    fn walk(self: *CommandBuffer, system: *const AssetSystem, gid: Gid, step: i32) error{GidError, OutOfMemory}!void {
        defer self.visited.clearRetainingCapacity();
        try self.walkRec(system.entries.slice(), gid, step);
    }

    fn walkRec(self: *CommandBuffer, slice: std.MultiArrayList(Entry).Slice, gid: Gid, step: i32) error{GidError, OutOfMemory}!void {
        if (self.visited.contains(gid)) return;

        if (gid.slot >= slice.len) return GidError.IdOutOfRange;
        if (slice.items(.generation)[gid.slot] != gid.gen) return GidError.GenerationMismatch;

        try self.visited.put(self.alloc, gid, {});

        const dense = slice.items(.dense)[gid.slot];
        const gop = try self.ops.getOrPut(self.alloc, gid);
        if (!gop.found_existing) {
            gop.value_ptr.* = .{
                .delta = 0,
                .dense = dense,
                .kind = slice.items(.kind)[gid.slot],
            };
        }
        gop.value_ptr.delta += step;

        for (slice.items(.deps)[gid.slot].items()) |dep| {
            try self.walkRec(slice, dep, step);
        }
    }
};

pub fn submit(self: *AssetSystem, cmds: *CommandBuffer) !void {
    _ = self;

    var it = cmds.ops.iterator();
    while (it.next()) |kv| {
        const op = kv.value_ptr.*;
        if (op.delta == 0) continue;

        // TODO(registries): dispatch on op.kind to the per-kind registry:
        //   op.delta > 0 && op.fresh → allocate dense id + queue upload
        //   op.delta > 0              → refcount++ only
        //   op.delta < 0              → refcount--; unload when it hits 0
        //                              (fence-deferred via DestroyQueue)
        //   op.delta < 0 && !op.fresh → caller bug: releasing a non-resident asset
    }

    cmds.ops.clearRetainingCapacity();
}

test {
    std.testing.refAllDecls(@This());
}
