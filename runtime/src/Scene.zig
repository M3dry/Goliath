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
const DisjointRanges = base.util.DisjointRanges;
const Range = base.util.Range;

// TODO: take in as comptime param to the functins that use this one/carry it comptime on the struct (probably better)
const debug_enabled = build_options.debug_build;

const Scene = @This();

pub const ExposedId = Exposed.Id;
const Exposed = struct {
    pub const Id = struct {
        gen: u32,
        id: u32,
    };

    data: std.ArrayList(struct {
        gen: u32,
        node_id: u32,
    }) = .empty,
    free: std.ArrayList(u32) = .empty,

    /// from node_id to slot
    backlink: std.AutoHashMapUnmanaged(u32, u32) = .empty,

    const Self = @This();

    pub inline fn getNodeId(self: *Self, stable_id: Id) ?u32 {
        if (self.data.items.len <= stable_id.id) return null;

        const d = self.data.items[stable_id.id];
        if (d.gen != stable_id.gen) return null;

        return d.node_id;
    }

    pub fn expose(self: *Self, alloc: Allocator, node_id: u32) !Id {
        if (self.backlink.get(node_id)) |id| {
            return .{
                .gen = self.data.items[id].gen,
                .id = id,
            };
        }

        try self.backlink.ensureUnusedCapacity(alloc, 1);
        if (self.free.items.len == 0) try self.data.ensureUnusedCapacity(alloc, 1);

        if (self.free.pop()) |id| {
            self.data.items[id].gen +%= 1;
            self.data.items[id].node_id = node_id;

            self.backlink.putAssumeCapacity(node_id, id);

            return .{
                .gen = self.data.items[id].gen,
                .id = id,
            };
        }

        const id: u32 = @intCast(self.data.items.len);
        self.backlink.putAssumeCapacity(node_id, id);

        try self.data.append(alloc, .{
            .gen = 0,
            .node_id = node_id,
        });

        return .{
            .gen = 0,
            .id = id,
        };
    }

    pub inline fn unexpose(self: *Self, alloc: Allocator, stable_id: Id) void {
        const node_id = getNodeId(self, stable_id) orelse return;

        _ = self.backlink.remove(node_id);

        self.data.items[stable_id.id].gen +%= 1;
        try self.free.append(alloc, stable_id.id);
    }

    pub fn incrNodeIds(self: *Self, above: u32, delta: i32) void {
        if (delta == 0) return;

        // Remove old keys first so shifted ids cannot shadow an existing
        // backlink while the mapping is being updated.
        for (self.data.items, 0..) |id, slot| {
            if (id.node_id <= above) continue;
            if (self.backlink.get(id.node_id) == @as(u32, @intCast(slot))) _ = self.backlink.remove(id.node_id);
        }

        for (self.data.items, 0..) |*id, slot| {
            if (id.node_id <= above) continue;
            id.node_id = @intCast(@as(i64, id.node_id) + delta);
            // Free slots retain their last node id in `data`, but have no
            // backlink and must not be reinserted.
            if (std.mem.indexOfScalar(u32, self.free.items, @intCast(slot)) == null)
                self.backlink.putAssumeCapacity(id.node_id, @intCast(slot));
        }
    }

    pub fn removeNodeRange(self: *Self, alloc: Allocator, range: Range) !void {
        for (0.., self.data.items) |i, *d| {
            if (!range.in(d.node_id)) continue;

            _ = self.backlink.remove(d.node_id);

            d.gen +%= 1;
            try self.free.append(alloc, @intCast(i));
        }
    }

    pub fn deinit(self: *Self, alloc: Allocator) void {
        self.data.deinit(alloc);
        self.free.deinit(alloc);

        self.backlink.deinit(alloc);

        self.data = .empty;
        self.free = .empty;

        self.backlink = .empty;
    }
};

