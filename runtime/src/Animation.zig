const std = @import("std");
const base = @import("base");
const zmesh = @import("zmesh");

const Allocator = std.mem.Allocator;
const zm = base.zmath;
const zcgltf = zmesh.io.zcgltf;

const Skeleton = @import("Skeleton.zig");
const Skin = @import("Skin.zig");

const Animation = @This();

pub const Path = enum { translation, rotation, scale };
pub const Interpolation = enum { linear, step, cubic_spline };

pub const Channel = struct {
    skeleton_node_index: u32,
    path: Path,
    sampler_index: u32,
};

pub const Sampler = struct {
    interpolation: Interpolation,
    input: []f32,
    output: []f32,
};

fn pathFromGltf(path: zcgltf.AnimationPathType) ?Path {
    return switch (path) {
        .translation => .translation,
        .rotation => .rotation,
        .scale => .scale,
        else => null,
    };
}

fn componentCount(path: Path) usize {
    return switch (path) {
        .translation, .scale => 3,
        .rotation => 4,
    };
}

channels: []Channel,
samplers: []Sampler,
duration: f32,

pub fn deinit(self: *const Animation, alloc: Allocator) void {
    alloc.free(self.channels);
    for (self.samplers) |s| {
        alloc.free(s.input);
        alloc.free(s.output);
    }
    alloc.free(self.samplers);
}

pub fn fromGltf(
    alloc: Allocator,
    data: *zcgltf.Data,
    anim_index: u32,
    node_map: []u32,
) !Animation {
    const gltf_anim = &data.animations.?[anim_index];
    const nodes_slice = data.nodes.?[0..data.nodes_count];
    const gltf_samplers = gltf_anim.samplers[0..gltf_anim.samplers_count];

    var duration: f32 = 0;

    const samplers = try alloc.alloc(Sampler, gltf_anim.samplers_count);
    var samplers_ok: usize = 0;
    errdefer {
        for (0..samplers_ok) |i| {
            alloc.free(samplers[i].input);
            alloc.free(samplers[i].output);
        }
        alloc.free(samplers);
    }

    for (0..gltf_anim.samplers_count) |i| {
        const gs = &gltf_samplers[i];

        const input = try alloc.alloc(f32, gs.input.unpackFloatsCount());
        errdefer alloc.free(input);
        _ = gs.input.unpackFloats(input);
        if (input.len == 0) return error.EmptyAnimationSampler;
        for (input, 0..) |key_time, key_index| {
            if (!std.math.isFinite(key_time)) return error.InvalidAnimationKeyTime;
            if (key_index > 0 and key_time <= input[key_index - 1]) return error.AnimationKeyTimesNotStrictlyIncreasing;
        }
        if (input.len > 0) duration = @max(duration, input[input.len - 1]);

        const output = try alloc.alloc(f32, gs.output.unpackFloatsCount());
        errdefer alloc.free(output);
        _ = gs.output.unpackFloats(output);
        for (output) |value| if (!std.math.isFinite(value)) return error.InvalidAnimationValue;

        const interp: Interpolation = switch (gs.interpolation) {
            .linear => .linear,
            .step => .step,
            .cubic_spline => .cubic_spline,
        };

        samplers[i] = .{
            .interpolation = interp,
            .input = input,
            .output = output,
        };
        samplers_ok = i + 1;
    }

    var valid_count: usize = 0;
    for (0..gltf_anim.channels_count) |i| {
        const chan = gltf_anim.channels[i];
        if (pathFromGltf(chan.target_path) != null and chan.target_node != null) {
            const node = chan.target_node.?;
            const gltf_ix = @as(u32, @intCast(
                (@intFromPtr(node) - @intFromPtr(nodes_slice.ptr)) / @sizeOf(zcgltf.Node),
            ));
            for (node_map) |me| if (me == gltf_ix) {
                valid_count += 1;
                break;
            };
        }
    }

    const channels = try alloc.alloc(Channel, valid_count);
    errdefer alloc.free(channels);

    var ch_idx: usize = 0;
    for (0..gltf_anim.channels_count) |i| {
        const chan = gltf_anim.channels[i];
        const target_node = chan.target_node orelse continue;

        const gltf_ix = @as(u32, @intCast(
            (@intFromPtr(target_node) - @intFromPtr(nodes_slice.ptr)) / @sizeOf(zcgltf.Node),
        ));

        var local_ix: u32 = std.math.maxInt(u32);
        for (node_map, 0..) |me, mi| if (me == gltf_ix) {
            local_ix = @intCast(mi);
            break;
        };
        if (local_ix == std.math.maxInt(u32)) continue;

        const sampler_ix = @as(u32, @intCast(
            (@intFromPtr(chan.sampler) - @intFromPtr(gltf_samplers.ptr)) / @sizeOf(zcgltf.AnimationSampler),
        ));

        const path = pathFromGltf(chan.target_path) orelse continue;
        const sampler = samplers[sampler_ix];
        const value_count = sampler.input.len * componentCount(path) * (if (sampler.interpolation == .cubic_spline) @as(usize, 3) else 1);
        if (sampler.output.len != value_count) return error.InvalidAnimationSamplerOutput;

        channels[ch_idx] = .{
            .skeleton_node_index = local_ix,
            .path = path,
            .sampler_index = sampler_ix,
        };
        ch_idx += 1;
    }

    return .{
        .channels = channels,
        .samplers = samplers,
        .duration = duration,
    };
}

