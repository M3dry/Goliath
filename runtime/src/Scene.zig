const std = @import("std");

const base = @import("base");
const Allocator = std.mem.Allocator;
const Mat = base.zmath.Mat;
const AssetSystem = @import("AssetSystem.zig");
const Gid = AssetSystem.Gid;
const Skin = @import("Skin.zig");
const PosePool = @import("PosePool.zig");
const TemplateScene = @import("TemplateScene.zig");

const debug_enabled = true;

const Scene = @This();

const Range = struct {
    start: u32,
    end: u32,

    pub inline fn contains(self: Range, other: Range) bool {
        return self.in(other.start) and self.inLimit(other.end);
    }

    /// returns the merged range of `self` and `other`
    pub inline fn overlap(self: Range, other: Range) ?Range {
        if (self.inLimit(other.start) or self.inLimit(other.end)) {
            return .{
                .start = @min(self.start, other.start),
                .end = @max(self.end, other.end),
            };
        }

        return null;
    }

    pub inline fn in(self: Range, point: u32) bool {
        return self.start <= point and self.end > point;
    }

    pub inline fn inLimit(self: Range, point: u32) bool {
        return self.start <= point and self.end >= point;
    }
};

const DirtyRanges = struct {
    ranges: std.ArrayList(Range) = .empty,

    pub fn add(self: *DirtyRanges, alloc: Allocator, range: Range) !void {
        if (range.start >= range.end) return;

        var merged = range;
        var first: usize = 0;
        while (first < self.ranges.items.len and self.ranges.items[first].end < merged.start) : (first += 1) {}

        if (first == self.ranges.items.len) {
            try self.ranges.append(alloc, merged);
            return;
        }
        if (merged.end < self.ranges.items[first].start) {
            try self.ranges.insert(alloc, first, merged);
            return;
        }

        var end = first;
        while (end < self.ranges.items.len and self.ranges.items[end].start <= merged.end) : (end += 1) {
            merged.start = @min(merged.start, self.ranges.items[end].start);
            merged.end = @max(merged.end, self.ranges.items[end].end);
        }

        // The replacement is never larger than the block it replaces, so it
        // cannot allocate and shifts the tail at most once.
        self.ranges.replaceRangeAssumeCapacity(first, end - first, &.{merged});
    }

    pub fn remove(self: *DirtyRanges, alloc: Allocator, range: Range) !void {
        if (range.start >= range.end) return;

        var first: usize = 0;
        while (first < self.ranges.items.len and self.ranges.items[first].end <= range.start) : (first += 1) {}
        if (first == self.ranges.items.len or self.ranges.items[first].start >= range.end) return;

        var end = first + 1;
        while (end < self.ranges.items.len and self.ranges.items[end].start < range.end) : (end += 1) {}

        const first_range = self.ranges.items[first];
        const last_range = self.ranges.items[end - 1];
        var replacement: [2]Range = undefined;
        var replacement_len: usize = 0;
        if (first_range.start < range.start) {
            replacement[replacement_len] = .{ .start = first_range.start, .end = range.start };
            replacement_len += 1;
        }
        if (last_range.end > range.end) {
            replacement[replacement_len] = .{ .start = range.end, .end = last_range.end };
            replacement_len += 1;
        }

        try self.ranges.replaceRange(alloc, first, end - first, replacement[0..replacement_len]);
    }

    pub fn update(self: *DirtyRanges, alloc: Allocator, from: u32, by: u32, comptime negative: bool) !void {
        if (by == 0) return;

        const boundary = if (negative) from else from + 1;
        var first: usize = 0;
        while (first < self.ranges.items.len and self.ranges.items[first].end <= boundary) : (first += 1) {}
        if (first == self.ranges.items.len) return;

        if (!negative and self.ranges.items[first].start < boundary) {
            const crossing = self.ranges.items[first];
            try self.ranges.ensureUnusedCapacity(alloc, 1);
            self.ranges.items[first].end = boundary;
            self.ranges.insertAssumeCapacity(first + 1, .{
                .start = boundary + by,
                .end = crossing.end + by,
            });
            first += 2;
        }

        // On deletion, the removed interval must already have been subtracted.
        std.debug.assert(!negative or self.ranges.items[first].start >= boundary);

        for (self.ranges.items[first..]) |*range| {
            if (negative) {
                range.start -= by;
                range.end -= by;
            } else {
                range.start += by;
                range.end += by;
            }
        }

        // Closing a deletion gap can make the ranges on either side touch.
        if (negative and first > 0 and first < self.ranges.items.len and
            self.ranges.items[first - 1].end >= self.ranges.items[first].start)
        {
            self.ranges.items[first - 1].end = @max(
                self.ranges.items[first - 1].end,
                self.ranges.items[first].end,
            );
            _ = self.ranges.orderedRemove(first);
        }
    }

    pub fn deinit(self: *DirtyRanges, alloc: Allocator) void {
        self.ranges.deinit(alloc);
    }
};

