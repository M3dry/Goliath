const std = @import("std");
const build_options = @import("build_options");

const zm = @import("base").zmath;

const Allocator = std.mem.Allocator;
const AssetSystem = @import("AssetSystem.zig");
const TRS = @import("Skeleton.zig").TRSNode;

const TemplateScene = @This();

const Scene = @import("Scene.zig");
const PosePool = @import("PosePool.zig");

pub const NodeRef = struct {
    ix: u32,
    ts: *TemplateScene,

    pub fn setName(self: NodeRef, name: []const u8, comptime copy: bool) !void {
        self.ts.nodes.items[self.ix].name = if (copy) try self.ts.name_alloc.dupe(u8, name) else name;
    }

    pub fn setPayload(self: NodeRef, payload: Node.Payload) void {
        self.ts.nodes.items[self.ix].payload = payload;
    }

    pub fn setLocalTransform(self: NodeRef, trs: TRS) void {
        self.ts.nodes.items[self.ix].local_transform = trs;
    }

    pub fn setTranslation(self: NodeRef, xyz: [3]f32) void {
        self.ts.nodes.items[self.ix].local_transform.translation = xyz;
    }

    pub fn setScale(self: NodeRef, xyz: [3]f32) void {
        self.ts.nodes.items[self.ix].local_transform.scale = xyz;
    }

    pub fn setRotation(self: NodeRef, quat: [4]f32) void {
        self.ts.nodes.items[self.ix].local_transform.rotation = quat;
    }

    pub fn expose(self: NodeRef, e: Expose) void {
        const node = &self.ts.nodes.items[self.ix];

        node.expose = node.expose.extend(e);
    }

    pub fn unexpose(self: NodeRef, e: Expose) void {
        const node = &self.ts.nodes.items[self.ix];

        node.expose = node.expose.subtract(e);
    }

    /// adds `sibling` as a sibling of `self`
    /// ordering will be `self` then `sibling`
    /// `sibling` can have a parent, and siblings, they'll get disconnected from `sibling`
    pub fn addSibling(self: NodeRef, sibling: NodeRef) !void {
        std.debug.assert(self.ts == sibling.ts);

        const self_node = &self.ts.nodes.items[self.ix];
        if (self_node.parent == null) return error.SelfNoParent;

        const sibling_node = &self.ts.nodes.items[sibling.ix];
        sibling.disconnect();

        sibling_node.parent = self_node.parent;
        if (self_node.next_sibling) |next| {
            self_node.next_sibling = sibling.ix;

            sibling_node.prev_sibling = self.ix;
            sibling_node.next_sibling = next;
            self.ts.nodes.items[next].prev_sibling = sibling.ix;
        } else {
            self_node.next_sibling = sibling.ix;

            sibling_node.prev_sibling = self.ix;
        }
    }

    /// adds `child` to the front of the child list of `self`
    /// if `child` has a parent, and siblings, they'll get disconnected
    pub fn addChild(self: NodeRef, child: NodeRef) void {
        std.debug.assert(self.ts == child.ts);

        const self_node = &self.ts.nodes.items[self.ix];

        const child_node = &self.ts.nodes.items[child.ix];
        child.disconnect();

        child_node.parent = self.ix;
        if (self_node.first_child) |first| {
            self_node.first_child = child.ix;

            child_node.next_sibling = first;
            self.ts.nodes.items[first].prev_sibling = child.ix;
        } else {
            self_node.first_child = child.ix;
        }
    }

    pub fn disconnect(self: NodeRef) void {
        const node = &self.ts.nodes.items[self.ix];
        if (node.parent) |parent| {
            if (node.prev_sibling) |prev| {
                self.ts.nodes.items[prev].next_sibling = node.next_sibling;

                if (node.next_sibling) |next| {
                    self.ts.nodes.items[next].prev_sibling = prev;
                }
            } else if (node.next_sibling) |next| {
                self.ts.nodes.items[next].prev_sibling = null;
                self.ts.nodes.items[parent].first_child = next;
            } else {
                self.ts.nodes.items[parent].first_child = null;
            }
        }
        node.parent = null;
        node.next_sibling = null;
        node.prev_sibling = null;
    }

    pub fn acquireAll(self: NodeRef, cmd_buf: *AssetSystem.CommandBuffer) !void {
        try self.ts.acquireAll(cmd_buf, self.ix);
    }

    pub fn releaseAll(self: NodeRef, cmd_buf: *AssetSystem.CommandBuffer) !void {
        try self.ts.releaseAll(cmd_buf, self.ix);
    }

    pub fn validateAcyclic(self: NodeRef, alloc: Allocator) !void {
        try self.ts.validateAcyclic(alloc, self.ix);
    }

    /// Builds runtime nodes after all referenced assets have been acquired.
    pub fn build(self: NodeRef, bc: BuildConfig) !Build {
        return self.ts.build(bc, self.ix);
    }
};

