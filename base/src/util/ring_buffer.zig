const std = @import("std");
const Allocator = std.mem.Allocator;

pub fn RingBuffer(comptime T: type) type {
    return struct {
        const Self = @This();

        items: []T = &.{},
        head: usize = 0,
        len: usize = 0,

        pub const empty: Self = .{};

        pub fn initCapacity(alloc: Allocator, cap: usize) !Self {
            const c = @max(cap, 1);
            const items = try alloc.alloc(T, c);
            return .{
                .items = items,
                .head = 0,
                .len = 0,
            };
        }

        pub fn deinit(self: *Self, alloc: Allocator) void {
            alloc.free(self.items);
            self.* = undefined;
        }

        pub fn capacity(self: *const Self) usize {
            return self.items.len;
        }

        pub fn is_empty(self: *const Self) bool {
            return self.len == 0;
        }

        pub fn ensureTotalCapacity(self: *Self, alloc: Allocator, new_cap: usize) !void {
            if (new_cap <= self.items.len) return;
            const new_items = try alloc.alloc(T, new_cap);
            const first_count = @min(self.len, self.items.len - self.head);

            @memcpy(new_items[0..first_count], self.items[self.head..][0..first_count]);
            if (first_count < self.len) {
                @memcpy(new_items[first_count..self.len], self.items[0..self.len - first_count]);
            }

            alloc.free(self.items);
            self.items = new_items;
            self.head = 0;
        }

        pub fn ensureUnusedCapacity(self: *Self, alloc: Allocator, additional: usize) !void {
            if (self.len + additional > self.items.len) {
                try self.ensureTotalCapacity(alloc, @max(self.len + additional, self.items.len * 2));
            }
        }

        pub fn append(self: *Self, alloc: Allocator, item: T) !void {
            if (self.len == self.items.len) try self.grow(alloc);
            self.items[wrap(self.head + self.len, self.items.len)] = item;
            self.len += 1;
        }

        pub fn appendAssumeCapacity(self: *Self, item: T) void {
            self.items[wrap(self.head + self.len, self.items.len)] = item;
            self.len += 1;
        }

        pub fn prepend(self: *Self, alloc: Allocator, item: T) !void {
            if (self.len == self.items.len) try self.grow(alloc);
            self.head = wrap(self.head + self.items.len - 1, self.items.len);
            self.items[self.head] = item;
            self.len += 1;
        }

        pub fn prependAssumeCapacity(self: *Self, item: T) void {
            self.head = wrap(self.head + self.items.len - 1, self.items.len);
            self.items[self.head] = item;
            self.len += 1;
        }

        pub fn addOne(self: *Self, alloc: Allocator) !*T {
            if (self.len == self.items.len) try self.grow(alloc);
            const ptr = &self.items[wrap(self.head + self.len, self.items.len)];
            self.len += 1;
            return ptr;
        }

        pub fn addOneAssumeCapacity(self: *Self) *T {
            const ptr = &self.items[wrap(self.head + self.len, self.items.len)];
            self.len += 1;
            return ptr;
        }

        pub fn prependOne(self: *Self, alloc: Allocator) !*T {
            if (self.len == self.items.len) try self.grow(alloc);
            self.head = wrap(self.head + self.items.len - 1, self.items.len);
            self.len += 1;
            return &self.items[self.head];
        }

        pub fn prependOneAssumeCapacity(self: *Self) *T {
            self.head = wrap(self.head + self.items.len - 1, self.items.len);
            self.len += 1;
            return &self.items[self.head];
        }

        pub fn popFirst(self: *Self) ?T {
            if (self.len == 0) return null;
            const item = self.items[self.head];
            self.head = wrap(self.head + 1, self.items.len);
            self.len -= 1;
            return item;
        }

        pub fn popLast(self: *Self) ?T {
            if (self.len == 0) return null;
            self.len -= 1;
            return self.items[wrap(self.head + self.len, self.items.len)];
        }

        pub fn first(self: *const Self) ?*T {
            if (self.len == 0) return null;
            return &self.items[self.head];
        }

        pub fn last(self: *const Self) ?*T {
            if (self.len == 0) return null;
            return &self.items[wrap(self.head + self.len - 1, self.items.len)];
        }

        pub fn get(self: *const Self, index: usize) ?*T {
            if (index >= self.len) return null;
            return &self.items[wrap(self.head + index, self.items.len)];
        }

        pub fn set(self: *Self, index: usize, item: T) void {
            self.items[wrap(self.head + index, self.items.len)] = item;
        }

        pub fn clear(self: *Self) void {
            self.head = 0;
            self.len = 0;
        }

        pub fn orderedRemove(self: *Self, index: usize) void {
            const actual_index = wrap(self.head + index, self.items.len);
            const last_actual_index = wrap(self.head + self.len - 1, self.items.len);

            if (index == 0) {
                self.head += 1;
                self.len -= 1;

                if (self.head == self.items.len) self.head = 0;
                return;
            } else if (last_actual_index == actual_index) {
                self.len -= 1;
                return;
            } else if (self.head + index < self.len) {
                @memmove(self.items[(self.head + 1)..(actual_index + 1)], self.items[self.head..actual_index]);

                self.head += 1;
                self.len -= 1;
                if (self.head == self.items.len) self.head = 0;
                return;
            } else {
                @memmove(self.items[actual_index..last_actual_index], self.items[(actual_index + 1)..(last_actual_index + 1)]);

                self.len -= 1;
                return;
            }
        }

        pub fn iterate(self: *const Self) Iterator {
            return .{ .rb = self, .index = 0 };
        }

        pub const Iterator = struct {
            rb: *const Self,
            index: usize,

            pub fn next(it: *Iterator) ?*T {
                if (it.index >= it.rb.len) return null;
                defer it.index += 1;
                return &it.rb.items[wrap(it.rb.head + it.index, it.rb.items.len)];
            }
        };

        fn grow(self: *Self, alloc: Allocator) !void {
            const new_cap = @max(self.items.len * 2, 2);
            try self.ensureTotalCapacity(alloc, new_cap);
        }

        fn wrap(index: usize, cap: usize) usize {
            if (index >= cap) return index - cap;
            return index;
        }
    };
}

