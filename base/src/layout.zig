const std = @import("std");
const testing = std.testing;

fn alignmentStd430(comptime T: type) usize {
    return switch (@typeInfo(T)) {
        .int => |info| switch (info.bits) {
            32 => 4,
            64 => 8,
            8, 16 => @compileError("GLSL has no " ++ @typeName(T) ++ "; use u32/i32"),
            else => @compileError("unsupported int width " ++ @typeName(T)),
        },
        .float => |info| switch (info.bits) {
            32 => 4,
            64 => 8,
            else => @compileError("unsupported float width " ++ @typeName(T)),
        },
        .bool => 4,
        .vector => |info| {
            const scalar_size = @sizeOf(info.child);
            return scalar_size * switch (info.len) {
                2 => 2,
                3, 4 => 4,
                else => @compileError("unsupported vector length " ++ @typeName(T)),
            };
        },
        .array => |info| @max(alignmentStd430(info.child), 16),
        .@"struct" => blk: {
            var max: usize = 0;
            inline for (std.meta.fields(T)) |f| {
                max = @max(max, alignmentStd430(f.type));
            }
            break :blk max;
        },
        else => @compileError("unsupported type in std430: " ++ @typeName(T)),
    };
}

fn sizeStd430(comptime T: type) usize {
    return switch (@typeInfo(T)) {
        .int => |info| switch (info.bits) {
            32, 64 => @sizeOf(T),
            8, 16 => @compileError("GLSL has no " ++ @typeName(T) ++ "; use u32/i32"),
            else => @compileError("unsupported int width " ++ @typeName(T)),
        },
        .float, .bool => @sizeOf(T),
        .vector => |info| @sizeOf(info.child) * info.len,
        .array => |info| blk: {
            const elem_size = sizeStd430(info.child);
            const elem_align = alignmentStd430(info.child);
            const padded_elem = std.mem.alignForward(usize, elem_size, elem_align);
            const stride = @max(padded_elem, 16);
            break :blk stride * @as(usize, info.len);
        },
        .@"struct" => blk: {
            var offset: usize = 0;
            var max_align: usize = 0;
            inline for (std.meta.fields(T)) |f| {
                const field_align = alignmentStd430(f.type);
                offset = std.mem.alignForward(usize, offset, field_align);
                offset += sizeStd430(f.type);
                max_align = @max(max_align, field_align);
            }
            if (max_align == 0) break :blk 0;
            break :blk std.mem.alignForward(usize, offset, max_align);
        },
        else => @compileError("unsupported type in std430: " ++ @typeName(T)),
    };
}

fn fieldOffsetsStd430(comptime T: type) [std.meta.fields(T).len]usize {
    var offsets: [std.meta.fields(T).len]usize = undefined;
    var offset: usize = 0;
    inline for (std.meta.fields(T), 0..) |f, i| {
        const field_align = alignmentStd430(f.type);
        offset = std.mem.alignForward(usize, offset, field_align);
        offsets[i] = offset;
        offset += sizeStd430(f.type);
    }
    return offsets;
}

fn alignmentScalar(comptime T: type) usize {
    return switch (@typeInfo(T)) {
        .int => |info| switch (info.bits) {
            32 => 4,
            64 => 8,
            8, 16 => @compileError("GLSL has no " ++ @typeName(T) ++ "; use u32/i32"),
            else => @compileError("unsupported int width " ++ @typeName(T)),
        },
        .float => |info| switch (info.bits) {
            32 => 4,
            64 => 8,
            else => @compileError("unsupported float width " ++ @typeName(T)),
        },
        .bool => 4,
        .vector => |info| @sizeOf(info.child),
        .array => |info| alignmentScalar(info.child),
        .@"struct" => blk: {
            var max: usize = 0;
            inline for (std.meta.fields(T)) |f| {
                max = @max(max, alignmentScalar(f.type));
            }
            break :blk max;
        },
        else => @compileError("unsupported type in scalar: " ++ @typeName(T)),
    };
}

fn sizeScalar(comptime T: type) usize {
    return switch (@typeInfo(T)) {
        .int => |info| switch (info.bits) {
            32, 64 => @sizeOf(T),
            8, 16 => @compileError("GLSL has no " ++ @typeName(T) ++ "; use u32/i32"),
            else => @compileError("unsupported int width " ++ @typeName(T)),
        },
        .float, .bool => @sizeOf(T),
        .vector => |info| @sizeOf(info.child) * info.len,
        .array => |info| blk: {
            const elem_size = sizeScalar(info.child);
            const elem_align = alignmentScalar(info.child);
            const stride = std.mem.alignForward(usize, elem_size, elem_align);
            break :blk stride * @as(usize, info.len);
        },
        .@"struct" => blk: {
            var offset: usize = 0;
            var max_align: usize = 0;
            inline for (std.meta.fields(T)) |f| {
                const field_align = alignmentScalar(f.type);
                offset = std.mem.alignForward(usize, offset, field_align);
                offset += sizeScalar(f.type);
                max_align = @max(max_align, field_align);
            }
            if (max_align == 0) break :blk 0;
            break :blk std.mem.alignForward(usize, offset, max_align);
        },
        else => @compileError("unsupported type in scalar: " ++ @typeName(T)),
    };
}