pub const Node = struct {
    pub const Payload = union(enum) {
        nothing,
        mesh: AssetSystem.Gid,
        model: AssetSystem.Gid,
        // later also a scene gid
    };

    name: []const u8 = &.{},

    payload: Payload = .nothing,
    expose: Expose = .{},

    local_transform: TRS = .{
        .translation = .{ 0, 0, 0 },
        .scale = .{ 1, 1, 1 },
        .rotation = zm.qidentity(),
    },

    /// only if `parent` is valid, then *_sibling can be valid
    parent: ?u32 = null,
    next_sibling: ?u32 = null,
    prev_sibling: ?u32 = null,

    first_child: ?u32 = null,
};

group_counter: u32 = 0,
node_counter: u32 = 0,
nodes: std.ArrayList(Node) = .empty,

alloc: Allocator,
name_alloc: Allocator,

/// `name_alloc` should probably be an arena because nodes don't track if the name was copied
pub fn init(alloc: Allocator, name_alloc: Allocator) TemplateScene {
    return .{
        .alloc = alloc,
        .name_alloc = name_alloc,
    };
}

pub fn deinit(self: *TemplateScene) void {
    self.nodes.deinit(self.alloc);
    // Copied names use name_alloc; callers should use an arena when names are
    // copied so they can release them together.
}

pub fn addNode(self: *TemplateScene) !NodeRef {
    const ix = self.node_counter;

    try self.nodes.append(self.alloc, .{});

    self.node_counter += 1;
    return .{
        .ix = ix,
        .ts = self,
    };
}

pub const GroupKey = u32;
pub const Expose = struct {
    node: ?GroupKey = null,
    pose: ?GroupKey = null,

    pub fn extend(self: Expose, other: Expose) Expose {
        return .{
            .node = if (other.node) |node| node else self.node,
            .pose = if (other.pose) |pose| pose else self.pose,
        };
    }

    pub fn subtract(self: Expose, other: Expose) Expose {
        return .{
            .node = if (self.node == other.node) null else self.node,
            .pose = if (self.pose == other.pose) null else self.pose,
        };
    }
};

pub fn newExposeGroup(self: *TemplateScene) GroupKey {
    const key = self.group_counter;

    self.group_counter += 1;
    return key;
}

fn acquireAll(self: *TemplateScene, cmd_buf: *AssetSystem.CommandBuffer, root: u32) !void {
    var it = try PreorderIter.init(cmd_buf.alloc, self, root);
    defer it.deinit(cmd_buf.alloc);

    while (try it.next(cmd_buf.alloc)) |node_ix| {
        switch (self.nodes.items[node_ix].payload) {
            .mesh, .model => |gid| try cmd_buf.request(gid),
            .nothing => {},
        }
    }
}

fn releaseAll(self: *TemplateScene, cmd_buf: *AssetSystem.CommandBuffer, root: u32) !void {
    var it = try PreorderIter.init(cmd_buf.alloc, self, root);
    defer it.deinit(cmd_buf.alloc);

    while (try it.next(cmd_buf.alloc)) |node_ix| {
        switch (self.nodes.items[node_ix].payload) {
            .mesh, .model => |gid| try cmd_buf.release(gid),
            .nothing => {},
        }
    }
}

