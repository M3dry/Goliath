const std = @import("std");
const base = @import("base");
const zmesh = @import("zmesh");

const Allocator = std.mem.Allocator;
const zm = base.zmath;
const zcgltf = zmesh.io.zcgltf;

const Skeleton = @import("Skeleton.zig");

const Skin = @This();

skeleton_node_indices: []u32,
inverse_bind_matrices: []zm.Mat,

pub fn deinit(self: *const Skin, alloc: Allocator) void {
    alloc.free(self.skeleton_node_indices);
    alloc.free(self.inverse_bind_matrices);
}

pub fn fromGltf(
    alloc: Allocator,
    data: *zcgltf.Data,
    skin_index: u32,
    skeleton: *const Skeleton,
) !Skin {
    const gltf_skin = &data.skins.?[skin_index];
    const count = gltf_skin.joints_count;
    const nodes_slice = data.nodes.?[0..data.nodes_count];

    const skeleton_node_indices = try alloc.alloc(u32, count);
    errdefer alloc.free(skeleton_node_indices);

    for (0..count) |i| {
        const joint_node = gltf_skin.joints[i];
        const gltf_ix = @as(u32, @intCast(
            (@intFromPtr(joint_node) - @intFromPtr(nodes_slice.ptr)) / @sizeOf(zcgltf.Node),
        ));

        var local_ix: u32 = std.math.maxInt(u32);
        for (skeleton.node_map, 0..) |map_entry, mi| {
            if (map_entry == gltf_ix) {
                local_ix = @intCast(mi);
                break;
            }
        }
        if (local_ix == std.math.maxInt(u32)) return error.JointNotInSkeleton;
        skeleton_node_indices[i] = local_ix;
    }

    const inverse_bind_matrices = if (gltf_skin.inverse_bind_matrices) |ibm| blk: {
        const float_count = ibm.unpackFloatsCount();
        const tmp = try alloc.alloc(f32, float_count);
        defer alloc.free(tmp);
        _ = ibm.unpackFloats(tmp);

        const mats = try alloc.alloc(zm.Mat, count);
        errdefer alloc.free(mats);
        for (0..count) |j| {
            var arr: [16]f32 = undefined;
            @memcpy(&arr, tmp[j * 16 ..][0..16]);
            mats[j] = zm.matFromArr(arr);
        }
        break :blk mats;
    } else blk: {
        const mats = try alloc.alloc(zm.Mat, count);
        errdefer alloc.free(mats);
        for (0..count) |j| {
            mats[j] = zm.identity();
        }
        break :blk mats;
    };
    errdefer alloc.free(inverse_bind_matrices);

    return .{
        .skeleton_node_indices = skeleton_node_indices,
        .inverse_bind_matrices = inverse_bind_matrices,
    };
}