fn fieldOffsetsScalar(comptime T: type) [std.meta.fields(T).len]usize {
    var offsets: [std.meta.fields(T).len]usize = undefined;
    var offset: usize = 0;
    inline for (std.meta.fields(T), 0..) |f, i| {
        const field_align = alignmentScalar(f.type);
        offset = std.mem.alignForward(usize, offset, field_align);
        offsets[i] = offset;
        offset += sizeScalar(f.type);
    }
    return offsets;
}

pub const std430 = struct {
    pub const alignment = alignmentStd430;
    pub const size = sizeStd430;
    pub const fieldOffsets = fieldOffsetsStd430;
};

pub const scalar = struct {
    pub const alignment = alignmentScalar;
    pub const size = sizeScalar;
    pub const fieldOffsets = fieldOffsetsScalar;
};

test "std430 alignment of scalars" {
    try testing.expectEqual(@as(usize, 4), alignmentStd430(f32));
    try testing.expectEqual(@as(usize, 4), alignmentStd430(u32));
    try testing.expectEqual(@as(usize, 4), alignmentStd430(i32));
    try testing.expectEqual(@as(usize, 8), alignmentStd430(u64));
}

test "std430 size of scalars" {
    try testing.expectEqual(@as(usize, 4), sizeStd430(f32));
    try testing.expectEqual(@as(usize, 4), sizeStd430(u32));
    try testing.expectEqual(@as(usize, 8), sizeStd430(u64));
}

test "std430 alignment of vectors" {
    try testing.expectEqual(@as(usize, 8), alignmentStd430(@Vector(2, f32)));
    try testing.expectEqual(@as(usize, 16), alignmentStd430(@Vector(3, f32)));
    try testing.expectEqual(@as(usize, 16), alignmentStd430(@Vector(4, f32)));
}

test "std430 size of vectors" {
    try testing.expectEqual(@as(usize, 8), sizeStd430(@Vector(2, f32)));
    try testing.expectEqual(@as(usize, 12), sizeStd430(@Vector(3, f32)));
    try testing.expectEqual(@as(usize, 16), sizeStd430(@Vector(4, f32)));
}

test "std430 alignment of arrays" {
    try testing.expectEqual(@as(usize, 16), alignmentStd430([3]f32));
    try testing.expectEqual(@as(usize, 16), alignmentStd430([4]f32));
    try testing.expectEqual(@as(usize, 16), alignmentStd430([4]@Vector(4, f32)));
}

test "std430 size of arrays" {
    try testing.expectEqual(@as(usize, 48), sizeStd430([3]f32));
    try testing.expectEqual(@as(usize, 64), sizeStd430([4]f32));
    try testing.expectEqual(@as(usize, 64), sizeStd430([4]@Vector(4, f32)));
}

test "std430 struct with consecutive scalars" {
    const S = struct { a: f32, b: f32 };
    try testing.expectEqual(@as(usize, 4), alignmentStd430(S));
    try testing.expectEqual(@as(usize, 8), sizeStd430(S));
    const off = comptime fieldOffsetsStd430(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
}

test "std430 struct with vec3 and float" {
    const S = struct { a: @Vector(3, f32), b: f32 };
    try testing.expectEqual(@as(usize, 16), alignmentStd430(S));
    try testing.expectEqual(@as(usize, 16), sizeStd430(S));
    const off = comptime fieldOffsetsStd430(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 12), off[1]);
}

test "std430 struct with float and vec4" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    try testing.expectEqual(@as(usize, 16), alignmentStd430(S));
    try testing.expectEqual(@as(usize, 32), sizeStd430(S));
    const off = comptime fieldOffsetsStd430(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 16), off[1]);
}