// on dispatch: 1) check if instance is valid,
//              2) dispatch animation task,
//              3) check the scene node's children to see if they have an attachment on this pose,
//                 if yes, then add to the diry ranges - here we might want to wait until after animation and check if the pose changed at the attachment location (in either case we can only update the world transforms after animation is done)
const AnimatedInstances = struct {
    pub const Id = struct {
        slot: u32,
    };

    const Instance = struct {
        skins: []const base.util.ConstView(Skin) = &.{},
        pose: PosePool.PoseId = undefined,
        joint_range: Range = undefined,
        revision: u64 = std.math.maxInt(u32),
    };

    instances: std.MultiArrayList(Instance) = .empty,
    free_instances: std.ArrayList(u32) = .empty,

    joints: std.ArrayList(Mat) = .empty,
    free_joints: DisjointRanges = .{},

    dirty: bool = false,

    pub fn addInstance(self: *AnimatedInstances, alloc: Allocator, skins: []const base.util.ConstView(Skin), pose_id: PosePool.PoseId, joint_count: u32) !Id {
        const reuse_instance = self.free_instances.items.len != 0;

        if (!reuse_instance) try self.instances.ensureUnusedCapacity(alloc, 1);
        try self.joints.ensureUnusedCapacity(alloc, joint_count);

        const slot: u32 = if (reuse_instance) self.free_instances.pop().? else @intCast(self.instances.addOneAssumeCapacity());

        const joint_range: Range = if (self.free_joints.takeFirstFit(joint_count)) |range| range else blk: {
            const start = self.joints.items.len;
            _ = self.joints.addManyAsSliceAssumeCapacity(joint_count);
            break :blk .{ .start = @intCast(start), .end = @intCast(self.joints.items.len) };
        };

        self.instances.set(slot, .{
            .skins = skins,
            .pose = pose_id,
            .joint_range = joint_range,
            .revision = 0,
        });

        self.dirty = true;
        return .{
            .slot = slot,
        };
    }

    pub fn removeInstance(self: *AnimatedInstances, alloc: Allocator, id: Id) !PosePool.PoseId {
        const inst = self.instances.get(id.slot);

        try self.free_instances.ensureUnusedCapacity(alloc, 1);

        try self.free_joints.add(alloc, inst.joint_range);

        self.instances.set(id.slot, .{});
        self.free_instances.appendAssumeCapacity(id.slot);

        self.dirty = true;
        return inst.pose;
    }

    pub fn isInvalid(self: *AnimatedInstances, id: Id) bool {
        return self.instances.items(.revision)[id.slot] == std.math.maxInt(u32);
    }

    pub fn deinit(self: *AnimatedInstances, alloc: Allocator) void {
        self.instances.deinit(alloc);
        self.free_instances.deinit(alloc);

        self.joints.deinit(alloc);
        self.free_joints.deinit(alloc);

        self.instances = .empty;
        self.free_instances = .empty;

        self.joints = .empty;
        self.free_joints = .{};
    }
};

