const std = @import("std");

const base = @import("base");
const Allocator = std.mem.Allocator;
const Mat = base.zmath.Mat;
const Gid = @import("AssetSystem.zig").Gid;
const Skin = @import("Skin.zig");

const Scene = @This();

const NodeDebug = struct {
    mesh_gid: ?Gid,
    skeleton_gid: ?Gid,
};

const Node = struct {
    const none: u32 = std.math.maxInt(u32);

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

    local_transform: Mat,
    world_transform: Mat,

    self: u32,
    parent: u32 = none,
    child_count: u32,
    subtree_end: u32,
};

nodes: []Node, // topo sorted

expose_counter: u32 = 0,
exposed: []struct {
    stable: u32,
    node_id: u32,
},

/// `tree` expected to be in DFS pre order
pub fn addNodes(self: *Scene, alloc: Allocator, tree: []const Node, target_node: u32) !void {
    const target_node_id = for (self.exposed) |e| blk: {
        if (e.stable == target_node) break :blk e.node_id;
    } else return error.TargetNotExposed;

    const orig_nodes_len = self.nodes.len;
    self.nodes = try alloc.realloc(self.nodes, self.nodes.len + tree.len);

    const target_subtree_end = self.nodes[target_node_id].subtree_end;
    const target_tree_len = target_subtree_end - target_node_id;
    @memmove(self.nodes[orig_nodes_len - target_tree_len..], self.nodes[target_node_id + 1..][0..target_subtree_end]);

    @memcpy(self.nodes[target_node_id + 1..][0..tree.len], tree);

    var last_id = target_node_id;
    for (self.nodes[target_node_id + 1][0..tree.len]) |*node| {
        node.self += target_node_id;
        last_id = node.self;

        node.parent = last_id;
        node.subtree_end += target_node_id;
    }

    last_id = target_node_id;
    for (self.nodes[target_node_id + tree.len..0]) |*node| {
        node.self += tree.len;
        last_id = node.self;

        node.parent = last_id;
        node.subtree_end += tree.len;
    }
}

test {
    std.testing.refAllDecls(@This());
}