const testing = @import("std").testing;

fn ringBufferTest(comptime cap: usize) type {
    return struct {
        const RB = RingBuffer(u32);
        alloc: std.mem.Allocator,
        rb: RB = .{},

        fn init(a: std.mem.Allocator) !@This() {
            var self: @This() = .{ .alloc = a };
            self.rb = try RB.initCapacity(a, cap);
            return self;
        }

        fn deinit(self: *@This()) void {
            self.rb.deinit(self.alloc);
        }

        fn check(self: *@This(), expected_head: usize, expected_len: usize, expected_items: []const u32) !void {
            try testing.expectEqual(expected_head, self.rb.head);
            try testing.expectEqual(expected_len, self.rb.len);
            if (expected_items.len > 0) {
                var i: usize = 0;
                var it = self.rb.iterate();
                while (it.next()) |item| : (i += 1) {
                    try testing.expectEqual(expected_items[i], item.*);
                }
                try testing.expectEqual(expected_items.len, i);
            }
        }
    };
}

test "ring buffer: empty state" {
    const RB = RingBuffer(u32);
    var rb: RB = .{};
    try testing.expect(rb.is_empty());
    try testing.expectEqual(@as(usize, 0), rb.len);
    try testing.expectEqual(@as(usize, 0), rb.capacity());
    try testing.expect(rb.first() == null);
    try testing.expect(rb.last() == null);
    try testing.expect(rb.get(0) == null);
    try testing.expect(rb.popFirst() == null);
    try testing.expect(rb.popLast() == null);
}

test "ring buffer: initCapacity and deinit" {
    const alloc = testing.allocator;
    const RB = RingBuffer(u32);
    var rb = try RB.initCapacity(alloc, 4);
    defer rb.deinit(alloc);
    try testing.expectEqual(@as(usize, 4), rb.capacity());
    try testing.expect(rb.is_empty());
}