test "std430 struct with bool" {
    const S = struct { a: bool, b: f32 };
    try testing.expectEqual(@as(usize, 4), alignmentStd430(S));
    try testing.expectEqual(@as(usize, 8), sizeStd430(S));
    const off = comptime fieldOffsetsStd430(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
}

test "std430 struct with mat4" {
    const S = struct { vp: [4]@Vector(4, f32) };
    try testing.expectEqual(@as(usize, 16), alignmentStd430(S));
    try testing.expectEqual(@as(usize, 64), sizeStd430(S));
    const off = comptime fieldOffsetsStd430(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
}

test "std430 empty struct" {
    const S = struct {};
    try testing.expectEqual(@as(usize, 0), alignmentStd430(S));
    try testing.expectEqual(@as(usize, 0), sizeStd430(S));
    const off = comptime fieldOffsetsStd430(S);
    try testing.expectEqual(@as(usize, 0), off.len);
}

test "std430 fieldOffsets is comptime" {
    const S = struct { x: f32, y: f32, z: @Vector(3, f32) };
    const off = comptime fieldOffsetsStd430(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
    try testing.expectEqual(@as(usize, 16), off[2]);
}

test "scalar alignment of scalars" {
    try testing.expectEqual(@as(usize, 4), alignmentScalar(f32));
    try testing.expectEqual(@as(usize, 4), alignmentScalar(u32));
    try testing.expectEqual(@as(usize, 4), alignmentScalar(i32));
    try testing.expectEqual(@as(usize, 8), alignmentScalar(u64));
}

test "scalar size of scalars" {
    try testing.expectEqual(@as(usize, 4), sizeScalar(f32));
    try testing.expectEqual(@as(usize, 4), sizeScalar(u32));
    try testing.expectEqual(@as(usize, 8), sizeScalar(u64));
}

test "scalar alignment of vectors" {
    try testing.expectEqual(@as(usize, 4), alignmentScalar(@Vector(2, f32)));
    try testing.expectEqual(@as(usize, 4), alignmentScalar(@Vector(3, f32)));
    try testing.expectEqual(@as(usize, 4), alignmentScalar(@Vector(4, f32)));
}

test "scalar size of vectors" {
    try testing.expectEqual(@as(usize, 8), sizeScalar(@Vector(2, f32)));
    try testing.expectEqual(@as(usize, 12), sizeScalar(@Vector(3, f32)));
    try testing.expectEqual(@as(usize, 16), sizeScalar(@Vector(4, f32)));
}

test "scalar alignment of arrays" {
    try testing.expectEqual(@as(usize, 4), alignmentScalar([3]f32));
    try testing.expectEqual(@as(usize, 4), alignmentScalar([4]f32));
    try testing.expectEqual(@as(usize, 4), alignmentScalar([4]@Vector(4, f32)));
}

test "scalar size of arrays" {
    try testing.expectEqual(@as(usize, 12), sizeScalar([3]f32));
    try testing.expectEqual(@as(usize, 16), sizeScalar([4]f32));
    try testing.expectEqual(@as(usize, 64), sizeScalar([4]@Vector(4, f32)));
}

test "scalar struct with consecutive scalars" {
    const S = struct { a: f32, b: f32 };
    try testing.expectEqual(@as(usize, 4), alignmentScalar(S));
    try testing.expectEqual(@as(usize, 8), sizeScalar(S));
    const off = comptime fieldOffsetsScalar(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
}

test "scalar struct with vec3 and float" {
    const S = struct { a: @Vector(3, f32), b: f32 };
    try testing.expectEqual(@as(usize, 4), alignmentScalar(S));
    try testing.expectEqual(@as(usize, 16), sizeScalar(S));
    const off = comptime fieldOffsetsScalar(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 12), off[1]);
}

test "scalar struct with float and vec4" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    try testing.expectEqual(@as(usize, 4), alignmentScalar(S));
    try testing.expectEqual(@as(usize, 20), sizeScalar(S));
    const off = comptime fieldOffsetsScalar(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
}

test "scalar struct with bool" {
    const S = struct { a: bool, b: f32 };
    try testing.expectEqual(@as(usize, 4), alignmentScalar(S));
    try testing.expectEqual(@as(usize, 8), sizeScalar(S));
    const off = comptime fieldOffsetsScalar(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
}

test "scalar struct with mat4" {
    const S = struct { vp: [4]@Vector(4, f32) };
    try testing.expectEqual(@as(usize, 4), alignmentScalar(S));
    try testing.expectEqual(@as(usize, 64), sizeScalar(S));
    const off = comptime fieldOffsetsScalar(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
}

test "scalar empty struct" {
    const S = struct {};
    try testing.expectEqual(@as(usize, 0), alignmentScalar(S));
    try testing.expectEqual(@as(usize, 0), sizeScalar(S));
    const off = comptime fieldOffsetsScalar(S);
    try testing.expectEqual(@as(usize, 0), off.len);
}

test "scalar fieldOffsets is comptime" {
    const S = struct { x: f32, y: f32, z: @Vector(3, f32) };
    const off = comptime fieldOffsetsScalar(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
    try testing.expectEqual(@as(usize, 8), off[2]);
}
