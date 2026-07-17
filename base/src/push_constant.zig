const std = @import("std");
const testing = std.testing;
const layout = @import("layout.zig");

const Self = @This();

pub fn size(comptime T: type) usize {
    return comptime layout.size(T);
}

pub fn write(comptime T: type, buffer: []u8, values: T) void {
    const off = comptime layout.fieldOffsets(T);
    inline for (std.meta.fields(T), 0..) |field, i| {
        @memcpy(buffer[off[i]..][0..@sizeOf(field.type)], std.mem.asBytes(&@field(values, field.name)));
    }
}

test "size matches layout.size" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    try testing.expectEqual(layout.size(S), Self.size(S));
}

test "write consecutive scalars" {
    const S = struct { a: u32, b: u32 };
    var buf: [Self.size(S)]u8 = undefined;
    Self.write(S, &buf, .{ .a = 0xAABB, .b = 0xCCDD });

    const a = std.mem.readInt(u32, buf[0..4], .little);
    const b = std.mem.readInt(u32, buf[4..8], .little);
    try testing.expectEqual(@as(u32, 0xAABB), a);
    try testing.expectEqual(@as(u32, 0xCCDD), b);
}

test "write with padding between fields" {
    const S = struct { a: f32, b: @Vector(4, f32) };
    var buf: [Self.size(S)]u8 = undefined;
    Self.write(S, &buf, .{ .a = 1.0, .b = .{ 2.0, 3.0, 4.0, 5.0 } });

    const a_val = std.mem.readInt(u32, buf[0..4], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), a_val);

    const b_x = std.mem.readInt(u32, buf[16..20], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 2.0))), b_x);

    const b_y = std.mem.readInt(u32, buf[20..24], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 3.0))), b_y);
}

test "write mat4 struct" {
    const S = struct { vp: [4]@Vector(4, f32) };
    try testing.expectEqual(@as(usize, 64), Self.size(S));

    var buf: [Self.size(S)]u8 = undefined;
    Self.write(S, &buf, .{ .vp = .{
        @Vector(4, f32){ 1, 0, 0, 0 },
        @Vector(4, f32){ 0, 1, 0, 0 },
        @Vector(4, f32){ 0, 0, 1, 0 },
        @Vector(4, f32){ 0, 0, 0, 1 },
    } });

    const m00 = std.mem.readInt(u32, buf[0..4], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m00);
    const m11 = std.mem.readInt(u32, buf[20..24], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m11);
    const m33 = std.mem.readInt(u32, buf[60..64], .little);
    try testing.expectEqual(@as(u32, @bitCast(@as(f32, 1.0))), m33);
}

test "write preserves untouched bytes" {
    const S = struct { a: f32, b: f32 };
    var buf: [Self.size(S)]u8 = undefined;
    @memset(&buf, 0xFF);
    Self.write(S, &buf, .{ .a = 0, .b = 0 });

    try testing.expectEqual(@as(u8, 0), buf[0]);
    try testing.expectEqual(@as(u8, 0), buf[4]);
    try testing.expectEqual(@as(u8, 0), buf[7]);
}
