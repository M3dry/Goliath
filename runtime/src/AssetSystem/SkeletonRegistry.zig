const std = @import("std");
const base = @import("base");
const Mesh = @import("../Mesh.zig");
const types = @import("Types.zig");
const resolver = @import("resolver.zig");
const Loader = @import("Loader.zig");
const Gid = types.Gid;

const Skeleton = @import("../Skeleton.zig");
const Animation = @import("../Animation.zig");

const Allocator = std.mem.Allocator;
const SkeletonRegistry = @This();

data: std.MultiArrayList(struct {
    skeleton_joint_names: [][]u8 = &.{},
    skeleton: Skeleton = .{
        .bind_local_transforms = &.{},
        .parents = &.{},
    },
    animation_names: [][]u8 = &.{},
    animations: []Animation = &.{},

    value_alloc: ?*std.heap.ArenaAllocator = null,
}) = .empty,
free: std.ArrayList(u32) = .empty,

pub const IngestData = struct {
    joint_names: []const []const u8,
    animation_names: []const []const u8,
    skeleton: Skeleton,
    animations: []const Animation,
};

const SaveDataBlob = struct {
    joint_names: []const []const u8,
    animation_names: []const []const u8,
    skeleton: Skeleton,
    animations: []const Animation,
};

const DataBlob = struct {
    joint_names: [][]u8,
    animation_names: [][]u8,
    skeleton: Skeleton,
    animations: []Animation,
};

pub fn init() SkeletonRegistry {
    return .{};
}

pub fn deinit(self: *SkeletonRegistry, alloc: Allocator) void {
    const slice = self.data.slice();
    for (slice.items(.value_alloc)) |value_alloc| {
        if (value_alloc) |vall| {
            vall.deinit();
            alloc.destroy(vall);
        }
    }

    self.data.deinit(alloc);
    self.free.deinit(alloc);
}

pub fn new(self: *SkeletonRegistry, alloc: Allocator) !u32 {
    if (self.free.pop()) |free| {
        self.data.set(free, .{});
        return free;
    }

    try self.data.append(alloc, .{});
    return @intCast(self.data.len - 1);
}

pub fn acquire(self: *SkeletonRegistry, alloc: Allocator, io: std.Io, loader: *Loader, cold: *const Loader.ColdAsset, id: u32) !void {
    const slice = self.data.slice();
    const data = try loader.load(io, cold);
    defer loader.unload(io, cold);

    const parsed_blob = try std.json.parseFromSlice(DataBlob, alloc, data, .{});
    errdefer parsed_blob.deinit();
    const blob = parsed_blob.value;

    slice.items(.skeleton_joint_names)[id] = blob.joint_names;
    slice.items(.skeleton)[id] = blob.skeleton;
    slice.items(.animation_names)[id] = blob.animation_names;
    slice.items(.animations)[id] = blob.animations;
    slice.items(.value_alloc)[id] = parsed_blob.arena;
}

pub fn release(self: *SkeletonRegistry, alloc: Allocator, id: u32) !void {
    var slice = self.data.slice();

    const value_alloc = slice.items(.value_alloc)[id];
    if (value_alloc) |vall| {
        vall.deinit();
        alloc.destroy(vall);
    }

    slice.set(id, .{});

    try self.free.append(alloc, id);
}

pub fn ingest(io: std.Io, location: types.IngestLocation, data: IngestData) types.IngestError!types.Entry {
    const file = try location.prefix_dir.createFile(io, location.path, .{});
    defer file.close(io);

    var writer_buffer: [1024]u8 = undefined;
    var file_writer = file.writer(io, &writer_buffer);
    var stringify = std.json.Stringify{
        .writer = &file_writer.interface,
        .options = .{
            .whitespace = .indent_2,
        },
    };

    try stringify.write(SaveDataBlob{
        .joint_names = data.joint_names,
        .animation_names = data.animation_names,
        .skeleton = data.skeleton,
        .animations = data.animations,
    });
    try file_writer.flush();

    return .{
        .generation = 0,
        .kind = .skeleton,
        .cold_asset = .{
            .location = location.loc,
            .offset = 0,
            .size = file_writer.logicalPos(),
        },
    };
}

pub inline fn getSkeleton(self: *SkeletonRegistry, id: u32) Skeleton {
    return self.data.items(.skeleton)[id];
}

pub inline fn getAnimations(self: *SkeletonRegistry, id: u32) []const Animation {
    return self.data.items(.animations)[id];
}

test {
    std.testing.refAllDecls(@This());
}
