const std = @import("std");

const base = @import("base");
const Allocator = std.mem.Allocator;
const Mat = base.zmath.Mat;
const Gid = @import("AssetSystem.zig").Gid;
const Skin = @import("Skin.zig");

const debug_enabled = true;

const Scene = @This();

const Range = struct {
    start: u32,
    end: u32,

    pub inline fn contains(self: Range, other: Range) void {
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

const Exposed = struct {
    stable_counter: u32 = 0,
    data: std.MultiArrayList(struct {
        stable: u32,
        node_id: u32,
    }) = .empty,

    const Self = @This();

    inline fn findByStable(self: *const Self, stable_id: u32) ?usize {
        const S = struct {
            fn order(context: u32, ix: u32) std.math.Order {
                return std.math.order(context, ix);
            }
        };

        const slice = self.data.slice();

        return std.sort.binarySearch(u32, slice.items(.stable), stable_id, S.order);
    }

    pub inline fn find(self: *const Self, stable_id: u32) ?u32 {
        return self.data.items(.node_id)[self.findByStable(stable_id) orelse return null];
    }

    pub inline fn expose(self: *Self, alloc: Allocator, node_id: u32) !u32 {
        try self.data.append(alloc, .{
            .stable = self.stable_counter,
            .node_id = node_id,
        });
        self.stable_counter += 1;
    }

    pub inline fn unexpose(self: *Self, stable_id: u32) void {
        self.data.orderedRemove(self.findByStable(stable_id) orelse return);
    }

    pub inline fn incrNodeIds(self: *Self, above: u32, delta: u32) void {
        for (self.data.items(.node_id)) |*id| {
            if (id > above) id += delta;
        }
    }
};

const NodeDebug = struct {
    mesh_gid: ?Gid,
    skeleton_gid: ?Gid,
};

const Node = struct {
    mesh: ?u32,
    mesh_skeleton: ?struct {
        // NOTE: skin is managed by the asset system, where it's stored as an allocated []Skin which lives until the model asset for which it belongs isn't released - which (if no bugs) can't happen as long as the scene is alive since it acquires the corresponding model asset
        skin: *Skin,
        // dense into SkeletonRegistry
        skeleton: u32,
    },

    parent_transform_relation: union(enum) {
        inherit_transform,
        attachment: u32,
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

nodes_debug: []NodeDebug = &.{},
nodes: []Node = &.{}, // topo sorted
/// [start, end) range
dirty_transforms: std.ArrayList(Range) = .empty,

exposed: Exposed = .{},

pub fn rebuildDirtyTransforms(self: *Scene) void {
    while (self.dirty_transforms.pop()) |range| {
        const nodes = self.nodes[range.start..range.end];

        for (nodes) |*node| {
            if (node.isRootNode()) {
                node.world_transform = base.zmath.identity();
                continue;
            }

            const parent = self.nodes[node.parent];
            switch (node.parent_transform_relation) {
                .attachment => |att| {
                    _ = att;
                    // TODO:
                    unreachable;
                },
                .inherit_transform => {
                    node.world_transform = base.zmath.mul(parent.world_transform, node.local_transform);
                },
            }
        }
    }
}

/// `tree` expected to be in DFS pre order
pub fn addTree(self: *Scene, alloc: Allocator, tree: []const Node, target_node: u32, tree_debug: []const NodeDebug, comptime expose: bool) !(if (expose) u32 else void) {
    const target_node_id = self.exposed.find(target_node) orelse return error.TargetNotExposed;

    const orig_nodes_len = self.nodes.len;
    self.nodes = try alloc.realloc(self.nodes, self.nodes.len + tree.len);

    const target_subtree_size = self.nodes[target_node_id].subtree_size;
    @memmove(self.nodes[orig_nodes_len - target_subtree_size..], self.nodes[target_node_id + 1..][0..target_subtree_size]);
    @memcpy(self.nodes[target_node_id + 1..][0..tree.len], tree);

    if (debug_enabled) {
        @memmove(self.nodes_debug[orig_nodes_len - target_subtree_size..], self.nodes_debug[target_node_id + 1..][0..target_subtree_size]);
        @memcpy(self.nodes_debug[target_node_id + 1..][0..tree.len], tree_debug);
    }

    self.nodes[target_node_id].subtree_size += @intCast(tree.len);

    for (self.nodes[target_node_id + 1..][0..tree.len]) |*node| {
        node.self += target_node_id;
        node.parent += target_node_id;
    }

    for (self.nodes[target_node_id + tree.len..]) |*node| {
        node.self += @intCast(tree.len);
        node.parent += @intCast(tree.len);
    }

    self.exposed.incrNodeIds(target_node_id, @intCast(tree.len));

    try addDirtyRange(&self.dirty_transforms, alloc, .{
        .start = target_node_id + 1,
        .end = @intCast(tree.len),
    });

    if (expose) {
        return try self.exposed.expose(alloc, target_node_id + 1);
    }
}

pub fn removeTree(self: *Scene, target_node: u32) void {

}

fn addDirtyRange(self: *std.ArrayList(Range), alloc: Allocator, range: Range) !void {
    var insert_ix: ?usize = null;
    for (0.., self.items) |i, *r| {
        if (range.end < r.start) {
            insert_ix = i;
            break;
        } else if (range.start > r.end) {
        } else {
            r.* = r.overlap(range).?;

            if (i + 1 == self.items.len) return;

            const r2 = self.items[i + 1];
            if (r.overlap(r2)) |overlap| {
                r.* = overlap;
                _ = self.orderedRemove(i + 1);
            }
            return;
        }
    }

    if (insert_ix) |ix| try self.insert(alloc, ix, range);
}

test {
    std.testing.refAllDecls(@This());
}

test "add dirty range" {
    const alloc = std.testing.allocator;
    var arr: std.ArrayList(Range) = .empty;
    defer arr.deinit(alloc);

    try arr.append(alloc, .{
        .start = 10,
        .end = 20,
    });
    try arr.append(alloc, .{
        .start = 25,
        .end = 30,
    });
    try arr.append(alloc, .{
        .start = 34,
        .end = 40,
    });

    try addDirtyRange(&arr, alloc, .{
        .start = 20,
        .end = 25,
    });

    try std.testing.expectEqual(Range{
        .start = 10,
        .end = 30,
    }, arr.items[0]);

    try addDirtyRange(&arr, alloc, .{
        .start = 30,
        .end = 34,
    });

    try std.testing.expectEqual(Range{
        .start = 10,
        .end = 40,
    }, arr.items[0]);

    try std.testing.expectEqual(1, arr.items.len);

    try addDirtyRange(&arr, alloc, .{
        .start = 0,
        .end = 5,
    });

    try std.testing.expectEqual(Range{
        .start = 0,
        .end = 5,
    }, arr.items[0]);
    try std.testing.expectEqual(2, arr.items.len);
}