const DrawableMeshes = struct {
    pub const Id = struct {
        sparse: u32,
    };

    pub const StaticDrawInstance = struct {
        mesh_desc_ix: u32,
        world_transform: [4][4]f32,
    };

    pub const SkinnedDrawInstance = struct {
        mesh_desc_ix: u32,
        joint_offset: u32,
        world_transform: [4][4]f32,
    };

    const Kind = enum { static, skinned };
    const SparseEntry = struct {
        dense: u32,
        kind: Kind,
    };

    static_drawlist: std.MultiArrayList(struct {
        draw: StaticDrawInstance,
        sparse_id: u32,
    }) = .empty,
    static_dirty: bool = false,

    skinned_drawlist: std.MultiArrayList(struct {
        draw: SkinnedDrawInstance,
        sparse_id: u32,
    }) = .empty,
    skinned_dirty: bool = false,

    // Each live Id points to an entry in one of the two dense draw lists.
    sparse: std.ArrayList(SparseEntry) = .empty,

    pub fn addStatic(self: *DrawableMeshes, alloc: Allocator, mesh_dense: u32, world_transform: Mat) !Id {
        const reusable = self.findInvalidSparse();
        try self.static_drawlist.ensureUnusedCapacity(alloc, 1);
        if (reusable == null) try self.sparse.ensureUnusedCapacity(alloc, 1);

        const dense: u32 = @intCast(self.static_drawlist.len);
        const sparse_id: u32 = reusable orelse @intCast(self.sparse.items.len);

        self.static_drawlist.appendAssumeCapacity(.{
            .draw = .{
                .mesh_desc_ix = mesh_dense,
                .world_transform = @bitCast(world_transform),
            },
            .sparse_id = sparse_id,
        });

        if (reusable != null) {
            self.sparse.items[sparse_id] = .{ .dense = dense, .kind = .static };
        } else {
            self.sparse.appendAssumeCapacity(.{ .dense = dense, .kind = .static });
        }

        self.static_dirty = true;
        return .{ .sparse = sparse_id };
    }

    pub fn addSkinned(self: *DrawableMeshes, alloc: Allocator, mesh_dense: u32, joint_offset: u32, world_transform: Mat) !Id {
        const reusable = self.findInvalidSparse();
        try self.skinned_drawlist.ensureUnusedCapacity(alloc, 1);
        if (reusable == null) try self.sparse.ensureUnusedCapacity(alloc, 1);

        const dense: u32 = @intCast(self.skinned_drawlist.len);
        const sparse_id: u32 = reusable orelse @intCast(self.sparse.items.len);
        self.skinned_drawlist.appendAssumeCapacity(.{
            .draw = .{
                .mesh_desc_ix = mesh_dense,
                .joint_offset = joint_offset,
                .world_transform = @bitCast(world_transform),
            },
            .sparse_id = sparse_id,
        });

        if (reusable != null) {
            self.sparse.items[sparse_id] = .{ .dense = dense, .kind = .skinned };
        } else {
            self.sparse.appendAssumeCapacity(.{ .dense = dense, .kind = .skinned });
        }

        self.skinned_dirty = true;
        return .{ .sparse = sparse_id };
    }

    fn findInvalidSparse(self: *const DrawableMeshes) ?u32 {
        for (self.sparse.items, 0..) |entry, i| {
            if (entry.dense == std.math.maxInt(u32)) return @intCast(i);
        }
        return null;
    }

    pub fn updateTransform(self: *DrawableMeshes, id: Id, world_transform: Mat) void {
        if (id.sparse >= self.sparse.items.len) return;

        const entry = self.sparse.items[id.sparse];
        if (entry.dense == std.math.maxInt(u32)) return;

        switch (entry.kind) {
            .static => {
                self.static_dirty = true;
                self.static_drawlist.slice().items(.draw)[entry.dense].world_transform = @bitCast(world_transform);
            },
            .skinned => {
                self.skinned_dirty = true;
                self.skinned_drawlist.slice().items(.draw)[entry.dense].world_transform = @bitCast(world_transform);
            },
        }
    }

    pub fn remove(self: *DrawableMeshes, id: Id) void {
        if (id.sparse >= self.sparse.items.len) return;

        const entry = self.sparse.items[id.sparse];
        if (entry.dense == std.math.maxInt(u32)) return;

        switch (entry.kind) {
            .static => {
                const last: u32 = @intCast(self.static_drawlist.len - 1);
                const moved_sparse_id = self.static_drawlist.slice().items(.sparse_id)[last];

                _ = self.static_drawlist.swapRemove(entry.dense);
                if (entry.dense != last) {
                    self.sparse.items[moved_sparse_id].dense = entry.dense;
                }

                self.static_dirty = true;
            },
            .skinned => {
                const last: u32 = @intCast(self.skinned_drawlist.len - 1);
                const moved_sparse_id = self.skinned_drawlist.slice().items(.sparse_id)[last];

                _ = self.skinned_drawlist.swapRemove(entry.dense);
                if (entry.dense != last) {
                    self.sparse.items[moved_sparse_id].dense = entry.dense;
                }

                self.skinned_dirty = true;
            },
        }
        self.sparse.items[id.sparse].dense = std.math.maxInt(u32);
    }

    pub fn deinit(self: *DrawableMeshes, alloc: Allocator) void {
        self.static_drawlist.deinit(alloc);
        self.skinned_drawlist.deinit(alloc);
        self.sparse.deinit(alloc);

        self.static_drawlist = .empty;
        self.skinned_drawlist = .empty;
        self.sparse = .empty;
    }
};

