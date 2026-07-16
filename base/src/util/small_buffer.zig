const std = @import("std");
const Allocator = std.mem.Allocator;

pub fn SmallBuffer(comptime T: type, comptime N: usize) type {
    return struct {
        const Self = @This();

        stack: [N]T = undefined,
        heap: []T = &.{},

        pub fn get(self: *Self, alloc: Allocator, count: usize) Allocator.Error![]T {
            if (count <= N) return self.stack[0..count];
            self.heap = try alloc.alloc(T, count);
            return self.heap;
        }

        pub fn deinit(self: *Self, alloc: Allocator) void {
            if (self.heap.len > 0) alloc.free(self.heap);
        }
    };
}
