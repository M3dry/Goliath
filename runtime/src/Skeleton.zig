const std = @import("std");
const zmesh = @import("zmesh");
const base = @import("base");
const zm = base.zmath;

const Allocator = std.mem.Allocator;
const zcgltf = zmesh.io.zcgltf;

const Skeleton = @This();

fn normalizeQuaternion(rotation: *[4]f32) void {
    const length_squared = rotation[0] * rotation[0] + rotation[1] * rotation[1] + rotation[2] * rotation[2] + rotation[3] * rotation[3];
    if (!(length_squared > 0) or !std.math.isFinite(length_squared)) {
        rotation.* = .{ 0, 0, 0, 1 };
        return;
    }
    const inverse_length = 1.0 / @sqrt(length_squared);
    for (rotation) |*component| component.* *= inverse_length;
}

bind_local_transforms: []base.util.TRS,
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

    if (root_node_index >= data.nodes_count) return error.InvalidSkeletonRoot;

    const required = try alloc.alloc(bool, data.nodes_count);
    defer alloc.free(required);
    @memset(required, false);

    var collected = std.ArrayList(u32).empty;
    defer collected.deinit(alloc);

    const Helpers = struct {
        fn indexOf(nodes: []zcgltf.Node, node: *const zcgltf.Node) u32 {
            return @intCast((@intFromPtr(node) - @intFromPtr(nodes.ptr)) / @sizeOf(zcgltf.Node));
        }

        fn isDescendantOf(node: *const zcgltf.Node, ancestor: *const zcgltf.Node) bool {
            var current: ?*const zcgltf.Node = node;
            while (current) |candidate| : (current = candidate.parent) {
                if (candidate == ancestor) return true;
            }
            return false;
        }

        fn markWithAncestors(nodes: []zcgltf.Node, required_nodes: []bool, node: *const zcgltf.Node) void {
            var current: ?*const zcgltf.Node = node;
            while (current) |candidate| : (current = candidate.parent) {
                const index = indexOf(nodes, candidate);
                if (required_nodes[index]) break;
                required_nodes[index] = true;
            }
        }

        fn collectPreorder(allocator: Allocator, nodes: []zcgltf.Node, required_nodes: []const bool, node: *const zcgltf.Node, out: *std.ArrayList(u32)) !void {
            const index = indexOf(nodes, node);
            if (!required_nodes[index]) return;
            try out.append(allocator, index);
            if (node.children) |children| {
                for (0..node.children_count) |child_index| {
                    try collectPreorder(allocator, nodes, required_nodes, children[child_index], out);
                }
            }
        }
    };

    const root_node = &nodes_slice[root_node_index];
    Helpers.markWithAncestors(nodes_slice, required, root_node);

    // Include joints belonging to this skeleton and the paths which connect
    // them to its root. Skins elsewhere in the same glTF remain independent.
    if (data.skins) |skins| {
        for (skins[0..data.skins_count]) |skin| {
            for (0..skin.joints_count) |joint_index| {
                const joint = skin.joints[joint_index];
                if (Helpers.isDescendantOf(joint, root_node)) Helpers.markWithAncestors(nodes_slice, required, joint);
            }
        }
    }

    // Animated helper nodes may not be skin joints, but still belong in the
    // pose when they are beneath the requested skeleton root.
    if (data.animations) |animations| {
        for (animations[0..data.animations_count]) |animation| {
            for (animation.channels[0..animation.channels_count]) |channel| {
                switch (channel.target_path) {
                    .translation, .rotation, .scale => {},
                    else => continue,
                }
                const target = channel.target_node orelse continue;
                if (Helpers.isDescendantOf(target, root_node)) Helpers.markWithAncestors(nodes_slice, required, target);
            }
        }
    }

    var hierarchy_root = root_node;
    while (hierarchy_root.parent) |parent| hierarchy_root = parent;
    try Helpers.collectPreorder(alloc, nodes_slice, required, hierarchy_root, &collected);
    if (collected.items.len == 0) return error.EmptySkeleton;

    // The required-node set also serves as complete visitation tracking:
    // ancestors are marked at the same time as every selected node.
    for (collected.items) |node_index| {
        std.debug.assert(required[node_index]);
    }

    const count = collected.items.len;
    const node_map = try collected.toOwnedSlice(alloc);
    errdefer alloc.free(node_map);

    const bind_local_transforms = try alloc.alloc(base.util.TRS, count);
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
        normalizeQuaternion(&bind_local_transforms[local_ix].rotation);
        for (bind_local_transforms[local_ix].translation) |component| if (!std.math.isFinite(component)) return error.InvalidSkeletonTransform;
        for (bind_local_transforms[local_ix].scale) |component| if (!std.math.isFinite(component)) return error.InvalidSkeletonTransform;

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