const Exposed = struct {
    pub const Id = struct {
        gen: u32,
        id: u32,
    };

    stable_counter: u32 = 0,
    data: std.ArrayList(struct {
        gen: u32,
        node_id: u32,
    }) = .empty,
    free: std.ArrayList(u32) = .empty,

    const Self = @This();

    pub inline fn getNodeId(self: *Self, stable_id: Id) ?u32 {
        if (self.data.items.len <= stable_id.id) return null;

        const d = self.data.items[stable_id.id];
        if (d.gen != stable_id.gen) return null;

        return d.node_id;
    }

    pub inline fn expose(self: *Self, alloc: Allocator, node_id: u32) !Id {
        if (self.free.pop()) |id| {
            self.data.items[id].gen +%= 1;
            self.data.items[id].node_id = node_id;

            return .{
                .gen = 0,
                .id = id,
            };
        }

        try self.data.append(alloc, .{
            .gen = 0,
            .node_id = node_id,
        });

        return .{
            .gen = 0,
            .id = @intCast(self.data.items.len - 1),
        };
    }

    pub inline fn unexpose(self: *Self, alloc: Allocator, stable_id: Id) void {
        if (getNodeId(self, stable_id) == null) return;

        self.data.items[stable_id.id].gen +%= 1;
        try self.free.append(alloc, stable_id.id);
    }

    pub inline fn incrNodeIds(self: *Self, above: u32, delta: i32) void {
        for (self.data.items) |*id| {
            if (id.node_id > above) id.node_id = @intCast(@as(i64, id.node_id) + delta);
        }
    }

    pub fn removeNodeRange(self: *Self, alloc: Allocator, range: Range) !void {
        for (0.., self.data.items) |i, *d| {
            if (!range.in(d.node_id)) continue;

            d.gen +%= 1;
            try self.free.append(alloc, @intCast(i));
        }
    }

    pub fn deinit(self: *Self, alloc: Allocator) void {
        self.data.deinit(alloc);
        self.free.deinit(alloc);
    }
};

const Attachments = struct {};

pub const NodeDebug = struct {
    node_name: []const u8,
};

pub const Node = struct {
    mesh: ?struct {
        gid: Gid,
        dense: u32,
    },
    model_gid: ?Gid, // not in `animated` because it signifies the "root"/anchor node of the model, the direct children are model meshes - used for cleanup purposes
    animated: ?struct {
        // dense into ModelRegistry - here the `skin` is stored
        model: u32,
        skin: u32,
        // wires up into an animation system, to be decided yet
        pose: PosePool.PoseId,
        pose_revision: u64,
    },

    parent_transform_relation: union(enum) {
        inherit_transform,
        attachment: u31, // points into `attachments`, still inherits the parent's transform (makes it easier to do dirty tracking, and most of the time what is wanted, otherwise node should be placed as the child of the root)
    },

    local_transform: Mat, // will get stored as TRS in the asset, for runtime we can convert to 4x4 at load once
    world_transform: Mat,

    self: u32,
    /// if equal to `self`, then the node is a root node
    parent: u32,
    child_count: u32,
    subtree_size: u32,

    pub inline fn isRootNode(self: Node) bool {
        return self.self == self.parent;
    }
};

pose_pool: PosePool = .{},

nodes_debug: std.ArrayList(NodeDebug) = .empty,
nodes: std.ArrayList(Node) = .empty, // topo sorted
dirty_transforms: DirtyRanges = .{},

exposed: Exposed = .{},

attachments: Attachments = .{},

pub fn initRoot(self: *Scene, alloc: Allocator) !Exposed.Id {
    if (self.nodes.items.len != 0) return error.SceneNotEmpty;

    try self.nodes_debug.append(alloc, .{
        .node_name = "root",
    });

    try self.nodes.append(alloc, .{
        .mesh = null,
        .model_gid = null,
        .animated = null,
        .parent_transform_relation = .inherit_transform,
        .local_transform = base.zmath.identity(),
        .world_transform = base.zmath.identity(),
        .self = 0,
        .parent = 0,
        .child_count = 0,
        .subtree_size = 0,
    });

    return self.exposed.expose(alloc, 0);
}

