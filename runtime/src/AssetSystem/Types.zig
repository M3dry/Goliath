const std = @import("std");
const base = @import("base");

const SmallBuffer = base.util.SmallBuffer;
const SmallBitSet = base.util.SmallBitset;

const ColdAsset = @import("Loader.zig").ColdAsset;
const TextureRegistry = @import("TextureRegistry.zig");

pub const Gid = packed struct(u64) {
    gen: u32,
    slot: u32,

    pub fn jsonParse(allocator: std.mem.Allocator, source: anytype, options: std.json.ParseOptions) std.json.ParseError(@TypeOf(source.*))!Gid {
        const pair = try std.json.innerParse([2]u32, allocator, source, options);
        return .{
            .gen = pair[0],
            .slot = pair[1],
        };
    }

     pub fn jsonStringify(self: *const Gid, jws: anytype) std.json.Stringify.Error!void {
         try jws.beginArray();
         try jws.write(self.gen);
         try jws.write(self.slot);
         try jws.endArray();
     }
};

pub const Kind = enum {
    texture,
    sampled_texture,
    material_schema, // reflection data on how to read a material_instance + shader
    material_instance, // binary blob + deps: (n x sampled_texture + material schema)
    geometry,
    mesh,
    model, // n x (mesh + transform + ?(skeleton + skin(s))) - skin not stored on mesh because skins are tied to a specific skeleton

    skeleton, // contains animations - no need to have animations be a separate asset since they're specific to a skeleton
};

pub const AcquireReturn = enum {
    load,
    incr,
};

pub const ReleaseReturn = enum {
    kept,
    released,
};

// fits into two cachelines
pub const Deps = struct {
    necessary: SmallBitSet(13) = .empty,
    gids: SmallBuffer(Gid, 13) = .empty,
};

pub const Entry = struct {
    name: []const u8 = &.{},

    generation: u32,
    dense: u32 = std.math.maxInt(u32), // == maxInt(u32) => not loaded into registry, == maxInt(u32) - 1 => entry deleted/none

    kind: Kind,

    cold_asset: ColdAsset,

    deps: Deps = .{},
    rdeps: SmallBuffer(Gid, 8) = .empty,

    pub const none: Entry = .{
        .generation = 0,
        .dense = std.math.maxInt(u32) - 1,
        .kind = .texture,
        .cold_asset = .{
            .location = std.math.maxInt(u32),
            .offset = 0,
            .size = 0,
        },
    };

    pub fn isNone(self: Entry) bool {
        return self.dense == none.dense;
    }
};

pub const IngestLocation = struct {
    loc: u32,
    path: []const u8,
    prefix_dir: std.Io.Dir,
};

pub const IngestError = std.mem.Allocator.Error || std.Io.Cancelable || std.Io.File.OpenError || std.Io.Writer.Error || std.Io.File.Writer.Error;

pub const PatchEnum = enum {
    geometry_to_mesh,
    material_instance_to_mesh,
};

pub const Patch = union(PatchEnum) {
    geometry_to_mesh: struct {
        patch_gid: Gid,
        patch_dense: u32,
        target_mesh: Gid,
    },
    material_instance_to_mesh: struct {
        upload_generation: u64,
        schema_gid: Gid,
        patch_gid: Gid,
        patch_dense: u32,
        target_mesh: Gid,
    },

    pub fn patch(self: Patch) struct {Gid, u32} {
        return switch (self) {
            .geometry_to_mesh => |d| .{ d.patch_gid, d.patch_dense },
            .material_instance_to_mesh => |d| .{ d.patch_gid, d.patch_dense },
        };
    }

    pub fn target(self: Patch) Gid {
        return switch (self) {
            .geometry_to_mesh => |d| d.target_mesh,
            .material_instance_to_mesh => |d| d.target_mesh,
        };
    }
};

pub const PatcherVariant = enum {
    mesh
};

pub fn Patcher(comptime variant: PatcherVariant) type {
    const MeshVariant = struct {
        const Self = @This();

        target_mesh: Gid,

        patches: *std.ArrayList(Patch),
        alloc: std.mem.Allocator,

        pub fn addGeometry(self: Self, patch: struct{Gid, u32}) !void {
            try self.patches.append(self.alloc, .{
                .geometry_to_mesh = .{
                    .patch_gid = patch.@"0",
                    .patch_dense = patch.@"1",
                    .target_mesh = self.target_mesh,
                }
            });
        }
    };

    return switch (variant) {
        .mesh => MeshVariant,
    };
}