pub const PreorderIter = struct {
    ts: *TemplateScene,
    root: u32,
    stack: std.ArrayList(u32) = .empty,

    pub fn init(alloc: Allocator, ts: *TemplateScene, root: u32) !PreorderIter {
        if (root >= ts.nodes.items.len) return error.RootOutOfRange;
        var it: PreorderIter = .{
            .ts = ts,
            .root = root,
        };
        errdefer it.stack.deinit(alloc);
        try it.stack.append(alloc, root);
        return it;
    }

    pub fn deinit(self: *PreorderIter, alloc: Allocator) void {
        self.stack.deinit(alloc);
    }

    pub fn next(self: *PreorderIter, alloc: Allocator) !?u32 {
        const curr = self.stack.pop() orelse return null;
        const curr_node = self.ts.nodes.items[curr];

        if (curr != self.root) {
            if (curr_node.next_sibling) |sibling| {
                try self.stack.append(alloc, sibling);
            }
        }

        if (curr_node.first_child) |child| {
            try self.stack.append(alloc, child);
        }

        return curr;
    }
};

pub const ExposeBuild = struct {
    const ExposeTypes = enum {
        node,
        pose,
    };
    // count while building, range afterwards
    const Map = std.AutoHashMapUnmanaged(GroupKey, union {
        range: std.EnumArray(ExposeTypes, struct { u32, u32 }),
        count: std.EnumArray(ExposeTypes, u32),
    });
    map: Map = .empty,

    exposed_nodes: []Scene.ExposedId = &.{},
    exposed_poses: []PosePool.PoseId = &.{},


    pub fn initArrays(self: *ExposeBuild, alloc: Allocator, node_count: u32, pose_count: u32) !void {
        self.exposed_nodes = try alloc.alloc(Scene.ExposedId, node_count);
        errdefer alloc.free(self.exposed_nodes);
        errdefer self.exposed_nodes = &.{};

        self.exposed_poses = try alloc.alloc(PosePool.PoseId, pose_count);
        errdefer alloc.free(self.exposed_poses);
        errdefer self.exposed_nodes = &.{};
    }

    pub fn deinit(self: *ExposeBuild, alloc: Allocator) void {
        self.map.deinit(alloc);
        alloc.free(self.exposed_nodes);
        alloc.free(self.exposed_poses);

        self.map = .empty;
        self.exposed_nodes = &.{};
        self.exposed_poses = &.{};
    }

    pub inline fn nodes(self: *ExposeBuild, key: GroupKey) []const Scene.ExposedId {
        const u = self.map.get(key) orelse return &.{};
        const start, const end = u.range.get(.node);

        return self.exposed_nodes[start..end];
    }

    pub inline fn poses(self: *ExposeBuild, key: GroupKey) []const PosePool.PoseId {
        const u = self.map.get(key) orelse return &.{};
        const start, const end = u.range.get(.pose);

        return self.exposed_poses[start..end];
    }

    pub fn build(self: *ExposeBuild) void {
        var map_it = self.map.iterator();

        var node_start: usize = 0;
        var pose_start: usize = 0;
        while (map_it.next()) |kv| {
            const counts = kv.value_ptr.count;

            const node_count = counts.get(.node);
            std.debug.assert(node_start + node_count <= self.exposed_nodes.len);

            const pose_count = counts.get(.pose);
            std.debug.assert(pose_start + pose_count <= self.exposed_poses.len);

            kv.value_ptr.* = .{ .range = .init(.{
                .node = .{ node_start, node_start }, // end range gets updated as the arrays get populated in buildRecurse
                .pose = .{ pose_start, pose_start },
            }) };

            node_start += node_count;
            pose_start += pose_count;
        }
    }
};

pub const Build = struct {
    nodes: []Scene.Node,
    debug_nodes: ?[]Scene.NodeDebug = null,

    exposed: ExposeBuild,

    pub fn deinit(self: *Build, alloc: Allocator, debug_alloc: Allocator) void {
        alloc.free(self.nodes);
        if (self.debug_nodes) |nodes| debug_alloc.free(nodes);

        self.exposed.deinit(alloc);
    }
};

pub const BuildConfig = struct {
    alloc: Allocator,
    debug_alloc: Allocator,
    // elements won't get freed individually
    name_alloc: Allocator,
    as: *const AssetSystem,

    comptime build_debug: bool = build_options.debug_build,
};

