const std = @import("std");
const base = @import("base");
const Mesh = @import("../Mesh.zig");
const types = @import("Types.zig");
const resolver = @import("resolver.zig");
const Loader = @import("Loader.zig");
const Gid = types.Gid;

const Skin = @import("../Skin.zig");
const Allocator = std.mem.Allocator;
const ModelRegistry = @This();

pub const SkeletonData = struct {
    gid: Gid,
    dense: u32,
    skins: []Skin,
};

const Model = struct {
    meshes: std.MultiArrayList(struct {
        gid: Gid,
        dense: u32,
        transform: base.zmath.Mat,
        skin: u32 = std.math.maxInt(u32),
    }) = .empty,

    skeleton: ?SkeletonData = null,
};

const ModelBlob = struct {
    meshes: []struct {
        gid: Gid,
        transform: base.zmath.Mat,
        skin: u32,
    },
    skeleton: ?struct {
        gid: Gid,
        skins: []Skin,
    },
};

pub const IngestModel = struct {
    pub const MeshData = struct {
        gid: Gid,
        transform: base.zmath.Mat,
        skin: u32,
    };

    meshes: []const MeshData,
    skeleton: ?struct {
        gid: Gid,
        skins: []const Skin,
    },
};

models: std.ArrayList(Model) = .empty,
free: std.ArrayList(u32) = .empty,

pub fn init() ModelRegistry {
    return .{};
}

pub fn deinit(self: *ModelRegistry, alloc: Allocator) void {
    for (self.models.items) |*model| {
        model.meshes.deinit(alloc);

        if (model.skeleton) |*skeleton| {
            for (skeleton.skins) |*skin| skin.deinit(alloc);
            alloc.free(skeleton.skins);
        }
    }

    self.models.deinit(alloc);
    self.free.deinit(alloc);
}

pub fn new(self: *ModelRegistry, alloc: Allocator) !u32 {
    if (self.free.pop()) |free| {
        self.models.items[free] = .{};
        return free;
    }

    try self.models.append(alloc, .{});
    return @intCast(self.models.items.len - 1);
}

pub fn acquire(self: *ModelRegistry, alloc: Allocator, io: std.Io, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, id: u32) !void {
    const model = &self.models.items[id];
    const data = try loader.load(io, cold);
    defer loader.unload(io, cold);

    const parsed_blob = try std.json.parseFromSlice(ModelBlob, alloc, data, .{});
    defer parsed_blob.deinit();
    const blob = parsed_blob.value;

    try model.meshes.ensureTotalCapacity(alloc, blob.meshes.len);
    model.meshes.shrinkRetainingCapacity(0);

    for (blob.meshes) |mesh| {
        const kind, const dense = resolver.lookup(resolved, mesh.gid) orelse return error.InvalidMeshGid;
        if (kind != .mesh) return error.InvalidMeshKind;

        model.meshes.appendAssumeCapacity(.{
            .gid = mesh.gid,
            .dense = dense,
            .transform = mesh.transform,
            .skin = mesh.skin,
        });
    }

    if (blob.skeleton) |skeleton| {
        const kind, const dense = resolver.lookup(resolved, skeleton.gid) orelse return error.InvalidSkeletonGid;
        if (kind != .skeleton) return error.InvalidSkeletonKind;

        const skins = try alloc.alloc(Skin, skeleton.skins.len);
        var skins_initialized: usize = 0;
        errdefer for (skins[0..skins_initialized]) |*skin| skin.deinit(alloc);
        errdefer alloc.free(skins);

        for (skeleton.skins, skins) |source, *dest| {
            const skeleton_node_indices = try alloc.dupe(u32, source.skeleton_node_indices);
            errdefer alloc.free(skeleton_node_indices);

            const inverse_bind_matrices = try alloc.dupe(base.zmath.Mat, source.inverse_bind_matrices);
            errdefer alloc.free(inverse_bind_matrices);

            dest.* = .{
                .skeleton_node_indices = skeleton_node_indices,
                .inverse_bind_matrices = inverse_bind_matrices,
            };
            skins_initialized += 1;
        }

        model.skeleton = .{
            .gid = skeleton.gid,
            .dense = dense,
            .skins = skins,
        };
    }
}

pub fn release(self: *ModelRegistry, alloc: Allocator, id: u32) !void {
    const model = &self.models.items[id];

    model.meshes.deinit(alloc);
    if (model.skeleton) |*s| {
        for (s.skins) |*skin| skin.deinit(alloc);
        alloc.free(s.skins);
    }
    model.* = .{};

    try self.free.append(alloc, id);
}

pub fn ingest(io: std.Io, alloc: Allocator, location: types.IngestLocation, data: IngestModel) types.IngestError!types.Entry {
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

    try stringify.write(data);
    try file_writer.flush();

    var entry: types.Entry = .{
        .generation = 0,
        .kind = .model,
        .cold_asset = .{
            .location = location.loc,
            .offset = 0,
            .size = file_writer.logicalPos(),
        },
    };

    const total = data.meshes.len + (if (data.skeleton) |_| @as(usize, 1) else 0);
    try entry.deps.necessary.ensureCapacity(alloc, total);
    try entry.deps.gids.ensureCapacity(alloc, total);

    for (0.., data.meshes) |i, mesh| {
        entry.deps.necessary.setAssumeCapacity(i);
        entry.deps.gids.addOneAssumeCapacity().* = mesh.gid;
    }

    if (data.skeleton) |s| {
        entry.deps.necessary.setAssumeCapacity(data.meshes.len);
        entry.deps.gids.addOneAssumeCapacity().* = s.gid;
    }

    return entry;
}

pub fn getMeshGids(self: *ModelRegistry, id: u32) []const Gid {
    return self.models.items[id].meshes.items(.gid);
}

pub fn getMeshDenseIds(self: *ModelRegistry, id: u32) []const u32 {
    return self.models.items[id].meshes.items(.dense);
}

pub fn getMeshTransforms(self: *ModelRegistry, id: u32) []const base.zmath.Mat {
    return self.models.items[id].meshes.items(.transform);
}

pub fn getMeshSkins(self: *ModelRegistry, id: u32) []const u32 {
    return self.models.items[id].meshes.items(.skin);
}

pub fn getSkeleton(self: *ModelRegistry, id: u32) ?SkeletonData {
    return self.models.items[id].skeleton;
}