pub const GPUBuffers = struct {
    joints: [2]base.Buffer = .{ .empty, .empty },
    staging_joints_ix: u32 = 0,
    staging_joints_ticket: base.Transport.GroupTicket = .none,

    static_drawlist: [2]base.Buffer = .{ .empty, .empty },
    staging_static_drawlist_ix: u32 = 0,
    staging_static_drawlist_ticket: base.Transport.GroupTicket = .none,

    skinned_drawlist: [2]base.Buffer = .{ .empty, .empty },
    staging_skinned_drawlist_ix: u32 = 0,
    staging_skinned_drawlist_ticket: base.Transport.GroupTicket = .none,

    pub fn updateJoints(self: *GPUBuffers, ctx: base.Ctx.Query(&.{ .transport, .destroy_queue, .device, .vma_allocator, .graphics_family, .transport_family }), joints: []Mat) !void {
        try GPUBuffers.genericUpdate(Mat, .from(ctx), .{
            .vertex_shader_bit = true,
        }, &self.joints, &self.staging_joints_ix, &self.staging_joints_ticket, joints, "joints");
    }

    pub fn updateStaticDrawlist(self: *GPUBuffers, ctx: base.Ctx.Query(&.{ .transport, .destroy_queue, .device, .vma_allocator, .graphics_family, .transport_family }), drawlist: []DrawableMeshes.StaticDrawInstance) !void {
        try GPUBuffers.genericUpdate(DrawableMeshes.StaticDrawInstance, .from(ctx), .{ .compute_shader_bit = true }, &self.static_drawlist, &self.staging_static_drawlist_ix, &self.staging_static_drawlist_ticket, drawlist, "static_drawlist");
    }

    pub fn updateSkinnedDrawlist(self: *GPUBuffers, ctx: base.Ctx.Query(&.{ .transport, .destroy_queue, .device, .vma_allocator, .graphics_family, .transport_family }), drawlist: []DrawableMeshes.SkinnedDrawInstance) !void {
        try GPUBuffers.genericUpdate(DrawableMeshes.SkinnedDrawInstance, .from(ctx), .{ .compute_shader_bit = true }, &self.skinned_drawlist, &self.staging_skinned_drawlist_ix, &self.staging_skinned_drawlist_ticket, drawlist, "skinned_drawlist");
    }

    fn genericUpdate(comptime T: type, ctx: base.Ctx.Query(&.{ .transport, .destroy_queue, .device, .vma_allocator, .graphics_family, .transport_family }), pipeline_stages: base.vk.PipelineStageFlags2, buffers: *[2]base.Buffer, staging_ix: *u32, ticket: *base.Transport.GroupTicket, data: []T, comptime buf_name: [:0]const u8) !void {
        if (try ctx.view.transport.isGroupReady(ticket.*)) {
            staging_ix.* = (staging_ix.* + 1) % 2;
        }

        const staging_buf = &buffers[staging_ix.*];
        var first_use = false;
        if (staging_buf.size < data.len * @sizeOf(T)) {
            staging_buf.deinit(.from(ctx));

            staging_buf.* = try .init(.from(ctx), .graphics, buf_name, data.len * @sizeOf(T), .{ .transfer_dst_bit = true, .storage_buffer_bit = true }, .gpu_only);
            first_use = true;
        }

        if (std.meta.eql(staging_buf.*, .empty)) return;

        var gu = try ctx.view.transport.beginGroup();
        errdefer gu.cancel(true);
        ticket.* = try gu.uploadBufferStagedFinal(false, std.mem.sliceAsBytes(data), staging_buf.*.handle, 0, pipeline_stages, .{ .shader_read_bit = true }, first_use);
        return;
    }

    pub fn deinit(self: *GPUBuffers, ctx: base.Ctx.Query(&.{ .transport, .destroy_queue })) void {
        ctx.view.transport.unqueueGroup(self.staging_joints_ticket, true);
        for (0..self.joints.len) |i| self.joints[i].deinit(.from(ctx));

        ctx.view.transport.unqueueGroup(self.staging_static_drawlist_ticket, true);
        for (0..self.static_drawlist.len) |i| self.static_drawlist[i].deinit(.from(ctx));

        ctx.view.transport.unqueueGroup(self.staging_skinned_drawlist_ticket, true);
        for (0..self.skinned_drawlist.len) |i| self.skinned_drawlist[i].deinit(.from(ctx));

        self.* = .{};
    }
};