fn build(self: *TemplateScene, config: BuildConfig, root: u32) !Build {
    var exposed = ExposeBuild{};
    errdefer exposed.deinit(config.alloc);

    const counts = try self.countNodes(config.alloc, config.as, &exposed, root);

    try exposed.initArrays(config.alloc, counts.exposed_nodes, counts.exposed_poses);
    exposed.build();

    var res: Build = undefined;
    res.exposed = exposed;

    res.nodes = try config.alloc.alloc(Scene.Node, counts.nodes);
    errdefer config.alloc.free(res.nodes);

    if (config.build_debug) res.debug_nodes = try config.debug_alloc.alloc(Scene.NodeDebug, counts.nodes);
    errdefer if (res.debug_nodes) |nodes| config.debug_alloc.free(nodes);

    // can't use PreorderIter because of subtree_size and child_count calculations
    var cursor: u32 = 0;
    try buildRecurse(self, &res, config.as, root, 0, &cursor, config.name_alloc, config.build_debug);
    if (cursor != counts.nodes) return error.RuntimeNodeCountMismatch;

    return res;
}

fn buildRecurse(self: *TemplateScene, b: *Build, as: *const AssetSystem, template_node: u32, parent_node: u32, cursor: *u32, name_alloc: Allocator, comptime build_debug: bool) !void {
    var child_count: u32 = 0;

    const node = self.nodes.items[template_node];

    if (node.expose.node) |node_key| {
        const v = b.exposed.map.getPtr(node_key) orelse unreachable;
        const range = v.range.getPtr(.node);

        b.exposed.exposed_nodes[range.@"1"] = .{
            .gen = cursor.*,
            .id = std.math.maxInt(u32),
        };

        range.@"1" += 1;
    }

    if (node.expose.pose) |pose_key| {
        const v = b.exposed.map.getPtr(pose_key) orelse unreachable;
        const range = v.range.getPtr(.pose);

        b.exposed.exposed_poses[range.@"1"] = .{
            .skeleton = cursor.*,
            .gen = std.math.maxInt(u8),
            .slot = std.math.maxInt(u24),
        };

        range.@"1" += 1;
    }

    const payload = node.payload;
    const last_output = switch (payload) {
        .model => |gid| blk: {
            const model_dense = as.denseIndex(gid);

            const meshes = as.model_reg.getMeshDenseIds(model_dense);
            const transforms = as.model_reg.getMeshTransforms(model_dense);
            const skins = as.model_reg.getMeshSkins(model_dense);
            const skeleton = as.model_reg.getSkeleton(model_dense);

            const anchor = cursor.*;
            b.nodes[anchor] = .{
                .mesh = null,
                .model_gid = gid,
                .animated = null,
                .parent_transform_relation = .{.inherit_transform},
                .local_transform = node.local_transform.toMat(),
                .world_transform = undefined,
                .self = anchor,
                .parent = parent_node,
                .child_count = @intCast(meshes.len),
                .subtree_size = 0,
            };
            if (build_debug) b.debug_nodes.?[anchor] = .{
                .node_name = try name_alloc.dupe(u8, node.name),
            };
            cursor.* += 1;

            for (meshes, transforms, skins, as.model_reg.getMeshGids(model_dense)) |mesh, transform, skin, mesh_gid| {
                if (skin != std.math.maxInt(u32) and skeleton == null) return error.SkinWithoutSkeleton;
                b.nodes[cursor.*] = .{
                    .mesh = .{ .dense = mesh, .gid = mesh_gid },
                    .model_gid = null,
                    .animated = if (skin != std.math.maxInt(u32)) .{
                        .model = model_dense,
                        .skin = skin,
                        .pose = undefined,
                        .pose_revision = 0,
                    } else null,
                    .parent_transform_relation = .{.inherit_transform},
                    .local_transform = transform,
                    .world_transform = undefined,
                    .self = cursor.*,
                    .parent = anchor,
                    .child_count = 0,
                    .subtree_size = 0,
                };
                if (build_debug) b.debug_nodes.?[cursor.*] = .{
                    .node_name = try name_alloc.dupe(u8, node.name),
                };

                cursor.* += 1;
            }
            break :blk anchor;
        },
        .nothing, .mesh => blk: {
            const mesh = switch (payload) {
                .mesh => |gid| as.denseIndex(gid),
                _ => null,
            };
            b.nodes[cursor.*] = .{
                .mesh = if (mesh) |dense| .{ .dense = dense, .gid = switch (payload) {
                    .mesh => |gid| gid,
                    else => unreachable,
                } } else null,
                .model_gid = null,
                .animated = null,
                .parent_transform_relation = .{.inherit_transform},
                .local_transform = node.local_transform.toMat(),
                .world_transform = undefined,
                .self = cursor.*,
                .parent = parent_node,
                .child_count = 0,
                .subtree_size = 0,
            };
            if (build_debug) b.debug_nodes.?[cursor.*] = .{
                .node_name = try name_alloc.dupe(u8, node.name),
            };

            cursor.* += 1;
            break :blk cursor.* - 1;
        },
    };

    var child = self.nodes.items[template_node].first_child;
    while (child) |c_ix| {
        try buildRecurse(self, b, as, c_ix, last_output, cursor, name_alloc, build_debug);

        child_count += 1;
        child = self.nodes.items[c_ix].next_sibling;
    }

    // A model anchor already counts its emitted mesh children; include the
    // template-authored children emitted below as additional direct children.
    b.nodes[last_output].child_count += child_count;
    b.nodes[last_output].subtree_size = cursor.* - last_output - 1;
}