test "ring buffer: append and popFirst" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 10);
    try t.rb.append(alloc, 20);
    try t.check(0, 2, &.{ 10, 20 });
    try testing.expectEqual(@as(u32, 10), t.rb.popFirst().?);
    try testing.expectEqual(@as(u32, 20), t.rb.popFirst().?);
    try testing.expect(t.rb.is_empty());
}

test "ring buffer: prepend and popLast" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.prepend(alloc, 30);
    try t.rb.prepend(alloc, 20);
    try t.rb.prepend(alloc, 10);
    // head wraps backward: 0→3→2→1
    try t.check(1, 3, &.{ 10, 20, 30 });
    try testing.expectEqual(@as(u32, 30), t.rb.popLast().?);
    try testing.expectEqual(@as(u32, 20), t.rb.popLast().?);
    try testing.expectEqual(@as(u32, 10), t.rb.popLast().?);
    try testing.expect(t.rb.is_empty());
}

test "ring buffer: addOne and prependOne" {
    const alloc = testing.allocator;
    var rb = try RingBuffer(u32).initCapacity(alloc, 4);
    defer rb.deinit(alloc);

    _ = rb.addOne(alloc) catch |e| return e;
    rb.items[rb.head] = 10;
    _ = rb.prependOne(alloc) catch |e| return e;
    rb.items[rb.head] = 20;
    _ = rb.addOne(alloc) catch |e| return e;
    const last = (rb.head + rb.len - 1) % rb.items.len;
    rb.items[last] = 30;

    try testing.expectEqual(@as(usize, 3), rb.len);
    try testing.expectEqual(@as(u32, 20), rb.popFirst().?);
    try testing.expectEqual(@as(u32, 10), rb.popFirst().?);
    try testing.expectEqual(@as(u32, 30), rb.popFirst().?);
}

test "ring buffer: get and set" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 100);
    try t.rb.append(alloc, 200);
    try t.rb.append(alloc, 300);

    try testing.expectEqual(@as(u32, 200), t.rb.get(1).?.*);
    try testing.expect(t.rb.get(3) == null);

    t.rb.set(1, 999);
    try testing.expectEqual(@as(u32, 999), t.rb.get(1).?.*);
}

test "ring buffer: clear" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 1);
    try t.rb.append(alloc, 2);
    t.rb.clear();
    try testing.expect(t.rb.is_empty());
    try testing.expectEqual(@as(usize, 0), t.rb.len);
}

test "ring buffer: growth" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(2).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 1);
    try t.rb.append(alloc, 2);
    try t.rb.append(alloc, 3); // triggers grow to cap 4
    try testing.expectEqual(@as(usize, 4), t.rb.capacity());
    try testing.expectEqual(@as(u32, 1), t.rb.popFirst().?);
    try testing.expectEqual(@as(u32, 2), t.rb.popFirst().?);
    try testing.expectEqual(@as(u32, 3), t.rb.popFirst().?);
}

test "ring buffer: wrapped iteration" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 1);
    try t.rb.append(alloc, 2);
    try t.rb.append(alloc, 3);
    _ = t.rb.popFirst(); // head=1
    try t.rb.append(alloc, 4); // wraps
    _ = t.rb.popFirst(); // head=2
    try t.rb.append(alloc, 5); // wraps

    // logical: [3, 4, 5]
    try t.check(2, 3, &.{ 3, 4, 5 });
}

test "ring buffer: orderedRemove first element" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 10);
    try t.rb.append(alloc, 20);
    try t.rb.append(alloc, 30);

    t.rb.orderedRemove(0);
    try t.check(1, 2, &.{ 20, 30 });
}

test "ring buffer: orderedRemove last element" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 10);
    try t.rb.append(alloc, 20);
    try t.rb.append(alloc, 30);

    t.rb.orderedRemove(2);
    try t.check(0, 2, &.{ 10, 20 });
}

