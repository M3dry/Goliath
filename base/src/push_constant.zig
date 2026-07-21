const std = @import("std");
const testing = std.testing;
const layout = @import("layout.zig");

const Self = @This();

pub fn size(comptime T: type, comptime layout_strategy: type) usize {
    return comptime layout_strategy.size(T);
}

pub fn write(comptime T: type, buffer: []u8, values: T, comptime layout_strategy: type) void {
    const off = comptime layout_strategy.fieldOffsets(T);
    inline for (std.meta.fields(T), 0..) |field, i| {
        @memcpy(buffer[off[i]..][0..@sizeOf(field.type)], std.mem.asBytes(&@field(values, field.name)));
    }
}

test "size matches layout.std430.size" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    try testing.expectEqual(layout.std430.size(S), Self.size(S, layout.std430));
}

test "size matches layout.scalar.size" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    try testing.expectEqual(layout.scalar.size(S), Self.size(S, layout.scalar));
}

test "write std430 consecutive scalars" {
    const S = struct { a: u32, b: u32 };
    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    Self.write(S, &buf, .{ .a = 0xAABB, .b = 0xCCDD }, layout.std430);

    const a = std.mem.readInt(u32, buf[0..4], .little);
    const b = std.mem.readInt(u32, buf[4..8], .little);
    try testing.expectEqual(@as(u32, 0xAABB), a);
    try testing.expectEqual(@as(u32, 0xCCDD), b);
}

test "write scalar consecutive scalars" {
    const S = struct { a: u32, b: u32 };
    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    Self.write(S, &buf, .{ .a = 0xAABB, .b = 0xCCDD }, layout.scalar);

    const a = std.mem.readInt(u32, buf[0..4], .little);
    const b = std.mem.readInt(u32, buf[4..8], .little);
    try testing.expectEqual(@as(u32, 0xAABB), a);
    try testing.expectEqual(@as(u32, 0xCCDD), b);
}

test "write std430 with padding between fields" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    Self.write(S, &buf, .{ .a = 1.0, .b = .{ 2.0, 3.0, 4.0, 5.0 } }, layout.std430);

    const a_val = std.mem.readInt(u32, buf[0..4], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), a_val);

    const b_x = std.mem.readInt(u32, buf[16..20], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), b_x);

    const b_y = std.mem.readInt(u32, buf[20..24], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), b_y);
}

test "write scalar with padding between fields" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    Self.write(S, &buf, .{ .a = 1.0, .b = .{ 2.0, 3.0, 4.0, 5.0 } }, layout.scalar);

    const a_val = std.mem.readInt(u32, buf[0..4], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), a_val);

    const b_x = std.mem.readInt(u32, buf[4..8], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), b_x);

    const b_y = std.mem.readInt(u32, buf[8..12], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), b_y);
}

test "write std430 mat4 struct" {
    const S = struct { vp: [4]@Vector(4, f32) };
    try testing.expectEqual(@as(usize, 64), Self.size(S, layout.std430));

    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    Self.write(S, &buf, .{ .vp = .{
        @Vector(4, f32){ 1, 0, 0, 0 },
        @Vector(4, f32){ 0, 1, 0, 0 },
        @Vector(4, f32){ 0, 0, 1, 0 },
        @Vector(4, f32){ 0, 0, 0, 1 },
    } }, layout.std430);

    const m00 = std.mem.readInt(u32, buf[0..4], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m00);
    const m11 = std.mem.readInt(u32, buf[20..24], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m11);
    const m33 = std.mem.readInt(u32, buf[60..64], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m33);
}

test "write scalar mat4 struct" {
    const S = struct { vp: [4]@Vector(4, f32) };
    try testing.expectEqual(@as(usize, 64), Self.size(S, layout.scalar));

    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    Self.write(S, &buf, .{ .vp = .{
        @Vector(4, f32){ 1, 0, 0, 0 },
        @Vector(4, f32){ 0, 1, 0, 0 },
        @Vector(4, f32){ 0, 0, 1, 0 },
        @Vector(4, f32){ 0, 0, 0, 1 },
    } }, layout.scalar);

    const m00 = std.mem.readInt(u32, buf[0..4], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m00);
    const m11 = std.mem.readInt(u32, buf[20..24], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m11);
    const m33 = std.mem.readInt(u32, buf[60..64], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m33);
}

test "write std430 preserves untouched bytes" {
    const S = struct { a: f32, b: f32 };
    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    @memset(&buf, 0xFF);
    Self.write(S, &buf, .{ .a = 0, .b = 0 }, layout.std430);

    try testing.expectEqual(@as(u8, 0), buf[0]);
    try testing.expectEqual(@as(u8, 0), buf[4]);
    try testing.expectEqual(@as(u8, 0), buf[7]);
}

test "write scalar preserves untouched bytes" {
    const S = struct { a: f32, b: f32 };
    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    @memset(&buf, 0xFF);
    Self.write(S, &buf, .{ .a = 0, .b = 0 }, layout.scalar);

    try testing.expectEqual(@as(u8, 0), buf[0]);
    try testing.expectEqual(@as(u8, 0), buf[4]);
    try testing.expectEqual(@as(u8, 0), buf[7]);
}

test "write std430 nested struct" {
    const Inner = struct { x: f32, y: f32 };
    const S = struct { a: Inner, b: f32 };
    try testing.expectEqual(@as(usize, 12), Self.size(S, layout.std430));
    const off = comptime layout.std430.fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 8), off[1]);

    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    Self.write(S, &buf, .{ .a = .{ .x = 1.0, .y = 2.0 }, .b = 3.0 }, layout.std430);

    const ax = std.mem.readInt(u32, buf[0..4], .little);
    const ay = std.mem.readInt(u32, buf[4..8], .little);
    const b = std.mem.readInt(u32, buf[8..12], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), ax);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), ay);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), b);
}