fn countNodes(self: *TemplateScene, alloc: Allocator, as: *const AssetSystem, exposed_build: *ExposeBuild, root: u32) !struct {
    nodes: usize,
    exposed_nodes: u32,
    exposed_poses: u32,
} {
    var it = try PreorderIter.init(alloc, self, root);
    defer it.deinit(alloc);

    var count: usize = 0;
    var exposed_nodes: u32 = 0;
    var exposed_poses: u32 = 0;
    while (try it.next(alloc)) |node_ix| {
        count += switch (self.nodes.items[node_ix].payload) {
            .mesh, .nothing => 1,
            .model => |gid| 1 + as.model_reg.getMeshDenseIds(as.denseIndex(gid)).len,
        };

        const exposed = self.nodes.items[node_ix].expose;
        if (exposed.node) |group_key| {
            const res = try exposed_build.map.getOrPut(alloc, group_key);
            if (!res.found_existing) res.value_ptr.* = .{ .count = .initFill(0) };

            res.value_ptr.count.getPtr(.node).* += 1;
            exposed_nodes += 1;
        }
        if (exposed.pose) |group_key| {
            const res = try exposed_build.map.getOrPut(alloc, group_key);
            if (!res.found_existing) res.value_ptr.* = .{ .count = .initFill(0) };

            res.value_ptr.count.getPtr(.pose).* += 1;
            exposed_poses += 1;
        }
    }
    return .{
        .nodes = count,
        .exposed_nodes = exposed_nodes,
        .exposed_poses = exposed_poses,
    };
}

/// Checks child/sibling links reachable from `root`; the root's own sibling is
/// outside the selected template subtree.
pub fn validateAcyclic(self: *const TemplateScene, alloc: Allocator, root: u32) !void {
    if (root >= self.nodes.items.len) return error.RootOutOfRange;
    const State = enum { unseen, visiting, complete };
    const Edge = enum { child, sibling, finish };
    const Frame = struct { node: u32, edge: Edge = .child };

    const states = try alloc.alloc(State, self.nodes.items.len);
    defer alloc.free(states);
    @memset(states, .unseen);
    var stack: std.ArrayList(Frame) = .empty;
    defer stack.deinit(alloc);
    states[root] = .visiting;
    try stack.append(alloc, .{ .node = root });

    while (stack.items.len > 0) {
        const frame = &stack.items[stack.items.len - 1];
        const target: ?u32 = switch (frame.edge) {
            .child => blk: {
                frame.edge = .sibling;
                break :blk self.nodes.items[frame.node].first_child;
            },
            .sibling => blk: {
                frame.edge = .finish;
                break :blk if (frame.node == root) null else self.nodes.items[frame.node].next_sibling;
            },
            .finish => {
                states[frame.node] = .complete;
                _ = stack.pop();
                continue;
            },
        };
        if (target) |ix| {
            if (ix >= self.nodes.items.len) return error.NodeIndexOutOfRange;
            switch (states[ix]) {
                .visiting => return error.CycleDetected,
                .complete => return error.NodeReferencedMultipleTimes,
                .unseen => {
                    states[ix] = .visiting;
                    try stack.append(alloc, .{ .node = ix });
                },
            }
        }
    }
}

