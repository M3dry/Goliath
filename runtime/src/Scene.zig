const std = @import("std");
const build_options = @import("build_options");

const base = @import("base");
const Allocator = std.mem.Allocator;
const Mat = base.zmath.Mat;
const AssetSystem = @import("AssetSystem.zig");
const Gid = AssetSystem.Gid;
const Skin = @import("Skin.zig");
const PosePool = @import("PosePool.zig");
const TemplateScene = @import("TemplateScene.zig");

// TODO: take in as comptime param to the functins that use this one/carry it comptime on the struct (probably better)
const debug_enabled = build_options.debug_build;

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

pub const ExposedId = Exposed.Id;
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
                .gen = self.data.items[id].gen,
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
        skin: u32 = std.math.maxInt(u32), // gets passed into the animation system, either it gets interpretted as normal skin indices, or for procedural animations it can be used as an identifier of the mesh eg. => TODO: modify TemplateScene to be able to set the skin value of a mesh payload node
    },
    // model anchor has .mesh = null, but .model != null, the anchor's children that have .mesh set, and .model fields equal to anchor's
    // pose is only set for the anchor node, it's children that have skin set "inherit" the pose (get sent to animation)
    model: ?struct {
        gid: Gid,
        dense: u32,
    },
    pose: ?struct {
        id: PosePool.PoseId,
        revision: u64,
    },

    parent_transform_relation: union(enum) {
        inherit_transform,
        attachment: u32, // points into `attachments`, still inherits the parent's transform (makes it easier to do dirty tracking, and most of the time what is wanted, otherwise node should be placed as the child of the root)
    },

    local_transform: Mat, // will get stored as TRS in the asset, for runtime we can convert to 4x4 at load once
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
nodes: std.MultiArrayList(struct {
    world_transform: Mat,
    node: Node,
}) = .empty, // topo sorted; transform storage is a contiguous field for GPU uploads
dirty_transforms: DirtyRanges = .{},

exposed: Exposed = .{},

attachments: Attachments = .{},

