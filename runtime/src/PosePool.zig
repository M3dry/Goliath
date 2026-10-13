const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;
const PosePool = @This();

const AssetSystem = @import("AssetSystem.zig");
const TRS = base.util.TRS;
const zm = base.zmath;
const Animation = @import("Animation.zig");

const cs = base.util.const_view;

pub const Pose = struct {
    local: []TRS,
    model: []zm.Mat,

    skeleton: u32,

    pub fn resetPose(self: *Pose, as: *const AssetSystem) !void {
        const skeleton = as.skeleton_reg.getSkeleton(self.skeleton);
        std.debug.assert(skeleton.bind_local_transforms.len == self.local.len);

        @memcpy(self.local, skeleton.bind_local_transforms);
        try Animation.buildModelPose(skeleton, self.local, self.model);
    }
};

pub const ConstPose = cs.ConstView(Pose);

pub const SkeletonPool = struct {
    pub const StableId = packed struct(u32) {
        gen: u8,
        slot: u24,
    };

    skeleton: u32,
    joint_count: u32,

    // TODO: merge local and model via MultiArrayList
    local: std.ArrayList(TRS) = .empty,
    model: std.ArrayList(zm.Mat) = .empty,

    // TODO: merge revisions with slots via MultiArrayList
    revisions: std.ArrayList(u64) = .empty,

    generations: std.ArrayList(u8) = .empty,
    free_slots: std.ArrayList(u32) = .empty,

    pub const uninitialized: SkeletonPool = .{
        .skeleton = std.math.maxInt(u32),
        .joint_count = std.math.maxInt(u32),
    };

    pub inline fn isUninitialized(self: SkeletonPool) bool {
        return std.meta.eql(self, SkeletonPool.uninitialized);
    }

    pub fn deinit(self: *SkeletonPool, alloc: Allocator) void {
        self.local.deinit(alloc);
        self.model.deinit(alloc);

        self.revisions.deinit(alloc);

        self.generations.deinit(alloc);
        self.free_slots.deinit(alloc);
    }

    pub fn newPose(self: *SkeletonPool, alloc: Allocator) !StableId {
        if (self.free_slots.pop()) |slot_ix| {
            const gen = &self.generations.items[slot_ix];
            gen.* +%= 1;

            return .{
                .gen = gen.*,
                .slot = @intCast(slot_ix),
            };
        } else {
            _ = try self.local.addManyAsSlice(alloc, self.joint_count);
            _ = try self.model.addManyAsSlice(alloc, self.joint_count);

            try self.revisions.append(alloc, 0);

            try self.generations.append(alloc, 0);

            return .{
                .gen = 0,
                .slot = @intCast(self.generations.items.len - 1),
            };
        }
    }

    pub fn removePose(self: *SkeletonPool, alloc: Allocator, id: StableId) !void {
        try self.checkId(id);

        self.revisions.items[id.slot] = 0;
        self.generations.items[id.slot] +%= 1;

        try self.free_slots.append(alloc, id.slot);
    }

    fn checkId(self: *SkeletonPool, id: StableId) !void {
        if (self.generations.items.len <= id.slot) return error.SlotOutOfRange;

        const gen = self.generations.items[id.slot];
        if (gen != id.gen) return error.GenerationMismatch;
    }

    inline fn getIx(self: *SkeletonPool, id: StableId) usize {
        return id.slot * self.joint_count;
    }

    fn getPose(self: *SkeletonPool, id: StableId) !Pose {
        try self.checkId(id);
        const ix = self.getIx(id);

        return .{
            .local = self.local.items[ix..][0..self.joint_count],
            .model = self.model.items[ix..][0..self.joint_count],
            .skeleton = self.skeleton,
        };
    }

    pub fn getConstPose(self: *SkeletonPool, id: StableId) !ConstPose {
        return cs.toConstView(try self.getPose(id));
    }

    pub fn editPose(self: *SkeletonPool, id: StableId) !Pose {
        return self.getPose(id);
    }

    pub fn finalizeEdit(self: *SkeletonPool, id: StableId) !void {
        _ = try self.checkId(id);

        self.revisions.items[id.slot] += 1;
    }

    pub fn getRevision(self: *SkeletonPool, id: StableId) !u64 {
        try self.checkId(id);

        return self.revisions.items[id.slot];
    }
};

