const std = @import("std");
const testing = std.testing;

pub fn alignment(comptime T: type) usize {
    return switch (@typeInfo(T)) {
        .int => |info| switch (info.bits) {
            8, 16, 32 => 4,
            64 => 8,
            else => @compileError("unsupported int width " ++ @typeName(T)),
        },
        .float => |info| switch (info.bits) {
            32 => 4,
            64 => 8,
            else => @compileError("unsupported float width " ++ @typeName(T)),
        },
        .vector => |info| {
            const scalar_size = @sizeOf(info.child);
            return scalar_size * switch (info.len) {
                2 => 2,
                3, 4 => 4,
                else => @compileError("unsupported vector length " ++ @typeName(T)),
            };
        },
        .array => |info| @max(alignment(info.child), 16),
        .@"struct" => blk: {
            var max: usize = 0;
            inline for (std.meta.fields(T)) |f| {
                max = @max(max, alignment(f.type));
            }
            break :blk max;
        },
        else => @compileError("unsupported type in std430: " ++ @typeName(T)),
    };
}

pub fn size(comptime T: type) usize {
    return switch (@typeInfo(T)) {
        .int, .float => @sizeOf(T),
        .vector => |info| @sizeOf(info.child) * info.len,
        .array => |info| blk: {
            const elem_size = size(info.child);
            const elem_align = alignment(info.child);
            const padded_elem = std.mem.alignForward(usize, elem_size, elem_align);
            const stride = @max(padded_elem, 16);
            break :blk stride * @as(usize, info.len);
        },
        .@"struct" => blk: {
            var offset: usize = 0;
            var max_align: usize = 0;
            inline for (std.meta.fields(T)) |f| {
                const field_align = alignment(f.type);
                offset = std.mem.alignForward(usize, offset, field_align);
                offset += size(f.type);
                max_align = @max(max_align, field_align);
            }
            if (max_align == 0) break :blk 0;
            break :blk std.mem.alignForward(usize, offset, max_align);
        },
        else => @compileError("unsupported type in std430: " ++ @typeName(T)),
    };
}

pub fn fieldOffsets(comptime T: type) [std.meta.fields(T).len]usize {
    var offsets: [std.meta.fields(T).len]usize = undefined;
    var offset: usize = 0;
    inline for (std.meta.fields(T), 0..) |f, i| {
        const field_align = alignment(f.type);
        offset = std.mem.alignForward(usize, offset, field_align);
        offsets[i] = offset;
        offset += size(f.type);
    }
    return offsets;
}

test "alignment of scalars" {
    try testing.expectEqual(@as(usize, 4), alignment(f32));
    try testing.expectEqual(@as(usize, 4), alignment(u32));
    try testing.expectEqual(@as(usize, 4), alignment(i32));
    try testing.expectEqual(@as(usize, 8), alignment(u64));
    try testing.expectEqual(@as(usize, 8), alignment(f64));
}

test "size of scalars" {
    try testing.expectEqual(@as(usize, 4), size(f32));
    try testing.expectEqual(@as(usize, 4), size(u32));
    try testing.expectEqual(@as(usize, 8), size(u64));
}

test "alignment of vectors" {
    try testing.expectEqual(@as(usize, 8), alignment(@Vector(2, f32)));
    try testing.expectEqual(@as(usize, 16), alignment(@Vector(3, f32)));
    try testing.expectEqual(@as(usize, 16), alignment(@Vector(4, f32)));
}

test "size of vectors" {
    try testing.expectEqual(@as(usize, 8), size(@Vector(2, f32)));
    try testing.expectEqual(@as(usize, 12), size(@Vector(3, f32)));
    try testing.expectEqual(@as(usize, 16), size(@Vector(4, f32)));
}

test "alignment of arrays" {
    try testing.expectEqual(@as(usize, 16), alignment([3]f32));
    try testing.expectEqual(@as(usize, 16), alignment([4]f32));
    try testing.expectEqual(@as(usize, 16), alignment([4]@Vector(4, f32)));
}

test "size of arrays" {
    try testing.expectEqual(@as(usize, 48), size([3]f32));
    try testing.expectEqual(@as(usize, 64), size([4]f32));
    try testing.expectEqual(@as(usize, 64), size([4]@Vector(4, f32)));
}

test "struct with consecutive scalars" {
    const S = struct { a: f32, b: f32 };
    try testing.expectEqual(@as(usize, 4), alignment(S));
    try testing.expectEqual(@as(usize, 8), size(S));
    const off = comptime fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
}

test "struct with vec3 and float" {
    const S = struct { a: @Vector(3, f32), b: f32 };
    try testing.expectEqual(@as(usize, 16), alignment(S));
    try testing.expectEqual(@as(usize, 16), size(S));
    const off = comptime fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 12), off[1]);
}

test "struct with float and vec4" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    try testing.expectEqual(@as(usize, 16), alignment(S));
    try testing.expectEqual(@as(usize, 32), size(S));
    const off = comptime fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 16), off[1]);
}

test "struct with mixed scalars" {
    const S = struct { a: u64, b: f32 };
    try testing.expectEqual(@as(usize, 8), alignment(S));
    try testing.expectEqual(@as(usize, 12), size(S));
    const off = comptime fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 8), off[1]);
}

test "struct with mat4" {
    const S = struct { vp: [4]@Vector(4, f32) };
    try testing.expectEqual(@as(usize, 16), alignment(S));
    try testing.expectEqual(@as(usize, 64), size(S));
    const off = comptime fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
}

test "empty struct" {
    const S = struct {};
    try testing.expectEqual(@as(usize, 0), alignment(S));
    try testing.expectEqual(@as(usize, 0), size(S));
    const off = comptime fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off.len);
}

test "fieldOffsets is comptime" {
    const S = struct { x: f32, y: f32, z: @Vector(3, f32) };
    const off = comptime fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 4), off[1]);
    try testing.expectEqual(@as(usize, 16), off[2]);
}