pub fn deinit(self: *Scene, alloc: Allocator) void {
    self.pose_pool.deinit(alloc);

    self.nodes_debug.deinit(alloc);
    self.nodes.deinit(alloc);
    self.dirty_transforms.deinit(alloc);
    self.exposed.deinit(alloc);
}

pub fn rebuildDirtyTransforms(self: *Scene) void {
    while (self.dirty_transforms.ranges.pop()) |range| {
        const nodes = self.nodes.items[range.start..range.end];

        for (nodes) |*node| {
            if (node.isRootNode()) {
                node.world_transform = node.local_transform;
                continue;
            }

            const parent = self.nodes.items[node.parent];
            switch (node.parent_transform_relation) {
                .attachment => |att| {
                    _ = att;
                    // TODO:
                    unreachable;
                },
                .inherit_transform => {
                    node.world_transform = base.zmath.mul(node.local_transform, parent.world_transform);
                },
            }
        }
    }
}

/// `tree` expected to be in DFS pre order
/// after calling the tree root will be placed at `resolved target_node id + 1`, this order won't last through subsequent calls to addTree/removeTree
pub fn addTree(self: *Scene, alloc: Allocator, tree: []const Node, target_node: Exposed.Id, tree_debug: []const NodeDebug, comptime expose: bool) !(if (expose) Exposed.Id else void) {
    std.debug.assert(tree.len == tree_debug.len);
    if (tree.len == 0) return error.EmptyTree;
    const target_node_id = self.exposed.getNodeId(target_node) orelse return error.TargetNotExposed;

    const orig_nodes_len = self.nodes.items.len;
    const tree_root_id = target_node_id + 1;
    _ = try self.nodes.addManyAsSlice(alloc, tree.len);
    if (debug_enabled) _ = try self.nodes_debug.addManyAsSlice(alloc, tree.len);

    @memmove(self.nodes.items[tree_root_id + tree.len ..], self.nodes.items[tree_root_id..orig_nodes_len]);
    @memcpy(self.nodes.items[tree_root_id..][0..tree.len], tree);

    if (debug_enabled) {
        @memmove(self.nodes_debug.items[tree_root_id + tree.len ..], self.nodes_debug.items[tree_root_id..orig_nodes_len]);
        @memcpy(self.nodes_debug.items[tree_root_id..][0..tree.len], tree_debug);
    }

    // propagate up the subtree size change
    var curr_parent = target_node_id;
    self.nodes.items[curr_parent].subtree_size += @intCast(tree.len);
    while (!self.nodes.items[curr_parent].isRootNode()) {
        curr_parent = self.nodes.items[curr_parent].parent;

        self.nodes.items[curr_parent].subtree_size += @intCast(tree.len);
    }

    for (self.nodes.items[tree_root_id..][0..tree.len]) |*node| {
        node.self += tree_root_id;
        node.parent += tree_root_id;
    }
    self.nodes.items[tree_root_id].parent = target_node_id;
    self.nodes.items[target_node_id].child_count += 1;

    for (self.nodes.items[tree_root_id + tree.len ..]) |*node| {
        node.self += @intCast(tree.len);
        if (node.parent >= tree_root_id) node.parent += @intCast(tree.len);
    }

    self.exposed.incrNodeIds(target_node_id, @intCast(tree.len));

    try self.dirty_transforms.update(alloc, target_node_id, @intCast(tree.len), false);
    try self.dirty_transforms.add(alloc, .{
        .start = tree_root_id,
        .end = tree_root_id + @as(u32, @intCast(tree.len)),
    });

    if (expose) {
        return try self.exposed.expose(alloc, tree_root_id);
    }
}