test "write scalar nested struct" {
    const Inner = struct { x: f32, y: f32 };
    const S = struct { a: Inner, b: f32 };
    try testing.expectEqual(@as(usize, 12), Self.size(S, layout.scalar));
    const off = comptime layout.scalar.fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 8), off[1]);

    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    Self.write(S, &buf, .{ .a = .{ .x = 1.0, .y = 2.0 }, .b = 3.0 }, layout.scalar);

    const ax = std.mem.readInt(u32, buf[0..4], .little);
    const ay = std.mem.readInt(u32, buf[4..8], .little);
    const b = std.mem.readInt(u32, buf[8..12], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), ax);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), ay);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), b);
}

test "write std430 vec3 field" {
    const S = struct { a: @Vector(3, f32), b: f32 };
    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    Self.write(S, &buf, .{ .a = @Vector(3, f32){ 1, 2, 3 }, .b = 4.0 }, layout.std430);

    const ax = std.mem.readInt(u32, buf[0..4], .little);
    const ay = std.mem.readInt(u32, buf[4..8], .little);
    const az = std.mem.readInt(u32, buf[8..12], .little);
    const b = std.mem.readInt(u32, buf[12..16], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), ax);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), ay);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), az);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 4.0))), b);
}

test "write scalar vec3 field" {
    const S = struct { a: @Vector(3, f32), b: f32 };
    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    Self.write(S, &buf, .{ .a = @Vector(3, f32){ 1, 2, 3 }, .b = 4.0 }, layout.scalar);

    const ax = std.mem.readInt(u32, buf[0..4], .little);
    const ay = std.mem.readInt(u32, buf[4..8], .little);
    const az = std.mem.readInt(u32, buf[8..12], .little);
    const b = std.mem.readInt(u32, buf[12..16], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), ax);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), ay);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), az);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 4.0))), b);
}

test "write std430 array field" {
    const S = struct { arr: [3]f32 };
    try testing.expectEqual(@as(usize, 48), Self.size(S, layout.std430));
    const off = comptime layout.std430.fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);

    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    @memset(&buf, 0xAA);
    Self.write(S, &buf, .{ .arr = .{ 1.0, 2.0, 3.0 } }, layout.std430);

    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), std.mem.readInt(u32, buf[0..4], .little));
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), std.mem.readInt(u32, buf[4..8], .little));
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), std.mem.readInt(u32, buf[8..12], .little));
}

test "write scalar array field" {
    const S = struct { arr: [3]f32 };
    try testing.expectEqual(@as(usize, 12), Self.size(S, layout.scalar));

    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    @memset(&buf, 0xAA);
    Self.write(S, &buf, .{ .arr = .{ 1.0, 2.0, 3.0 } }, layout.scalar);

    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), std.mem.readInt(u32, buf[0..4], .little));
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), std.mem.readInt(u32, buf[4..8], .little));
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), std.mem.readInt(u32, buf[8..12], .little));
}

test "write std430 bool field" {
    const S = struct { a: bool, b: bool };
    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    @memset(&buf, 0xFF);
    Self.write(S, &buf, .{ .a = true, .b = false }, layout.std430);

    try testing.expectEqual(@as(u8, 1), buf[0]);
    try testing.expectEqual(@as(u8, 0), buf[4]);
}

test "write scalar bool field" {
    const S = struct { a: bool, b: bool };
    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    @memset(&buf, 0xFF);
    Self.write(S, &buf, .{ .a = true, .b = false }, layout.scalar);

    try testing.expectEqual(@as(u8, 1), buf[0]);
    try testing.expectEqual(@as(u8, 0), buf[4]);
}

test "write std430 mat4 plus u64" {
    const S = struct { vp: [4]@Vector(4, f32), addr: u64 };
    try testing.expectEqual(@as(usize, 80), Self.size(S, layout.std430));
    const off = comptime layout.std430.fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 64), off[1]);

    var buf: [Self.size(S, layout.std430)]u8 = undefined;
    Self.write(S, &buf, .{ .vp = .{
        @Vector(4, f32){ 1, 0, 0, 0 },
        @Vector(4, f32){ 0, 1, 0, 0 },
        @Vector(4, f32){ 0, 0, 1, 0 },
        @Vector(4, f32){ 0, 0, 0, 1 },
    }, .addr = 0xDEADBEEFCAFE }, layout.std430);

    const m00 = std.mem.readInt(u32, buf[0..4], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m00);
    const addr = std.mem.readInt(u64, buf[64..72], .little);
    try testing.expectEqual(@as(u64, 0xDEADBEEFCAFE), addr);
}

test "write scalar mat4 plus u64" {
    const S = struct { vp: [4]@Vector(4, f32), addr: u64 };
    try testing.expectEqual(@as(usize, 72), Self.size(S, layout.scalar));
    const off = comptime layout.scalar.fieldOffsets(S);
    try testing.expectEqual(@as(usize, 0), off[0]);
    try testing.expectEqual(@as(usize, 64), off[1]);

    var buf: [Self.size(S, layout.scalar)]u8 = undefined;
    Self.write(S, &buf, .{ .vp = .{
        @Vector(4, f32){ 1, 0, 0, 0 },
        @Vector(4, f32){ 0, 1, 0, 0 },
        @Vector(4, f32){ 0, 0, 1, 0 },
        @Vector(4, f32){ 0, 0, 0, 1 },
    }, .addr = 0xDEADBEEFCAFE }, layout.scalar);

    const m00 = std.mem.readInt(u32, buf[0..4], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m00);
    const addr = std.mem.readInt(u64, buf[64..72], .little);
    try testing.expectEqual(@as(u64, 0xDEADBEEFCAFE), addr);
}
