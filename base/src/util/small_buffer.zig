const std = @import("std");
const Allocator = std.mem.Allocator;

pub fn SmallBuffer(comptime T: type, comptime stack_count: usize) type {
    return struct {
        const Self = @This();
        const tag_mask: u32 = 1 << 31;

        len_field: u32 = 0, // MSB set => heap-backed
        storage: union {
            stack: [stack_count]T,
            heap: []T,
        } = .{ .stack = undefined },

        pub fn onHeap(self: *const Self) bool {
            return (self.len_field & tag_mask) != 0;
        }

        pub fn len(self: *const Self) usize {
            return self.len_field & (tag_mask - 1);
        }

        pub fn capacity(self: *const Self) usize {
            return if (self.onHeap()) self.storage.heap.len else stack_count;
        }

        pub fn items(self: *Self) []T {
            const n = self.len();
            if (self.onHeap()) return self.storage.heap[0..n];
            return self.storage.stack[0..n];
        }

        pub fn resize(self: *Self, alloc: Allocator, new_len: usize) Allocator.Error!void {
            std.debug.assert(new_len <= max_mask);
            if (self.onHeap()) {
                if (new_len <= stack_count) return self.moveToStack(alloc, new_len);
                // ponytail: exact-size realloc, no doubling; add a grow factor if
                // profiles show allocator churn.
                self.storage = .{ .heap = try alloc.realloc(self.storage.heap, new_len) };
            } else if (new_len > stack_count) {
                try self.moveToHeap(alloc, new_len);
            }
            self.setLen(new_len);
        }

        pub fn resizeStayOnHeap(self: *Self, alloc: Allocator, new_len: usize) Allocator.Error!void {
            std.debug.assert(new_len <= max_mask);
            if (self.onHeap()) {
                self.storage = .{ .heap = try alloc.realloc(self.storage.heap, new_len) };
            } else if (new_len > stack_count) {
                try self.moveToHeap(alloc, new_len);
            }
            self.setLen(new_len);
        }

        pub fn ensureCapacity(self: *Self, alloc: Allocator, cap: usize) Allocator.Error!void {
            if (cap <= self.capacity()) return;
            if (self.onHeap()) {
                self.storage = .{ .heap = try alloc.realloc(self.storage.heap, cap) };
            } else {
                try self.moveToHeap(alloc, cap);
            }
        }

        pub fn ensureUnusedCapacity(self: *Self, alloc: Allocator, n: usize) Allocator.Error!void {
            try self.ensureCapacity(alloc, self.len() + n);
        }

        pub fn addOne(self: *Self, alloc: Allocator) Allocator.Error!*T {
            try self.ensureUnusedCapacity(alloc, 1);
            return self.addOneAssumeCapacity();
        }

        pub fn addOneAssumeCapacity(self: *Self) *T {
            std.debug.assert(self.len() < self.capacity());

            self.setLen(self.len() + 1);
            return &self.items()[self.len() - 1];
        }

        pub fn resizeAssumeCapacity(self: *Self, new_len: usize) void {
            std.debug.assert(new_len <= self.capacity());
            self.setLen(new_len);
        }

        pub fn deinit(self: *Self, alloc: Allocator) void {
            if (self.onHeap()) alloc.free(self.storage.heap);
            self.len_field = 0;
            self.storage = .{ .stack = undefined };
        }

        const max_mask = (1 << 31) - 1;

        fn setLen(self: *Self, new_len: usize) void {
            self.len_field = (self.len_field & tag_mask) | @as(u32, @intCast(new_len));
        }

        fn moveToHeap(self: *Self, alloc: Allocator, cap: usize) Allocator.Error!void {
            const buf = try alloc.alloc(T, cap);
            const n = self.len();

            @memcpy(buf[0..n], self.storage.stack[0..n]);

            self.storage = .{ .heap = buf };
            self.len_field |= tag_mask;
        }

        fn moveToStack(self: *Self, alloc: Allocator, new_len: usize) void {
            const src = self.storage.heap;
            self.storage = .{ .stack = undefined };
            @memcpy(self.storage.stack[0..new_len], src[0..new_len]);
            alloc.free(src);
            self.len_field = @as(u32, @intCast(new_len));
        }
    };
}

test "small buffer" {
    var sb: SmallBuffer(u32, 2) = .{};
    const alloc = std.testing.allocator;
    defer sb.deinit(alloc);

    (try sb.addOne(alloc)).* = 10;
    (try sb.addOne(alloc)).* = 20;
    try std.testing.expect(!sb.onHeap());
    (try sb.addOne(alloc)).* = 30;
    try std.testing.expect(sb.onHeap());
    try std.testing.expectEqual(@as(usize, 3), sb.len());
    try std.testing.expectEqualSlices(u32, &.{ 10, 20, 30 }, sb.items());

    try sb.resize(alloc, 1);
    try std.testing.expect(!sb.onHeap());
    try std.testing.expectEqual(@as(usize, 1), sb.len());
    try std.testing.expectEqual(@as(u32, 10), sb.items()[0]);

    var stay: SmallBuffer(u32, 2) = .{};
    defer stay.deinit(alloc);
    try stay.resize(alloc, 3);
    try stay.resizeStayOnHeap(alloc, 1);
    try std.testing.expect(stay.onHeap());

    var cap: SmallBuffer(u32, 2) = .{};
    defer cap.deinit(alloc);
    cap.resizeAssumeCapacity(1);
    cap.items()[0] = 7;
    try std.testing.expectEqual(@as(usize, 1), cap.len());
    try std.testing.expectEqual(@as(u32, 7), cap.items()[0]);
}