pub fn removeTree(self: *Scene, alloc: Allocator, target_node: Exposed.Id) !void {
    const target_node_id = self.exposed.getNodeId(target_node) orelse return error.TargetNotExposed;
    if (self.nodes.items[target_node_id].isRootNode()) return error.CannotRemoveRoot;
    const target_subtree_count = self.nodes.items[target_node_id].subtree_size;

    const parent_id = self.nodes.items[target_node_id].parent;

    const removed = 1 + target_subtree_count;
    const removed_end = target_node_id + removed;

    try self.exposed.removeNodeRange(alloc, .{ .start = target_node_id, .end = removed_end });
    @memmove(self.nodes.items[target_node_id .. self.nodes.items.len - removed], self.nodes.items[target_node_id + removed ..]);
    if (debug_enabled) {
        @memmove(self.nodes_debug.items[target_node_id .. self.nodes.items.len - removed], self.nodes_debug.items[target_node_id + removed ..]);
    }

    const new_size = self.nodes.items.len - removed;
    self.nodes.shrinkRetainingCapacity(new_size);
    if (debug_enabled) self.nodes_debug.shrinkRetainingCapacity(new_size);

    // propagate up the subtree size change
    var curr_parent = parent_id;
    self.nodes.items[parent_id].child_count -= 1;
    self.nodes.items[parent_id].subtree_size -= removed;
    while (!self.nodes.items[curr_parent].isRootNode()) {
        curr_parent = self.nodes.items[curr_parent].parent;

        self.nodes.items[curr_parent].subtree_size -= removed;
    }

    for (self.nodes.items[target_node_id..]) |*node| {
        node.self -= removed;
        if (node.parent >= removed_end) node.parent -= removed;
    }

    self.exposed.incrNodeIds(removed_end - 1, -@as(i32, @intCast(removed)));

    try self.dirty_transforms.remove(alloc, .{
        .start = target_node_id,
        .end = target_node_id + removed,
    });

    try self.dirty_transforms.update(alloc, removed_end, removed, true);
}

pub fn requestAllMeshDeps(self: *const Scene, cmd_buf: *AssetSystem.CommandBuffer, range: Range) !void {
    // not very efficient, later on the scene will be split into chunks by which these will get requested + GPU requesting
    for (self.nodes.items[range.start..range.end]) |node| {
        const mesh = node.mesh orelse continue;
        const lod_range = cmd_buf.asset_system.mesh_reg.getLodRange(mesh.dense);
        for (lod_range.@"0"..lod_range.@"1") |lod| {
            var assets = cmd_buf.asset_system.mesh_reg.getLodAssets(@intCast(lod));
            var it = assets.iterator();
            while (it.next()) |entry| {
                try cmd_buf.request(entry.value.*);
            }
        }
    }
}

pub fn releaseAllMeshDeps(self: *const Scene, cmd_buf: *AssetSystem.CommandBuffer, range: Range) !void {
    for (self.nodes.items[range.start..range.end]) |node| {
        const mesh = node.mesh orelse continue;
        const lod_range = cmd_buf.asset_system.mesh_reg.getLodRange(mesh.dense);
        for (lod_range.@"0"..lod_range.@"1") |lod| {
            var assets = cmd_buf.asset_system.mesh_reg.getLodAssets(@intCast(lod));
            var it = assets.iterator();
            while (it.next()) |entry| {
                try cmd_buf.release(entry.value.*);
            }
        }
    }
}

test {
    std.testing.refAllDecls(@This());
}

test "add dirty range" {
    const alloc = std.testing.allocator;
    var ranges = DirtyRanges{};
    defer ranges.deinit(alloc);

    try ranges.add(alloc, .{
        .start = 10,
        .end = 20,
    });
    try ranges.add(alloc, .{
        .start = 25,
        .end = 30,
    });
    try ranges.add(alloc, .{
        .start = 34,
        .end = 40,
    });

    try ranges.add(alloc, .{
        .start = 20,
        .end = 25,
    });

    try std.testing.expectEqual(Range{
        .start = 10,
        .end = 30,
    }, ranges.ranges.items[0]);

    try ranges.add(alloc, .{
        .start = 30,
        .end = 34,
    });

    try std.testing.expectEqual(Range{
        .start = 10,
        .end = 40,
    }, ranges.ranges.items[0]);

    try std.testing.expectEqual(1, ranges.ranges.items.len);

    try ranges.add(alloc, .{
        .start = 0,
        .end = 5,
    });

    try std.testing.expectEqual(Range{
        .start = 0,
        .end = 5,
    }, ranges.ranges.items[0]);
    try std.testing.expectEqual(2, ranges.ranges.items.len);
}

test "dirty ranges add bridges all touching ranges" {
    const alloc = std.testing.allocator;
    var ranges: DirtyRanges = .{};
    defer ranges.deinit(alloc);

    try ranges.add(alloc, .{ .start = 1, .end = 3 });
    try ranges.add(alloc, .{ .start = 5, .end = 7 });
    try ranges.add(alloc, .{ .start = 9, .end = 11 });
    try ranges.add(alloc, .{ .start = 3, .end = 9 });

    try std.testing.expectEqualSlices(Range, &.{.{ .start = 1, .end = 11 }}, ranges.ranges.items);
}