pub fn initRoot(self: *Scene, alloc: Allocator) !Exposed.Id {
    if (self.nodes.len != 0) return error.SceneNotEmpty;

    try self.nodes_debug.append(alloc, .{
        .node_name = "root",
    });

    try self.nodes.append(alloc, .{
        .node = .{
            .mesh = null,
            .model = null,
            .pose = null,
            .parent_transform_relation = .inherit_transform,
            .local_transform = base.zmath.identity(),
            .self = 0,
            .parent = 0,
            .child_count = 0,
            .subtree_size = 0,
        },
        .world_transform = base.zmath.identity(),
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
    const nodes = self.nodes.items(.node);
    const world_transforms = self.nodes.items(.world_transform);
    while (self.dirty_transforms.ranges.pop()) |range| {
        for (range.start..range.end) |ix| {
            const node = &nodes[ix];
            if (node.isRootNode()) {
                world_transforms[ix] = node.local_transform;
                continue;
            }

            switch (node.parent_transform_relation) {
                .attachment => |att| {
                    _ = att;
                    // TODO:
                    unreachable;
                },
                .inherit_transform => {
                    world_transforms[ix] = base.zmath.mul(node.local_transform, world_transforms[node.parent]);
                },
            }
        }
    }
}

pub fn addTemplateScene(self: *Scene, target_node: Exposed.Id, ts: *TemplateScene.BuildPrepare, as: *const AssetSystem, alloc: Allocator, name_alloc: Allocator) !void {
    if (self.nodes.len == 0) return error.SceneNotInitialized;
    if (ts.node_count == 0) return error.EmptyTemplate;
    const target_node_id = self.exposed.getNodeId(target_node) orelse return error.TargetNotExposed;

    // The iterator is consumed and may grow its traversal stack, so BuildPrepare
    // is passed by pointer. Its group arrays remain available to the caller.
    ts.exposed_groups.build();

    const insert_at: u32 = target_node_id + 1;
    const count = ts.node_count;
    if (count > std.math.maxInt(u32) - self.nodes.len) return error.SceneTooLarge;

    try self.nodes.ensureUnusedCapacity(alloc, count);
    if (debug_enabled) try self.nodes_debug.ensureUnusedCapacity(alloc, count);
    try self.exposed.data.ensureUnusedCapacity(alloc, ts.exposed_groups.exposed_nodes.len);
    try self.dirty_transforms.ranges.ensureUnusedCapacity(alloc, 2);

    const old_len = self.nodes.len;
    try self.nodes.resize(alloc, old_len + count);
    const node_items = self.nodes.items(.node);
    const world_transforms = self.nodes.items(.world_transform);
    @memmove(node_items[insert_at + count ..], node_items[insert_at..old_len]);
    @memmove(world_transforms[insert_at + count ..], world_transforms[insert_at..old_len]);
    @memset(world_transforms[insert_at..][0..count], undefined);
    if (debug_enabled) {
        _ = try self.nodes_debug.addManyAsSlice(alloc, count);
        @memmove(self.nodes_debug.items[insert_at + count ..], self.nodes_debug.items[insert_at..old_len]);
    }

    for (node_items[insert_at + count ..]) |*node| {
        node.self += @intCast(count);
        if (node.parent >= insert_at) node.parent += @intCast(count);
    }
    self.exposed.incrNodeIds(target_node_id, @intCast(count));

    const BuildFrame = struct {
        template_index: u32,
        runtime_index: u32,
        subtree_start: usize,
        extra_children: u32,
    };
    var frames: std.ArrayList(BuildFrame) = .empty;
    defer frames.deinit(alloc);

    var cursor: usize = 0;
    while (try ts.iter.next(alloc)) |event| {
        switch (event) {
            .enter => |build_node| {
                const parent = if (frames.items.len == 0)
                    target_node_id
                else
                    frames.items[frames.items.len - 1].runtime_index;
                const runtime_index: u32 = @intCast(insert_at + cursor);
                const local_transform = build_node.local_transform.toMat();
                var pose: @FieldType(Node, "pose") = null;
                var mesh_child_count: u32 = 0;

                switch (build_node.payload) {
                    .nothing => |maybe_pose| {
                        if (maybe_pose) |id| {
                            pose = .{ .id = id, .revision = try self.pose_pool.getRevision(id) };
                        }
                        node_items[runtime_index] = .{
                            .mesh = null,
                            .model = null,
                            .pose = pose,
                            .parent_transform_relation = .inherit_transform,
                            .local_transform = local_transform,
                            .self = runtime_index,
                            .parent = parent,
                            .child_count = 0,
                            .subtree_size = 0,
                        };
                        cursor += 1;
                    },
                    .mesh => |mesh| {
                        node_items[runtime_index] = .{
                            .mesh = .{ .gid = mesh.gid, .dense = as.denseIndex(mesh.gid), .skin = mesh.custom_skin },
                            .model = null,
                            .pose = null,
                            .parent_transform_relation = .inherit_transform,
                            .local_transform = local_transform,
                            .self = runtime_index,
                            .parent = parent,
                            .child_count = 0,
                            .subtree_size = 0,
                        };
                        cursor += 1;
                    },
                    .model => |model_gid| {
                        const model_dense = as.denseIndex(model_gid);
                        const meshes = as.model_reg.getMeshDenseIds(model_dense);
                        const transforms = as.model_reg.getMeshTransforms(model_dense);
                        const skins = as.model_reg.getMeshSkins(model_dense);
                        const mesh_gids = as.model_reg.getMeshGids(model_dense);
                        const skeleton = as.model_reg.getSkeleton(model_dense);

                        if (meshes.len != transforms.len or meshes.len != skins.len or meshes.len != mesh_gids.len)
                            return error.ModelMeshDataLengthMismatch;
                        mesh_child_count = @intCast(meshes.len);

                        if (skeleton) |s| {
                            if (self.pose_pool.pools.items.len <= s.dense or self.pose_pool.pools.items[s.dense].isUninitialized()) {
                                const skeleton_data = as.skeleton_reg.getSkeleton(s.dense);
                                try self.pose_pool.newSkeletonPool(
                                    alloc,
                                    s.gid,
                                    s.dense,
                                    @intCast(skeleton_data.bind_local_transforms.len),
                                );
                            }

                            const id = self.pose_pool.newPose(alloc, s.dense) catch unreachable;
                            pose = .{ .id = id, .revision = self.pose_pool.getRevision(id) catch unreachable };
                        }

                        node_items[runtime_index] = .{
                            .mesh = null,
                            .model = .{ .gid = model_gid, .dense = model_dense },
                            .pose = pose,
                            .parent_transform_relation = .inherit_transform,
                            .local_transform = local_transform,
                            .self = runtime_index,
                            .parent = parent,
                            .child_count = mesh_child_count,
                            .subtree_size = 0,
                        };
                        cursor += 1;

                        for (meshes, transforms, skins, mesh_gids) |mesh_dense, transform, skin, mesh_gid| {
                            if (skin != std.math.maxInt(u32) and skeleton == null) return error.SkinWithoutSkeleton;
                            const child_index: u32 = @intCast(insert_at + cursor);
                            node_items[child_index] = .{
                                .mesh = .{ .gid = mesh_gid, .dense = mesh_dense, .skin = skin },
                                .model = .{ .gid = model_gid, .dense = model_dense },
                                .pose = null,
                                .parent_transform_relation = .inherit_transform,
                                .local_transform = transform,
                                .self = child_index,
                                .parent = runtime_index,
                                .child_count = 0,
                                .subtree_size = 0,
                            };
                            if (debug_enabled) self.nodes_debug.items[child_index] = .{
                                .node_name = try name_alloc.dupe(u8, build_node.name),
                            };
                            cursor += 1;
                        }
                    },
                }

                if (debug_enabled) self.nodes_debug.items[runtime_index] = .{
                    .node_name = try name_alloc.dupe(u8, build_node.name),
                };

                if (build_node.expose.node) |key| {
                    const stable_id = try self.exposed.expose(alloc, runtime_index);
                    try recordExposedNode(&ts.exposed_groups, key, stable_id);
                }
                if (build_node.expose.pose) |key| {
                    if (pose == null) {
                        base.zprobe.event(.err, "Node doesn't have a pose to expose", .{
                            .group_key = key,
                            .node_name = if (debug_enabled) self.nodes_debug.items[runtime_index].node_name else "",
                        });
                    } else {
                        const node_pose = node_items[runtime_index].pose orelse return error.ExposedNodeHasNoPose;
                        try recordExposedPose(&ts.exposed_groups, key, node_pose.id);
                    }
                }

                try frames.append(alloc, .{
                    .template_index = build_node.template_index,
                    .runtime_index = runtime_index,
                    .subtree_start = cursor - mesh_child_count - 1,
                    .extra_children = mesh_child_count,
                });
            },
            .exit => |exit| {
                if (frames.items.len == 0) return error.InvalidBuildIterator;
                const frame = frames.pop().?;
                if (frame.template_index != exit.template_index) return error.InvalidBuildIterator;
                const node = &node_items[frame.runtime_index];
                node.child_count += exit.child_count;
                node.subtree_size = @intCast(cursor - frame.subtree_start - 1);
            },
        }
    }

    std.debug.assert(cursor == count and frames.items.len == 0);

    var ancestor = target_node_id;
    while (true) {
        node_items[ancestor].subtree_size += @intCast(count);
        if (node_items[ancestor].isRootNode()) break;
        ancestor = node_items[ancestor].parent;
    }
    node_items[target_node_id].child_count += 1;

    try self.dirty_transforms.update(alloc, target_node_id, @intCast(count), false);
    try self.dirty_transforms.add(alloc, .{
        .start = insert_at,
        .end = insert_at + @as(u32, @intCast(count)),
    });
}

fn recordExposedNode(groups: *TemplateScene.ExposedGroups, key: TemplateScene.GroupKey, id: Exposed.Id) !void {
    const group = groups.map.getPtr(key) orelse return error.ExposeGroupNotFound;
    const range = group.range.getPtr(.node);
    if (range.@"1" >= groups.exposed_nodes.len) return error.ExposeCountMismatch;
    groups.exposed_nodes[range.@"1"] = id;
    range.@"1" += 1;
}

fn recordExposedPose(groups: *TemplateScene.ExposedGroups, key: TemplateScene.GroupKey, id: PosePool.PoseId) !void {
    const group = groups.map.getPtr(key) orelse return error.ExposeGroupNotFound;
    const range = group.range.getPtr(.pose);
    if (range.@"1" >= groups.exposed_poses.len) return error.ExposeCountMismatch;
    groups.exposed_poses[range.@"1"] = id;
    range.@"1" += 1;
}

/// `tree` expected to be in DFS pre order
/// after calling the tree root will be placed at `resolved target_node id + 1`, this order won't last through subsequent calls to addTree/removeTree
pub fn addTree(self: *Scene, alloc: Allocator, tree: []const Node, target_node: Exposed.Id, tree_debug: []const NodeDebug, comptime expose: bool) !(if (expose) Exposed.Id else void) {
    std.debug.assert(tree.len == tree_debug.len);
    if (tree.len == 0) return error.EmptyTree;
    const target_node_id = self.exposed.getNodeId(target_node) orelse return error.TargetNotExposed;

    const orig_nodes_len = self.nodes.len;
    const tree_root_id = target_node_id + 1;
    try self.nodes.ensureUnusedCapacity(alloc, tree.len);
    try self.nodes.resize(alloc, orig_nodes_len + tree.len);
    if (debug_enabled) _ = try self.nodes_debug.addManyAsSlice(alloc, tree.len);

    const node_items = self.nodes.items(.node);
    const world_transforms = self.nodes.items(.world_transform);
    @memmove(node_items[tree_root_id + tree.len ..], node_items[tree_root_id..orig_nodes_len]);
    @memcpy(node_items[tree_root_id..][0..tree.len], tree);
    @memmove(world_transforms[tree_root_id + tree.len ..], world_transforms[tree_root_id..orig_nodes_len]);
    @memset(world_transforms[tree_root_id..][0..tree.len], undefined);

    if (debug_enabled) {
        @memmove(self.nodes_debug.items[tree_root_id + tree.len ..], self.nodes_debug.items[tree_root_id..orig_nodes_len]);
        @memcpy(self.nodes_debug.items[tree_root_id..][0..tree.len], tree_debug);
    }

    // propagate up the subtree size change
    var curr_parent = target_node_id;
    node_items[curr_parent].subtree_size += @intCast(tree.len);
    while (!node_items[curr_parent].isRootNode()) {
        curr_parent = node_items[curr_parent].parent;

        node_items[curr_parent].subtree_size += @intCast(tree.len);
    }

    for (node_items[tree_root_id..][0..tree.len]) |*node| {
        node.self += tree_root_id;
        node.parent += tree_root_id;
    }
    node_items[tree_root_id].parent = target_node_id;
    node_items[target_node_id].child_count += 1;

    for (node_items[tree_root_id + tree.len ..]) |*node| {
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
    const node_items = self.nodes.items(.node);
    const world_transforms = self.nodes.items(.world_transform);
    if (node_items[target_node_id].isRootNode()) return error.CannotRemoveRoot;
    const target_subtree_count = node_items[target_node_id].subtree_size;

    const parent_id = node_items[target_node_id].parent;

    const removed = 1 + target_subtree_count;
    const removed_end = target_node_id + removed;

    try self.exposed.removeNodeRange(alloc, .{ .start = target_node_id, .end = removed_end });
    @memmove(node_items[target_node_id .. self.nodes.len - removed], node_items[target_node_id + removed ..]);
    @memmove(world_transforms[target_node_id .. self.nodes.len - removed], world_transforms[target_node_id + removed ..]);
    if (debug_enabled) {
        @memmove(self.nodes_debug.items[target_node_id .. self.nodes.len - removed], self.nodes_debug.items[target_node_id + removed ..]);
    }

    const new_size = self.nodes.len - removed;
    self.nodes.shrinkRetainingCapacity(new_size);
    if (debug_enabled) self.nodes_debug.shrinkRetainingCapacity(new_size);

    // propagate up the subtree size change
    var curr_parent = parent_id;
    node_items[parent_id].child_count -= 1;
    node_items[parent_id].subtree_size -= removed;
    while (!node_items[curr_parent].isRootNode()) {
        curr_parent = node_items[curr_parent].parent;

        node_items[curr_parent].subtree_size -= removed;
    }

    for (self.nodes.items(.node)[target_node_id..]) |*node| {
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
    for (self.nodes.items(.node)[range.start..range.end]) |node| {
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
    for (self.nodes.items(.node)[range.start..range.end]) |node| {
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
        .model = null,
        .pose = null,
        .parent_transform_relation = .inherit_transform,
        .local_transform = base.zmath.identity(),
        .self = self_id,
        .parent = parent_id,
        .child_count = child_count,
        .subtree_size = subtree_size,
    };
}

fn appendTestNode(scene: *Scene, alloc: Allocator, node: Node) !void {
    try scene.nodes.append(alloc, .{
        .node = node,
        .world_transform = base.zmath.identity(),
    });
}

const test_node_debug = NodeDebug{ .node_name = "test" };

test "tree insertion and removal preserve preorder indices and exposed handles" {
    const alloc = std.testing.allocator;
    var scene: Scene = .{};
    defer scene.deinit(alloc);

    // Start with a root and one existing direct child.
    try appendTestNode(&scene, alloc, testNode(0, 0, 1, 1));
    try appendTestNode(&scene, alloc, testNode(1, 0, 0, 0));
    try scene.nodes_debug.appendSlice(alloc, &.{ test_node_debug, test_node_debug });
    const scene_root = try scene.exposed.expose(alloc, 0);
    const existing_child = try scene.exposed.expose(alloc, 1);

    const inserted_root = try scene.addTree(alloc, &.{
        testNode(0, 0, 1, 1),
        testNode(1, 0, 0, 0),
    }, scene_root, &.{ test_node_debug, test_node_debug }, true);
    const inserted_child = try scene.exposed.expose(alloc, 2);

    try std.testing.expectEqual(@as(usize, 4), scene.nodes.len);
    try std.testing.expectEqual(@as(u32, 2), scene.nodes.items(.node)[0].child_count);
    try std.testing.expectEqual(@as(u32, 3), scene.nodes.items(.node)[0].subtree_size);
    try std.testing.expectEqual(@as(u32, 0), scene.nodes.items(.node)[1].parent);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[2].parent);
    try std.testing.expectEqual(@as(u32, 0), scene.nodes.items(.node)[3].parent);
    try std.testing.expectEqual(@as(u32, 3), scene.exposed.getNodeId(existing_child).?);

    try scene.removeTree(alloc, inserted_root);

    try std.testing.expectEqual(@as(usize, 2), scene.nodes.len);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[0].child_count);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[0].subtree_size);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[1].self);
    try std.testing.expectEqual(@as(u32, 0), scene.nodes.items(.node)[1].parent);
    try std.testing.expectEqual(@as(u32, 1), scene.exposed.getNodeId(existing_child).?);
    try std.testing.expect(scene.exposed.getNodeId(inserted_root) == null);
    try std.testing.expect(scene.exposed.getNodeId(inserted_child) == null);
}

test "scene root cannot be removed as a subtree" {
    const alloc = std.testing.allocator;
    var scene: Scene = .{};
    defer scene.deinit(alloc);

    try appendTestNode(&scene, alloc, testNode(0, 0, 0, 0));
    try scene.nodes_debug.append(alloc, test_node_debug);
    const scene_root = try scene.exposed.expose(alloc, 0);

    try std.testing.expectError(error.CannotRemoveRoot, scene.removeTree(alloc, scene_root));
}

test "template scene insertion builds nodes and resolves exposed groups" {
    const alloc = std.testing.allocator;
    var template = TemplateScene.init(alloc, alloc);
    defer template.deinit();

    const node = try template.addNode();
    const group = template.newExposeGroup();
    node.expose(.{ .node = group });

    var prepared = try node.prepareBuild(alloc, undefined);
    defer prepared.deinit(alloc);

    var scene: Scene = .{};
    defer scene.deinit(alloc);
    const scene_root = try scene.initRoot(alloc);
    const target_node = try scene.addTree(alloc, &.{testNode(0, 0, 0, 0)}, scene_root, &.{test_node_debug}, true);
    try scene.addTemplateScene(target_node, &prepared, undefined, alloc, alloc);

    try std.testing.expectEqual(@as(usize, 3), scene.nodes.len);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[0].child_count);
    try std.testing.expectEqual(@as(u32, 2), scene.nodes.items(.node)[0].subtree_size);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[1].self);
    try std.testing.expectEqual(@as(u32, 0), scene.nodes.items(.node)[1].parent);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[1].child_count);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[1].subtree_size);
    try std.testing.expectEqual(@as(u32, 1), scene.nodes.items(.node)[2].parent);

    const exposed_nodes = prepared.exposed_groups.nodes(group);
    try std.testing.expectEqual(@as(usize, 1), exposed_nodes.len);
    try std.testing.expectEqual(@as(?u32, 2), scene.exposed.getNodeId(exposed_nodes[0]));
}