pub const PoseId = packed struct(u64) {
    skeleton: u32,
    gen: u8,
    slot: u24,
};

pools: std.ArrayList(SkeletonPool) = .empty,

// Instead of passing in the joint count via args we could pass in the skeleton registry and look it up there, but that seems unnecessarily complex
/// skeleton needs to stay acquired (`skeleton_dense` needs to stay valid) until `removeSkeletonPool` or `deinit` are called
pub fn newSkeletonPool(self: *PosePool, alloc: Allocator, skeleton_dense: u32, joint_count: u32) !void {
    if (self.pools.items.len > skeleton_dense) {
        if (!self.pools.items[skeleton_dense].isUninitialized()) return error.SkeletonPoolAlreadyInitialized;

        self.pools.items[skeleton_dense] = .{
            .skeleton = skeleton_dense,
            .joint_count = joint_count,
        };

        return;
    }

    @memset(try self.pools.addManyAsSlice(alloc, skeleton_dense - self.pools.items.len + 1), SkeletonPool.uninitialized);

    self.pools.items[skeleton_dense] = .{
        .skeleton = skeleton_dense,
        .joint_count = joint_count,
    };
}

pub fn removeSkeletonPool(self: *PosePool, alloc: Allocator, skeleton: u32) void {
    if (self.pools.items.len <= skeleton) return;

    const pool = &self.pools.items[skeleton];
    if (pool.isUninitialized()) return;

    pool.deinit(alloc);
}

pub fn newPose(self: *PosePool, alloc: Allocator, skeleton: u32) !PoseId {
    if (self.pools.items.len <= skeleton) return error.NoSkeletonPool;

    const pool = &self.pools.items[skeleton];
    const stable_id = try pool.newPose(alloc);

    return .{
        .gen = stable_id.gen,
        .slot = stable_id.slot,
        .skeleton = skeleton,
    };
}

pub fn removePose(self: *PosePool, alloc: Allocator, pose: PoseId) !void {
    if (self.pools.items.len <= pose.skeleton) return;

    const pool = &self.pools.items[pose.skeleton];
    pool.removePose(alloc, .{
        .gen = pose.gen,
        .slot = pose.slot,
    }) catch return;
}

pub fn getConstPose(self: *PosePool, pose: PoseId) !ConstPose {
    if (self.pools.items.len <= pose.skeleton) return error.NoSkeletonPool;

    const pool = &self.pools.items[pose.skeleton];
    return pool.getConstPose(.{
        .gen = pose.gen,
        .slot = pose.slot,
    });
}

pub const Finalizer = struct {
    pose: PoseId,

    pub fn finalize(self: Finalizer, pool: *PosePool) void {
        if (pool.pools.items.len <= self.pose.skeleton) return;

        const skeleton_pool = &pool.pools.items[self.pose.skeleton];
        skeleton_pool.finalizeEdit(.{
            .gen = self.pose.gen,
            .slot = self.pose.slot,
        }) catch {};
    }
};

pub fn editPose(self: *PosePool, pose: PoseId) !struct{Finalizer, Pose} {
    if (self.pools.items.len <= pose.skeleton) return error.NoSkeletonPool;

    const pool = &self.pools.items[pose.skeleton];
    const p = try pool.editPose(.{
        .gen = pose.gen,
        .slot = pose.slot,
    });

    return .{
        .{ .pose = pose },
        p
    };
}

pub fn getRevision(self: *PosePool, pose: PoseId) !u64 {
    if (self.pools.items.len <= pose.skeleton) return error.NoSkeletonPool;

    const pool = &self.pools.items[pose.skeleton];
    return pool.getRevision(.{
        .gen = pose.gen,
        .slot = pose.slot
    });
}

pub fn deinit(self: *PosePool, alloc: Allocator) void {
    for (self.pools.items) |*pool| {
        pool.deinit(alloc);
    }

    self.pools.deinit(alloc);
}

test {
    std.testing.refAllDecls(@This());
}