test "preorder iterator yields root and descendants in depth-first order" {
    const alloc = std.testing.allocator;
    var ts = TemplateScene.init(alloc, alloc);
    defer ts.nodes.deinit(alloc);

    // root
    // ├─ a
    // │  ├─ c
    // │  └─ d
    // └─ b
    // separate is the root's sibling and must not be part of this walk.
    try ts.nodes.append(alloc, .{ .name = "root", .first_child = 1, .next_sibling = 5 });
    try ts.nodes.append(alloc, .{ .name = "a", .parent = 0, .first_child = 3, .next_sibling = 2 });
    try ts.nodes.append(alloc, .{ .name = "b", .parent = 0 });
    try ts.nodes.append(alloc, .{ .name = "c", .parent = 1, .next_sibling = 4 });
    try ts.nodes.append(alloc, .{ .name = "d", .parent = 1 });
    try ts.nodes.append(alloc, .{ .name = "separate" });

    var it = try PreorderIter.init(alloc, &ts, 0);
    defer it.deinit(alloc);
    var actual: [6]u32 = undefined;
    var len: usize = 0;
    while (try it.next(alloc)) |ix| {
        actual[len] = ix;
        len += 1;
    }

    try std.testing.expectEqualSlices(u32, &.{ 0, 1, 3, 4, 2 }, actual[0..len]);
    try std.testing.expectEqual(@as(?u32, null), try it.next(alloc));
}

test "preorder iterator handles a single node and remains exhausted" {
    const alloc = std.testing.allocator;
    var ts = TemplateScene.init(alloc, alloc);
    defer ts.nodes.deinit(alloc);
    try ts.nodes.append(alloc, .{ .name = "only", .next_sibling = 1 });
    try ts.nodes.append(alloc, .{ .name = "outside" });

    var it = try PreorderIter.init(alloc, &ts, 0);
    defer it.deinit(alloc);
    try std.testing.expectEqual(@as(?u32, 0), try it.next(alloc));
    try std.testing.expectEqual(@as(?u32, null), try it.next(alloc));
    try std.testing.expectEqual(@as(?u32, null), try it.next(alloc));
}

test "preorder iterator handles a single-child chain" {
    const alloc = std.testing.allocator;
    var ts = TemplateScene.init(alloc, alloc);
    defer ts.nodes.deinit(alloc);
    try ts.nodes.append(alloc, .{ .name = "root", .first_child = 1 });
    try ts.nodes.append(alloc, .{ .name = "child", .parent = 0, .first_child = 2 });
    try ts.nodes.append(alloc, .{ .name = "grandchild", .parent = 1 });

    var it = try PreorderIter.init(alloc, &ts, 0);
    defer it.deinit(alloc);
    try std.testing.expectEqual(@as(?u32, 0), try it.next(alloc));
    try std.testing.expectEqual(@as(?u32, 1), try it.next(alloc));
    try std.testing.expectEqual(@as(?u32, 2), try it.next(alloc));
    try std.testing.expectEqual(@as(?u32, null), try it.next(alloc));
}

test "cycle validation catches child and sibling loops but ignores root sibling" {
    const alloc = std.testing.allocator;
    var ts = TemplateScene.init(alloc, alloc);
    defer ts.nodes.deinit(alloc);
    try ts.nodes.append(alloc, .{ .first_child = 1, .next_sibling = 3 });
    try ts.nodes.append(alloc, .{ .parent = 0, .next_sibling = 2 });
    try ts.nodes.append(alloc, .{ .parent = 0 });
    try ts.nodes.append(alloc, .{});
    try ts.validateAcyclic(alloc, 0);

    ts.nodes.items[2].next_sibling = 1;
    try std.testing.expectError(error.CycleDetected, ts.validateAcyclic(alloc, 0));

    ts.nodes.items[2].next_sibling = null;
    ts.nodes.items[1].first_child = 0;
    try std.testing.expectError(error.CycleDetected, ts.validateAcyclic(alloc, 0));
}