const Attachments = struct {};

pub const NodeDebug = struct {
    node_name: []const u8,
};

pub const Node = struct {
    pub const Payload = union(enum) {
        mesh: struct {
            gid: Gid,
            dense: u32,

            drawable_id: DrawableMeshes.Id,
        },
        model_mesh: struct {
            gid: Gid,
            dense: u32,
            joint_offset: u32 = std.math.maxInt(u32),

            drawable_id: DrawableMeshes.Id,
        },
        model_anchor: struct {
            model_gid: Gid,
            model_dense: u32,
            animation: ?AnimatedInstances.Id,
        },
        nothing,
    };

    // Model anchors carry the pose; their mesh children refer back to the model.
    payload: Payload,

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
animated_instances: AnimatedInstances = .{},
drawable_meshes: DrawableMeshes = .{},

nodes_debug: std.ArrayList(NodeDebug) = .empty,
nodes: std.MultiArrayList(struct {
    world_transform: Mat,
    node: Node,
}) = .empty, // topo sorted; transform storage is a contiguous field for GPU uploads

dirty_transforms: DisjointRanges = .{},

exposed: Exposed = .{},
attachments: Attachments = .{},

pub fn initRoot(self: *Scene, alloc: Allocator) !Exposed.Id {
    if (self.nodes.len != 0) return error.SceneNotEmpty;

    if (debug_enabled) try self.nodes_debug.append(alloc, .{
        .node_name = "root",
    });

    try self.nodes.append(alloc, .{
        .node = .{
            .payload = .nothing,
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
    self.animated_instances.deinit(alloc);
    self.drawable_meshes.deinit(alloc);

    if (debug_enabled) self.nodes_debug.deinit(alloc);
    self.nodes.deinit(alloc);
    self.dirty_transforms.deinit(alloc);
    self.exposed.deinit(alloc);

    self.pose_pool = .{};
    self.animated_instances = .{};
    self.drawable_meshes = .{};

    if (debug_enabled) self.nodes_debug = .empty;
    self.nodes = .empty;
    self.dirty_transforms = .{};
    self.exposed = .{};
}

pub fn rebuildDirtyTransforms(self: *Scene) void {
    const nodes = self.nodes.items(.node);
    const world_transforms = self.nodes.items(.world_transform);
    while (self.dirty_transforms.ranges.pop()) |range| {
        for (range.start..range.end) |ix| {
            const node = &nodes[ix];
            if (node.isRootNode()) {
                world_transforms[ix] = node.local_transform;
            } else {
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

            const drawable_id = switch (node.payload) {
                .mesh => |mesh| mesh.drawable_id,
                .model_mesh => |mesh| mesh.drawable_id,
                .model_anchor, .nothing => continue,
            };
            self.drawable_meshes.updateTransform(drawable_id, world_transforms[ix]);
        }
    }
}

pub fn collectIntoGPUBuffers(self: *Scene, ctx: base.Ctx.Query(&.{ .transport, .destroy_queue, .device, .vma_allocator, .graphics_family, .transport_family }), gpu_buffers: *GPUBuffers) !void {
    if (self.animated_instances.dirty) {
        self.animated_instances.dirty = false;
        try gpu_buffers.updateJoints(.from(ctx), self.animated_instances.joints.items);
    }

    if (self.drawable_meshes.static_dirty) {
        self.drawable_meshes.static_dirty = false;
        try gpu_buffers.updateStaticDrawlist(.from(ctx), self.drawable_meshes.static_drawlist.items(.draw));
    }

    if (self.drawable_meshes.skinned_dirty) {
        base.zprobe.event(.debug, "collecting skinned drawlist", .{});
        try gpu_buffers.updateSkinnedDrawlist(.from(ctx), self.drawable_meshes.skinned_drawlist.items(.draw));
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
                var pose: ?PosePool.PoseId = null;
                var mesh_child_count: u32 = 0;

                switch (build_node.payload) {
                    .nothing => {
                        node_items[runtime_index] = .{
                            .payload = .nothing,
                            .parent_transform_relation = .inherit_transform,
                            .local_transform = local_transform,
                            .self = runtime_index,
                            .parent = parent,
                            .child_count = 0,
                            .subtree_size = 0,
                        };
                        cursor += 1;
                    },
                    .mesh => |gid| {
                        const dense = as.denseIndex(gid);
                        const drawable_id = try self.drawable_meshes.addStatic(alloc, dense, base.zmath.identity());
                        errdefer self.drawable_meshes.remove(drawable_id);

                        node_items[runtime_index] = .{
                            .payload = .{ .mesh = .{ .gid = gid, .dense = as.denseIndex(gid), .drawable_id = drawable_id } },
                            .parent_transform_relation = .inherit_transform,
                            .local_transform = local_transform,
                            .self = runtime_index,
                            .parent = parent,
                            .child_count = 0,
                            .subtree_size = 0,
                        };
                        cursor += 1;
                    },
                    .model => |m| {
                        const model_dense = as.denseIndex(m.gid);

                        const meshes = as.model_reg.getMeshDenseIds(model_dense);
                        const transforms = as.model_reg.getMeshTransforms(model_dense);
                        const skins = as.model_reg.getMeshSkins(model_dense);
                        const mesh_gids = as.model_reg.getMeshGids(model_dense);
                        const skeleton = as.model_reg.getSkeleton(model_dense);

                        if (meshes.len != transforms.len or meshes.len != skins.len or meshes.len != mesh_gids.len)
                            return error.ModelMeshDataLengthMismatch;
                        mesh_child_count = @intCast(meshes.len);

                        pose = if (skeleton) |s| blk: {
                            if (m.pose_overwrite) |p| {
                                if (p.skeleton != s.dense) {
                                    base.zprobe.event(.err, "Pose overwrite isn't the same skeleton", .{
                                        .overwritten_skeleton = p.skeleton,
                                        .needed_skeleton = s.dense,
                                    });
                                    break :blk null;
                                }

                                break :blk p;
                            }

                            if (build_node.expose.pose == null) break :blk null;

                            if (self.pose_pool.pools.items.len <= s.dense or self.pose_pool.pools.items[s.dense].isUninitialized()) {
                                const skeleton_data = as.skeleton_reg.getSkeleton(s.dense);
                                try self.pose_pool.newSkeletonPool(
                                    alloc,
                                    s.dense,
                                    @intCast(skeleton_data.bind_local_transforms.len),
                                );
                            }

                            break :blk self.pose_pool.newPose(alloc, s.dense) catch unreachable;
                        } else if (m.pose_overwrite) |_| blk: {
                            base.zprobe.event(.err, "Trying to overwrite pose for a static model", .{});
                            break :blk null;
                        } else null;

                        var joint_offset_start: u32 = 0;
                        node_items[runtime_index] = .{
                            .payload = .{ .model_anchor = .{
                                .model_gid = m.gid,
                                .model_dense = model_dense,
                                .animation = if (pose) |p| blk: {
                                    var joint_count: u32 = 0;
                                    for (skeleton.?.skins) |s| joint_count += @intCast(s.inverse_bind_matrices.len);

                                    try self.pose_pool.acquirePose(p);
                                    const id = try self.animated_instances.addInstance(alloc, skeleton.?.skins, p, joint_count);
                                    joint_offset_start = self.animated_instances.instances.items(.joint_range)[id.slot].start;
                                    break :blk id;
                                } else null,
                            } },
                            .parent_transform_relation = .inherit_transform,
                            .local_transform = local_transform,
                            .self = runtime_index,
                            .parent = parent,
                            .child_count = mesh_child_count,
                            .subtree_size = 0,
                        };
                        cursor += 1;

                        errdefer for (insert_at + 1..insert_at + cursor) |i| self.drawable_meshes.remove(node_items[i].payload.model_mesh.drawable_id);

                        for (meshes, transforms, skins, mesh_gids) |mesh_dense, transform, skin, mesh_gid| {
                            if (skin != std.math.maxInt(u32) and skeleton == null) return error.SkinWithoutSkeleton;

                            const joint_offset: u32, const drawable_id: DrawableMeshes.Id = if (pose) |_| blk: {
                                var sum: u32 = joint_offset_start;
                                for (0..skin) |s_ix| sum += @intCast(skeleton.?.skins[s_ix].inverse_bind_matrices.len);

                                break :blk .{ sum, try self.drawable_meshes.addSkinned(alloc, mesh_dense, sum, base.zmath.identity()) };
                            } else .{ std.math.maxInt(u32), try self.drawable_meshes.addStatic(alloc, mesh_dense, base.zmath.identity()) };

                            const child_index: u32 = @intCast(insert_at + cursor);
                            node_items[child_index] = .{
                                .payload = .{ .model_mesh = .{ .gid = mesh_gid, .dense = mesh_dense, .joint_offset = joint_offset, .drawable_id = drawable_id } },
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
                        try recordExposedPose(&ts.exposed_groups, key, pose.?);
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

pub fn removeTree(self: *Scene, alloc: Allocator, cmd_buf: *AssetSystem.CommandBuffer, target_node: Exposed.Id) !void {
    const target_node_id = self.exposed.getNodeId(target_node) orelse return error.TargetNotExposed;
    const node_items = self.nodes.items(.node);
    const world_transforms = self.nodes.items(.world_transform);
    if (node_items[target_node_id].isRootNode()) return error.CannotRemoveRoot;

    // A generated model mesh is owned by its model anchor. Removing only that
    // mesh removes its descendants, while preserving the mesh and its asset.
    const preserve_target = switch (node_items[target_node_id].payload) {
        .model_mesh => true,
        else => false,
    };
    const range_start = target_node_id + @intFromBool(preserve_target);
    const removed = if (preserve_target)
        node_items[target_node_id].subtree_size
    else
        1 + node_items[target_node_id].subtree_size;
    if (removed == 0) return;
    const removed_end = range_start + removed;

    // Release resources while the node records are still available. Model
    // mesh gids are owned by their anchor and are intentionally not released.
    for (node_items[range_start..removed_end]) |node| {
        switch (node.payload) {
            .mesh => |mesh| {
                self.drawable_meshes.remove(mesh.drawable_id);
                try cmd_buf.release(mesh.gid);
            },
            .model_mesh => |mesh| self.drawable_meshes.remove(mesh.drawable_id),
            .model_anchor => |anchor| {
                if (anchor.animation) |animation| {
                    const pose = try self.animated_instances.removeInstance(alloc, animation);
                    try self.pose_pool.releasePose(alloc, pose);
                }
                try cmd_buf.release(anchor.model_gid);
            },
            .nothing => {},
        }
    }

    const parent_id = if (preserve_target) target_node_id else node_items[target_node_id].parent;

    try self.exposed.removeNodeRange(alloc, .{ .start = range_start, .end = removed_end });
    @memmove(node_items[range_start .. self.nodes.len - removed], node_items[removed_end..]);
    @memmove(world_transforms[range_start .. self.nodes.len - removed], world_transforms[removed_end..]);
    if (debug_enabled) {
        @memmove(self.nodes_debug.items[range_start .. self.nodes.len - removed], self.nodes_debug.items[removed_end..]);
    }

    const new_size = self.nodes.len - removed;
    self.nodes.shrinkRetainingCapacity(new_size);
    if (debug_enabled) self.nodes_debug.shrinkRetainingCapacity(new_size);

    // propagate up the subtree size change
    var curr_parent = parent_id;
    if (preserve_target) {
        node_items[parent_id].child_count = 0;
    } else {
        node_items[parent_id].child_count -= 1;
    }
    node_items[parent_id].subtree_size -= removed;
    while (!node_items[curr_parent].isRootNode()) {
        curr_parent = node_items[curr_parent].parent;

        node_items[curr_parent].subtree_size -= removed;
    }

    for (self.nodes.items(.node)[range_start..]) |*node| {
        node.self -= removed;
        if (node.parent >= removed_end) node.parent -= removed;
    }

    self.exposed.incrNodeIds(removed_end - 1, -@as(i32, @intCast(removed)));

    try self.dirty_transforms.remove(alloc, .{
        .start = range_start,
        .end = removed_end,
    });

    try self.dirty_transforms.update(alloc, removed_end, removed, true);
}

pub fn requestAllMeshDeps(self: *const Scene, cmd_buf: *AssetSystem.CommandBuffer, range: Range) !void {
    // not very efficient, later on the scene will be split into chunks by which these will get requested + GPU requesting
    for (self.nodes.items(.node)[range.start..range.end]) |node| {
        const mesh_dense = switch (node.payload) {
            .mesh => |mesh| mesh.dense,
            .model_mesh => |mesh| mesh.dense,
            else => continue,
        };

        const lod_range = cmd_buf.asset_system.mesh_reg.getLodRange(mesh_dense);
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
        const mesh_dense = switch (node.payload) {
            .mesh => |mesh| mesh.dense,
            .model_mesh => |mesh| mesh.dense,
            else => continue,
        };
        const lod_range = cmd_buf.asset_system.mesh_reg.getLodRange(mesh_dense);
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
    var ranges = DisjointRanges{};
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
    var ranges: DisjointRanges = .{};
    defer ranges.deinit(alloc);

    try ranges.add(alloc, .{ .start = 1, .end = 3 });
    try ranges.add(alloc, .{ .start = 5, .end = 7 });
    try ranges.add(alloc, .{ .start = 9, .end = 11 });
    try ranges.add(alloc, .{ .start = 3, .end = 9 });

    try std.testing.expectEqualSlices(Range, &.{.{ .start = 1, .end = 11 }}, ranges.ranges.items);
}

test "dirty ranges remove trims splits and spans ranges" {
    const alloc = std.testing.allocator;
    var ranges: DisjointRanges = .{};
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
    var ranges: DisjointRanges = .{};
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
        .payload = .nothing,
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
    const assets: AssetSystem = undefined; // TODO: maybe implement an actual dummy AssetSystem for testing
    var cmd_buf = AssetSystem.CommandBuffer.init(&assets, alloc);
    defer cmd_buf.deinit();

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

    try scene.removeTree(alloc, &cmd_buf, inserted_root);

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
    const assets: AssetSystem = undefined;
    var cmd_buf = AssetSystem.CommandBuffer.init(&assets, alloc);
    defer cmd_buf.deinit();

    try appendTestNode(&scene, alloc, testNode(0, 0, 0, 0));
    try scene.nodes_debug.append(alloc, test_node_debug);
    const scene_root = try scene.exposed.expose(alloc, 0);

    try std.testing.expectError(error.CannotRemoveRoot, scene.removeTree(alloc, &cmd_buf, scene_root));
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
