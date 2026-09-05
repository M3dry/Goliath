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
        if (input.len > 0) duration = @max(duration, input[input.len - 1]);

        const output = try alloc.alloc(f32, gs.output.unpackFloatsCount());
        errdefer alloc.free(output);
        _ = gs.output.unpackFloats(output);

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
        if (chan.target_node) |node| {
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

        const path: Path = switch (chan.target_path) {
            .translation => .translation,
            .rotation => .rotation,
            .scale => .scale,
            else => continue,
        };

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

pub fn evaluate(self: *const Animation, alloc: Allocator, skeleton: *const Skeleton, skin: *const Skin, time: f32, out: []zm.Mat) !void {
    var eval_trs = try alloc.alloc(Skeleton.TRSNode, skeleton.bind_local_transforms.len);
    defer alloc.free(eval_trs);
    @memcpy(eval_trs, skeleton.bind_local_transforms);

    for (self.channels) |ch| {
        const sampler = self.samplers[ch.sampler_index];
        const input = sampler.input;
        if (input.len == 0) continue;
        const t = @min(@max(time, input[0]), input[input.len - 1]);

        var kf: usize = 0;
        for (input, 0..) |key_time, i| {
            if (t >= key_time) kf = i;
        }

        const stride: u32 = switch (ch.path) { .translation => 3, .rotation => 4, .scale => 3 };

        if (sampler.interpolation == .step or kf == input.len - 1) {
            const b0 = kf * stride;
            switch (ch.path) {
                .translation => for (0..3) |j| {
                    eval_trs[ch.skeleton_node_index].translation[j] = sampler.output[b0 + j];
                },
                .rotation => for (0..4) |j| {
                    eval_trs[ch.skeleton_node_index].rotation[j] = sampler.output[b0 + j];
                },
                .scale => for (0..3) |j| {
                    eval_trs[ch.skeleton_node_index].scale[j] = sampler.output[b0 + j];
                },
            }
        } else if (sampler.interpolation == .linear) {
            const t_norm = (t - input[kf]) / (input[kf + 1] - input[kf]);
            const b0 = kf * stride;
            const b1 = (kf + 1) * stride;
            switch (ch.path) {
                .translation => for (0..3) |j| {
                    eval_trs[ch.skeleton_node_index].translation[j] = sampler.output[b0 + j] + (sampler.output[b1 + j] - sampler.output[b0 + j]) * t_norm;
                },
                .rotation => {
                    const q = zm.slerp(
                        zm.f32x4(sampler.output[b0 + 0], sampler.output[b0 + 1], sampler.output[b0 + 2], sampler.output[b0 + 3]),
                        zm.f32x4(sampler.output[b1 + 0], sampler.output[b1 + 1], sampler.output[b1 + 2], sampler.output[b1 + 3]),
                        t_norm,
                    );
                    eval_trs[ch.skeleton_node_index].rotation = .{ q[0], q[1], q[2], q[3] };
                },
                .scale => for (0..3) |j| {
                    eval_trs[ch.skeleton_node_index].scale[j] = sampler.output[b0 + j] + (sampler.output[b1 + j] - sampler.output[b0 + j]) * t_norm;
                },
            }
        }
    }

    var joint_world = try alloc.alloc(zm.Mat, skeleton.bind_local_transforms.len);
    defer alloc.free(joint_world);

    for (eval_trs, skeleton.parents, 0..) |trs, parent, i| {
        const local = zm.mul(zm.scaling(trs.scale[0], trs.scale[1], trs.scale[2]), zm.mul(zm.matFromQuat(zm.f32x4(trs.rotation[0], trs.rotation[1], trs.rotation[2], trs.rotation[3])), zm.translation(trs.translation[0], trs.translation[1], trs.translation[2])));
        joint_world[i] = if (parent == std.math.maxInt(u32)) local else zm.mul(local, joint_world[parent]);
    }

    for (0..out.len) |i| out[i] = zm.mul(skin.inverse_bind_matrices[i], joint_world[skin.skeleton_node_indices[i]]);
}