test "dirty ranges remove trims splits and spans ranges" {
    const alloc = std.testing.allocator;
    var ranges: DirtyRanges = .{};
    defer ranges.deinit(alloc);

    try ranges.add(alloc, .{ .start = 0, .end = 10 });
    try ranges.remove(alloc, .{ .start = 3, .end = 7 });
    try std.testing.expectEqualSlices(Range, &.{
        .{ .start = 0, .end = 3 },
        .{ .start = 7, .end = 10 },
    }, ranges.ranges.items);

    try ranges.add(alloc, .{ .start = 12, .end = 15 });
    try ranges.remove(alloc, .{ .start = 2, .end = 13 });
    try std.testing.expectEqualSlices(Range, &.{
        .{ .start = 0, .end = 2 },
        .{ .start = 13, .end = 15 },
    }, ranges.ranges.items);
}

test "dirty ranges update preserves insertion gap and closes deletion gap" {
    const alloc = std.testing.allocator;
    var ranges: DirtyRanges = .{};
    defer ranges.deinit(alloc);

    try ranges.add(alloc, .{ .start = 2, .end = 8 });
    try ranges.update(alloc, 4, 3, false);
    try std.testing.expectEqualSlices(Range, &.{
        .{ .start = 2, .end = 5 },
        .{ .start = 8, .end = 11 },
    }, ranges.ranges.items);

    try ranges.remove(alloc, .{ .start = 5, .end = 8 });
    try ranges.update(alloc, 8, 3, true);
    try std.testing.expectEqualSlices(Range, &.{.{ .start = 2, .end = 8 }}, ranges.ranges.items);
}

fn testNode(self_id: u32, parent_id: u32, child_count: u32, subtree_size: u32) Node {
    return .{
        .mesh = null,
        .model_gid = null,
        .animated = null,
        .parent_transform_relation = .inherit_transform,
        .local_transform = base.zmath.identity(),
        .world_transform = base.zmath.identity(),
        .self = self_id,
        .parent = parent_id,
        .child_count = child_count,
        .subtree_size = subtree_size,
    };
}

const test_node_debug = NodeDebug{ .node_name = "test" };

test "tree insertion and removal preserve preorder indices and exposed handles" {
    const alloc = std.testing.allocator;
    var scene: Scene = .{};
    defer scene.deinit(alloc);

    // Start with a root and one existing direct child.
    try scene.nodes.append(alloc, testNode(0, 0, 1, 1));
    try scene.nodes.append(alloc, testNode(1, 0, 0, 0));
    try scene.nodes_debug.appendSlice(alloc, &.{ test_node_debug, test_node_debug });
    const scene_root = try scene.exposed.expose(alloc, 0);
    const existing_child = try scene.exposed.expose(alloc, 1);

    const inserted_root = try scene.addTree(alloc, &.{
        testNode(0, 0, 1, 1),
        testNode(1, 0, 0, 0),
    }, scene_root, &.{ test_node_debug, test_node_debug }, true);
    const inserted_child = try scene.exposed.expose(alloc, 2);

    try std.testing.expectEqual(@as(usize, 4), scene.nodes.items.len);
    try std.testing.expectEqual(@as(u32, 2), scene.nodes.items[0].child_count);
    try std.testing.expectEqual(@as(u32, 3), scene.nodes.items[0].subtree_size);
    try std.testing.expectEqual(@as(u32, 0), scene.nodes.items[1].parent);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items[2].parent);
    try std.testing.expectEqual(@as(u32, 0), scene.nodes.items[3].parent);
    try std.testing.expectEqual(@as(u32, 3), scene.exposed.getNodeId(existing_child).?);

    try scene.removeTree(alloc, inserted_root);

    try std.testing.expectEqual(@as(usize, 2), scene.nodes.items.len);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items[0].child_count);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items[0].subtree_size);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items[1].self);
    try std.testing.expectEqual(@as(u32, 0), scene.nodes.items[1].parent);
    try std.testing.expectEqual(@as(u32, 1), scene.exposed.getNodeId(existing_child).?);
    try std.testing.expect(scene.exposed.getNodeId(inserted_root) == null);
    try std.testing.expect(scene.exposed.getNodeId(inserted_child) == null);
}

test "scene root cannot be removed as a subtree" {
    const alloc = std.testing.allocator;
    var scene: Scene = .{};
    defer scene.deinit(alloc);

    try scene.nodes.append(alloc, testNode(0, 0, 0, 0));
    try scene.nodes_debug.append(alloc, test_node_debug);
    const scene_root = try scene.exposed.expose(alloc, 0);

    try std.testing.expectError(error.CannotRemoveRoot, scene.removeTree(alloc, scene_root));
}
