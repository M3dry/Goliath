const std = @import("std");
const zmesh = @import("zmesh");

const Allocator = std.mem.Allocator;
const zcgltf = zmesh.io.zcgltf;

const Skeleton = @This();

pub const TRSNode = struct {
    translation: [3]f32,
    rotation: [4]f32,
    scale: [3]f32,
};

bind_local_transforms: []TRSNode,
parents: []u32,

pub fn deinit(self: *const Skeleton, alloc: Allocator) void {
    alloc.free(self.bind_local_transforms);
    alloc.free(self.parents);
}

pub fn fromGltf(
    alloc: Allocator,
    data: *zcgltf.Data,
    root_node_index: u32,
) !struct { Skeleton, []u32 } {
    const nodes_slice = data.nodes.?[0..data.nodes_count];

    var visited = try alloc.alloc(bool, data.nodes_count);
    defer alloc.free(visited);
    @memset(visited, false);

    var collected = std.ArrayList(u32).empty;
    defer collected.deinit(alloc);

    // Walk up to collect ancestors of root_node_index
    {
        var current_ix = root_node_index;
        while (true) {
            const current_node = &nodes_slice[current_ix];
            if (current_node.parent) |parent| {
                const parent_ix = @as(u32, @intCast(
                    (@intFromPtr(parent) - @intFromPtr(nodes_slice.ptr)) / @sizeOf(zcgltf.Node),
                ));
                try collected.append(alloc, parent_ix);
                current_ix = parent_ix;
            } else {
                break;
            }
        }
    }
    // Reverse so root of hierarchy comes first
    std.mem.reverse(u32, collected.items);

    try collected.append(alloc, root_node_index);
    visited[root_node_index] = true;

    var read_ix: usize = 0;
    while (read_ix < collected.items.len) {
        const gltf_ix = collected.items[read_ix];
        read_ix += 1;

        const node = &nodes_slice[gltf_ix];
        if (node.children) |children| {
            for (0..node.children_count) |ci| {
                const child_ptr = children[ci];
                const child_ix = @as(u32, @intCast(
                    (@intFromPtr(child_ptr) - @intFromPtr(nodes_slice.ptr)) / @sizeOf(zcgltf.Node),
                ));
                if (!visited[child_ix]) {
                    visited[child_ix] = true;
                    try collected.append(alloc, child_ix);
                }
            }
        }
    }

    const count = collected.items.len;
    const node_map = try collected.toOwnedSlice(alloc);
    errdefer alloc.free(node_map);

    const bind_local_transforms = try alloc.alloc(TRSNode, count);
    errdefer alloc.free(bind_local_transforms);

    const parents = try alloc.alloc(u32, count);
    errdefer alloc.free(parents);

    for (node_map, 0..) |gltf_ix, local_ix| {
        const node = &nodes_slice[gltf_ix];
        bind_local_transforms[local_ix] = .{
            .translation = if (node.has_translation != 0) node.translation else .{ 0, 0, 0 },
            .rotation = if (node.has_rotation != 0) node.rotation else .{ 0, 0, 0, 1 },
            .scale = if (node.has_scale != 0) node.scale else .{ 1, 1, 1 },
        };

        if (node.parent) |parent| {
            const parent_gltf_ix = @as(u32, @intCast(
                (@intFromPtr(parent) - @intFromPtr(nodes_slice.ptr)) / @sizeOf(zcgltf.Node),
            ));
            var found = false;
            for (node_map[0..local_ix], 0..) |map_entry, mi| {
                if (map_entry == parent_gltf_ix) {
                    parents[local_ix] = @intCast(mi);
                    found = true;
                    break;
                }
            }
            if (!found) return error.ParentNotInSkeleton;
        } else {
            parents[local_ix] = std.math.maxInt(u32);
        }
    }

    for (parents, 0..) |p, i| {
        if (p != std.math.maxInt(u32)) std.debug.assert(p < i);
    }

    return .{
        .{
            .bind_local_transforms = bind_local_transforms,
            .parents = parents,
        },
        node_map,
    };
}