test "ring buffer: orderedRemove middle non-wrapping" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 10);
    try t.rb.append(alloc, 20);
    try t.rb.append(alloc, 30);
    try t.rb.append(alloc, 40);

    t.rb.orderedRemove(1); // remove 20
    try t.check(1, 3, &.{ 10, 30, 40 });
}

test "ring buffer: orderedRemove single element" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 42);
    t.rb.orderedRemove(0);
    try testing.expect(t.rb.is_empty());
    try testing.expectEqual(@as(usize, 0), t.rb.len);
}

test "ring buffer: orderedRemove wrapping phys_src>0" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    // Create wrapped state: logical = [C, D, E, F]
    // items: [E, F, C, D], head=2
    try t.rb.append(alloc, 1); // A
    try t.rb.append(alloc, 2); // B
    try t.rb.append(alloc, 3); // C
    _ = t.rb.popFirst(); // remove A, head=1
    try t.rb.append(alloc, 4); // D
    _ = t.rb.popFirst(); // remove B, head=2
    try t.rb.append(alloc, 5); // E
    try t.rb.append(alloc, 6); // F

    // Now logical = [C=3, D=4, E=5, F=6], head=2
    // phys: [E=5, F=6, C=3, D=4]
    t.check(2, 4, &.{ 3, 4, 5, 6 }) catch |e| return e;

    // Remove logical index 0 (C=3 at phys 2, count=3)
    // phys_src = wrap(2+0+1, 4) = 3
    // phys_src+count = 3+3 = 6 > 4, wrapping case with phys_src>0
    t.rb.orderedRemove(0);
    // After: logical = [D=4, E=5, F=6]
    try t.check(3, 3, &.{ 4, 5, 6 });
}

test "ring buffer: orderedRemove wrapping phys_src==0" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    // Fill buffer, then pop to advance head
    try t.rb.append(alloc, 1); // A at 0
    try t.rb.append(alloc, 2); // B at 1
    try t.rb.append(alloc, 3); // C at 2
    try t.rb.append(alloc, 4); // D at 3
    _ = t.rb.popFirst(); // head=1, logical=[B,C,D]
    _ = t.rb.popFirst(); // head=2, logical=[C,D]
    _ = t.rb.popFirst(); // head=3, logical=[D]
    try t.rb.append(alloc, 5); // E at 0 (wrap)
    try t.rb.append(alloc, 6); // F at 1 (wrap)
    try t.rb.append(alloc, 7); // G at 2 (wrap)

    // items: [E=5, F=6, G=7, D=4], head=3, len=4
    // logical: [D=4(3), E=5(0), F=6(1), G=7(2)]

    try t.check(3, 4, &.{ 4, 5, 6, 7 });

    // Remove logical index 0 (D=4 at phys 3)
    // phys_src = wrap(3+0+1, 4) = 0
    // Falls into phys_src==0 else branch
    t.rb.orderedRemove(0);
    // After: logical = [E=5, F=6, G=7]
    try t.check(0, 3, &.{ 5, 6, 7 });
}

test "ring buffer: orderedRemove middle wrapping" {
    const alloc = testing.allocator;
    var t = try ringBufferTest(4).init(alloc);
    defer t.deinit();

    try t.rb.append(alloc, 1);
    try t.rb.append(alloc, 2);
    try t.rb.append(alloc, 3);
    try t.rb.append(alloc, 4);
    _ = t.rb.popFirst();
    _ = t.rb.popFirst();
    // head=2, logical=[3,4] → items[2]=3, items[3]=4
    try t.rb.append(alloc, 5); // items[0]=5
    try t.rb.append(alloc, 6); // items[1]=6
    // head=2, items=[5,6,3,4], logical=[3,4,5,6]

    // Remove logical index 2 (5 at phys 0, count=1)
    // phys_src = wrap(2+2+1, 4) = 1
    // phys_src+count = 1+1 = 2 <= 4, phys_src>0 → single memmove
    t.rb.orderedRemove(2);
    try t.check(2, 3, &.{ 3, 4, 6 });
}