fn normalizeQuaternion(rotation: *[4]f32) void {
    const length_squared = rotation[0] * rotation[0] + rotation[1] * rotation[1] + rotation[2] * rotation[2] + rotation[3] * rotation[3];
    if (!(length_squared > 0) or !std.math.isFinite(length_squared)) {
        rotation.* = .{ 0, 0, 0, 1 };
        return;
    }
    const inverse_length = 1.0 / @sqrt(length_squared);
    for (rotation) |*component| component.* *= inverse_length;
}

fn keyframeAtOrBefore(input: []const f32, time: f32) usize {
    var low: usize = 0;
    var high = input.len;
    while (low < high) {
        const middle = low + (high - low) / 2;
        if (input[middle] <= time) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return if (low == 0) 0 else low - 1;
}

/// Samples this clip into caller-owned local-space pose storage. The output is
/// reset to the skeleton's bind pose before animation channels are applied.
pub fn sample(self: *const Animation, skeleton: base.util.ConstView(Skeleton), time: f32, out_local_pose: []base.util.TRS) !void {
    if (out_local_pose.len != skeleton.bind_local_transforms.len) return error.PoseSizeMismatch;
    if (!std.math.isFinite(time)) return error.InvalidAnimationTime;
    @memcpy(out_local_pose, skeleton.bind_local_transforms);

    for (self.channels) |ch| {
        if (ch.skeleton_node_index >= out_local_pose.len) return error.InvalidAnimationNodeIndex;
        if (ch.sampler_index >= self.samplers.len) return error.InvalidAnimationSamplerIndex;
        const sampler = self.samplers[ch.sampler_index];
        const input = sampler.input;
        const t = @min(@max(time, input[0]), input[input.len - 1]);
        const kf = keyframeAtOrBefore(input, t);
        const stride = componentCount(ch.path);

        if (sampler.interpolation == .step) {
            const b0 = kf * stride;
            switch (ch.path) {
                .translation => for (0..3) |j| {
                    out_local_pose[ch.skeleton_node_index].translation[j] = sampler.output[b0 + j];
                },
                .rotation => for (0..4) |j| {
                    out_local_pose[ch.skeleton_node_index].rotation[j] = sampler.output[b0 + j];
                },
                .scale => for (0..3) |j| {
                    out_local_pose[ch.skeleton_node_index].scale[j] = sampler.output[b0 + j];
                },
            }
            if (ch.path == .rotation) normalizeQuaternion(&out_local_pose[ch.skeleton_node_index].rotation);
        } else if (sampler.interpolation == .linear) {
            if (kf == input.len - 1) {
                const base_index = kf * stride;
                switch (ch.path) {
                    .translation => @memcpy(&out_local_pose[ch.skeleton_node_index].translation, sampler.output[base_index..][0..3]),
                    .rotation => {
                        @memcpy(&out_local_pose[ch.skeleton_node_index].rotation, sampler.output[base_index..][0..4]);
                        normalizeQuaternion(&out_local_pose[ch.skeleton_node_index].rotation);
                    },
                    .scale => @memcpy(&out_local_pose[ch.skeleton_node_index].scale, sampler.output[base_index..][0..3]),
                }
                continue;
            }
            const t_norm = (t - input[kf]) / (input[kf + 1] - input[kf]);
            const b0 = kf * stride;
            const b1 = (kf + 1) * stride;
            switch (ch.path) {
                .translation => for (0..3) |j| {
                    out_local_pose[ch.skeleton_node_index].translation[j] = sampler.output[b0 + j] + (sampler.output[b1 + j] - sampler.output[b0 + j]) * t_norm;
                },
                .rotation => {
                    const q = zm.slerp(
                        zm.f32x4(sampler.output[b0 + 0], sampler.output[b0 + 1], sampler.output[b0 + 2], sampler.output[b0 + 3]),
                        zm.f32x4(sampler.output[b1 + 0], sampler.output[b1 + 1], sampler.output[b1 + 2], sampler.output[b1 + 3]),
                        t_norm,
                    );
                    out_local_pose[ch.skeleton_node_index].rotation = .{ q[0], q[1], q[2], q[3] };
                    normalizeQuaternion(&out_local_pose[ch.skeleton_node_index].rotation);
                },
                .scale => for (0..3) |j| {
                    out_local_pose[ch.skeleton_node_index].scale[j] = sampler.output[b0 + j] + (sampler.output[b1 + j] - sampler.output[b0 + j]) * t_norm;
                },
            }
        } else {
            const value_base = (kf * 3 + 1) * stride;
            if (kf == input.len - 1) {
                switch (ch.path) {
                    .translation => @memcpy(&out_local_pose[ch.skeleton_node_index].translation, sampler.output[value_base..][0..3]),
                    .rotation => {
                        @memcpy(&out_local_pose[ch.skeleton_node_index].rotation, sampler.output[value_base..][0..4]);
                        normalizeQuaternion(&out_local_pose[ch.skeleton_node_index].rotation);
                    },
                    .scale => @memcpy(&out_local_pose[ch.skeleton_node_index].scale, sampler.output[value_base..][0..3]),
                }
                continue;
            }

            const delta = input[kf + 1] - input[kf];
            const u = (t - input[kf]) / delta;
            const u_squared = u * u;
            const u_cubed = u_squared * u;
            const h00 = 2 * u_cubed - 3 * u_squared + 1;
            const h10 = u_cubed - 2 * u_squared + u;
            const h01 = -2 * u_cubed + 3 * u_squared;
            const h11 = u_cubed - u_squared;
            const out_tangent_base = (kf * 3 + 2) * stride;
            const next_in_tangent_base = ((kf + 1) * 3) * stride;
            const next_value_base = ((kf + 1) * 3 + 1) * stride;
            var value: [4]f32 = undefined;
            for (0..stride) |j| {
                value[j] = h00 * sampler.output[value_base + j] +
                    h10 * delta * sampler.output[out_tangent_base + j] +
                    h01 * sampler.output[next_value_base + j] +
                    h11 * delta * sampler.output[next_in_tangent_base + j];
            }
            switch (ch.path) {
                .translation => @memcpy(&out_local_pose[ch.skeleton_node_index].translation, value[0..3]),
                .rotation => {
                    out_local_pose[ch.skeleton_node_index].rotation = value;
                    normalizeQuaternion(&out_local_pose[ch.skeleton_node_index].rotation);
                },
                .scale => @memcpy(&out_local_pose[ch.skeleton_node_index].scale, value[0..3]),
            }
        }
    }
}

pub fn buildModelPose(skeleton: base.util.ConstView(Skeleton), local_pose: []const base.util.TRS, out_model_pose: []zm.Mat) !void {
    if (local_pose.len != skeleton.parents.len or out_model_pose.len != skeleton.parents.len) return error.PoseSizeMismatch;
    for (local_pose, skeleton.parents, 0..) |trs, parent, i| {
        const local = trs.toMat();
        if (parent != std.math.maxInt(u32) and parent >= i) return error.InvalidSkeletonParent;
        out_model_pose[i] = if (parent == std.math.maxInt(u32)) local else zm.mul(local, out_model_pose[parent]);
    }
}

pub fn buildSkinPalette(skin: base.util.ConstView(Skin), model_pose: []const zm.Mat, out_palette: []zm.Mat) !void {
    if (out_palette.len != skin.skeleton_node_indices.len or out_palette.len != skin.inverse_bind_matrices.len) return error.SkinPaletteSizeMismatch;
    for (out_palette, skin.skeleton_node_indices, skin.inverse_bind_matrices) |*out, skeleton_node_index, inverse_bind| {
        if (skeleton_node_index >= model_pose.len) return error.InvalidSkinJointIndex;
        out.* = zm.mul(inverse_bind, model_pose[skeleton_node_index]);
    }
}

test "linear and cubic sampling use caller-owned pose storage" {
    var bind_pose = [_]base.util.TRS{.{
        .translation = .{ 0, 0, 0 },
        .rotation = .{ 0, 0, 0, 1 },
        .scale = .{ 1, 1, 1 },
    }};
    var parents = [_]u32{std.math.maxInt(u32)};
    const skeleton = Skeleton{ .bind_local_transforms = &bind_pose, .parents = &parents };
    var times = [_]f32{ 0, 1 };
    var linear_values = [_]f32{ 0, 0, 0, 2, 4, 6 };
    var linear_samplers = [_]Sampler{.{ .interpolation = .linear, .input = &times, .output = &linear_values }};
    var translation_channels = [_]Channel{.{ .skeleton_node_index = 0, .path = .translation, .sampler_index = 0 }};
    const linear = Animation{ .channels = &translation_channels, .samplers = &linear_samplers, .duration = 1 };
    var pose: [1]base.util.TRS = undefined;

    try linear.sample(base.util.toConstView(skeleton), 0.5, &pose);
    try std.testing.expectEqualSlices(f32, &.{ 1, 2, 3 }, &pose[0].translation);

    // Two cubic keys, each encoded as in-tangent/value/out-tangent.
    var cubic_values = [_]f32{
        0, 0, 0, 0, 0, 0, 1, 0, 0,
        1, 0, 0, 1, 0, 0, 0, 0, 0,
    };
    var cubic_samplers = [_]Sampler{.{ .interpolation = .cubic_spline, .input = &times, .output = &cubic_values }};
    const cubic = Animation{ .channels = &translation_channels, .samplers = &cubic_samplers, .duration = 1 };
    try cubic.sample(base.util.toConstView(skeleton), 0.5, &pose);
    try std.testing.expectApproxEqAbs(@as(f32, 0.5), pose[0].translation[0], 0.0001);
}

test "sample normalizes animated quaternions" {
    var bind_pose = [_]base.util.TRS{.{
        .translation = .{ 0, 0, 0 },
        .rotation = .{ 0, 0, 0, 1 },
        .scale = .{ 1, 1, 1 },
    }};
    var parents = [_]u32{std.math.maxInt(u32)};
    const skeleton = Skeleton{ .bind_local_transforms = &bind_pose, .parents = &parents };
    var times = [_]f32{0};
    var values = [_]f32{ 0, 0, 0, 2 };
    var samplers = [_]Sampler{.{ .interpolation = .step, .input = &times, .output = &values }};
    var channels = [_]Channel{.{ .skeleton_node_index = 0, .path = .rotation, .sampler_index = 0 }};
    const animation = Animation{ .channels = &channels, .samplers = &samplers, .duration = 0 };
    var pose: [1]base.util.TRS = undefined;

    try animation.sample(base.util.toConstView(skeleton), 0, &pose);
    try std.testing.expectEqualSlices(f32, &.{ 0, 0, 0, 1 }, &pose[0].rotation);
}
