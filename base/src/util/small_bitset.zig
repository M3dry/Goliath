const std = @import("std");
const Allocator = std.mem.Allocator;

pub fn SmallBitset(comptime stack_count: usize) type {
    const stack_words: u64 = @max((stack_count + 63) / 64, @sizeOf([]u64) / @sizeOf(u64));

    return struct {
        const Self = @This();
        const tag_mask: u32 = 1 << 31;

        pub const empty: Self = .{};

        len_field: u32 = 0, // bit length; MSB set => heap-backed
        words: union {
            stack: [stack_words]u64,
            heap: []u64,
        } = .{ .stack = @splat(0) },

        pub fn onHeap(self: *const Self) bool {
            return (self.len_field & tag_mask) != 0;
        }

        pub fn len(self: *const Self) usize {
            return self.len_field & (tag_mask - 1);
        }

        pub fn capacity(self: *const Self) usize {
            return if (self.onHeap()) self.words.heap.len * 64 else stack_words * 64;
        }

        pub fn backing(self: *Self) []u64 {
            if (self.onHeap()) return self.words.heap;
            return &self.words.stack;
        }

        pub fn resize(self: *Self, alloc: Allocator, new_len: usize) Allocator.Error!void {
            std.debug.assert(new_len <= max_mask);
            const new_words = (new_len + 63) / 64;
            if (self.onHeap()) {
                if (new_words <= stack_words) {
                    self.moveToStack(alloc, new_words);
                } else if (new_words > self.words.heap.len) {
                    try self.growHeap(alloc, new_words);
                } else {
                    self.words = .{ .heap = try alloc.realloc(self.words.heap, new_words) };
                }
            } else if (new_words > stack_words) {
                try self.moveToHeap(alloc, new_words);
            }
            self.setLen(new_len);
            self.trimTail();
        }

        pub fn ensureCapacity(self: *Self, alloc: Allocator, cap: usize) Allocator.Error!void {
            if (cap <= self.capacity()) return;

            if (self.onHeap()) {
                try self.growHeap(alloc, (cap + 63) / 64);
            } else {
                try self.moveToHeap(alloc, (cap + 63) / 64);
            }
        }

        pub fn findFirstSet(self: *const Self, offset: usize) ?usize {
            const wl = self.wordLen();
            if (wl == 0 or offset >= self.len()) return null;

            const words = if (self.onHeap()) self.words.heap else &self.words.stack;

            var i = offset / 64;
            var w = words[i] & ~lowBitsMask(@intCast(offset % 64));
            while (true) {
                if (w != 0) return i * 64 + @as(usize, @ctz(w));
                i += 1;
                if (i == wl) return null;
                w = words[i];
            }
        }

        pub fn findFirstUnset(self: *const Self, offset: usize) ?usize {
            const wl = self.wordLen();
            if (wl == 0 or offset >= self.len()) return null;

            const words = if (self.onHeap()) self.words.heap else &self.words.stack;

            var i = offset / 64;
            var w = ~words[i] & ~lowBitsMask(@intCast(offset % 64));
            while (true) {
                if (w != 0) {
                    const idx = i * 64 + @as(usize, @ctz(w));
                    return if (idx < self.len()) idx else null;
                }
                i += 1;
                if (i == wl) return null;
                w = ~words[i];
            }
        }

        pub fn findLastSet(self: *const Self, offset: usize) ?usize {
            const l = self.len();
            if (l == 0) return null;

            std.debug.assert(offset <= l);
            const hi = offset;
            const words = if (self.onHeap()) self.words.heap else &self.words.stack;

            var i = (hi - 1) / 64;
            var w = words[i];
            if (hi % 64 != 63) w &= lowBitsMask(@intCast(hi % 64 + 1));

            while (true) {
                if (w != 0) return i * 64 + @as(usize, 63 - @clz(w));
                if (i == 0) return null;
                i -= 1;
                w = words[i];
            }
        }

        pub fn findLastUnset(self: *const Self, offset: usize) ?usize {
            const l = self.len();
            const wl = self.wordLen();
            if (wl == 0) return null;

            std.debug.assert(offset <= l);
            const hi = offset;
            const words = if (self.onHeap()) self.words.heap else &self.words.stack;

            var i = (hi - 1) / 64;
            var w = ~words[i];
            if (i == wl - 1 and l % 64 != 0) w &= lowBitsMask(@intCast(l % 64));
            if (hi % 64 != 63) w &= lowBitsMask(@intCast(hi % 64 + 1));
            while (true) {
                if (w != 0) return i * 64 + @as(usize, 63 - @clz(w));
                if (i == 0) return null;
                i -= 1;
                w = ~words[i];
            }
        }

        pub fn set(self: *Self, alloc: Allocator, index: usize) Allocator.Error!void {
            if (index >= self.capacity()) try self.resize(alloc, index + 1);
            self.setAssumeCapacity(index);
        }

        pub fn setAssumeCapacity(self: *Self, index: usize) void {
            std.debug.assert(index < self.capacity());
            std.debug.assert(index + 1 <= max_mask);
            self.backing()[index / 64] |= @as(u64, 1) << @intCast(index % 64);
            if (index + 1 > self.len()) self.setLen(index + 1);
        }

        pub fn unset(self: *Self, index: usize) void {
            if (index >= self.len()) return;
            self.backing()[index / 64] &= ~(@as(u64, 1) << @intCast(index % 64));
        }

        pub fn isSet(self: *const Self, index: usize) bool {
            if (index >= self.len()) return false;

            const words = if (self.onHeap()) self.words.heap else &self.words.stack;
            return (words[index / 64] & (@as(u64, 1) << @intCast(index % 64))) != 0;
        }

        pub fn bitCount(self: *const Self) usize {
            var count: usize = 0;
            const words = if (self.onHeap()) self.words.heap else &self.words.stack;
            for (words[0..self.wordLen()]) |w| count += @popCount(w);
            return count;
        }

        pub fn deinit(self: *Self, alloc: Allocator) void {
            if (self.onHeap()) alloc.free(self.words.heap);
            self.len_field = 0;
            self.words = .{ .stack = @splat(0) };
        }

        const max_mask = (1 << 31) - 1;

        fn wordLen(self: *const Self) usize {
            return (self.len() + 63) / 64;
        }

        fn lowBitsMask(n: u6) u64 {
            return (@as(u64, 1) << n) - 1;
        }

        fn growHeap(self: *Self, alloc: Allocator, new_words: usize) Allocator.Error!void {
            std.debug.assert(new_words > self.words.heap.len);
            const old = self.words.heap.len;
            self.words = .{ .heap = try alloc.realloc(self.words.heap, new_words) };
            @memset(self.words.heap[old..new_words], 0);
        }

        fn setLen(self: *Self, new_len: usize) void {
            self.len_field = (self.len_field & tag_mask) | @as(u32, @intCast(new_len));
        }

        fn trimTail(self: *Self) void {
            const n = self.len();
            const wl = self.wordLen();
            if (n % 64 != 0) self.backing()[wl - 1] &= lowBitsMask(@intCast(n % 64));
            @memset(self.backing()[wl..], 0);
        }

        fn moveToHeap(self: *Self, alloc: Allocator, new_words: usize) Allocator.Error!void {
            const buf = try alloc.alloc(u64, new_words);
            @memset(buf, 0);
            const n = self.wordLen();
            @memcpy(buf[0..n], self.words.stack[0..n]);
            self.words = .{ .heap = buf };
            self.len_field |= tag_mask;
        }

        fn moveToStack(self: *Self, alloc: Allocator, new_words: usize) void {
            const src = self.words.heap;
            const copy = @min(new_words, src.len);
            self.words = .{ .stack = @splat(0) };
            @memcpy(self.words.stack[0..copy], src[0..copy]);
            alloc.free(src);
            self.len_field = @as(u32, @intCast(new_words * 64));
        }
    };
}

test {
    std.testing.refAllDecls(@This());
    std.debug.print("{}\n", .{@sizeOf(SmallBitset(8))});
}

test "small bitset" {
    var bs: SmallBitset(64) = .{};
    const alloc = std.testing.allocator;
    defer bs.deinit(alloc);

    try bs.set(alloc, 3);
    try bs.set(alloc, 63);
    try std.testing.expect(!bs.onHeap());
    // stack storage fills the []u64 union member: 2 words = 128 bits
    try std.testing.expectEqual(@as(usize, 128), bs.capacity());
    try bs.set(alloc, 127);
    try std.testing.expect(!bs.onHeap());
    try std.testing.expectEqual(@as(usize, 128), bs.len());
    try bs.set(alloc, 128);
    try std.testing.expect(bs.onHeap());
    try std.testing.expectEqual(@as(usize, 3), bs.words.heap.len);
    try std.testing.expect(bs.isSet(128));
    try std.testing.expectEqual(@as(usize, 4), bs.bitCount());

    bs.unset(3);
    try std.testing.expect(!bs.isSet(3));
    try std.testing.expectEqual(@as(usize, 3), bs.bitCount());

    try bs.resize(alloc, 3);
    try std.testing.expect(!bs.onHeap());
    try std.testing.expectEqual(@as(usize, 3), bs.len());
    try std.testing.expect(!bs.isSet(127));

    try bs.resize(alloc, 200);
    try std.testing.expect(bs.onHeap());
    try std.testing.expect(!bs.isSet(127));
    try std.testing.expect(!bs.isSet(128));
    try std.testing.expectEqual(@as(usize, 0), bs.bitCount());

    var cap: SmallBitset(64) = .{};
    defer cap.deinit(alloc);
    try cap.ensureCapacity(alloc, 200);
    try std.testing.expect(cap.onHeap());
    try std.testing.expectEqual(@as(usize, 0), cap.len());
    try cap.set(alloc, 199);
    try std.testing.expect(cap.isSet(199));
    try std.testing.expectEqual(@as(usize, 200), cap.len());
}

test "small bitset find" {
    var bs: SmallBitset(64) = .{};
    const alloc = std.testing.allocator;
    defer bs.deinit(alloc);

    try bs.set(alloc, 5);
    try bs.set(alloc, 70);
    try std.testing.expectEqual(@as(?usize, 5), bs.findFirstSet(0));
    try std.testing.expectEqual(@as(?usize, 70), bs.findFirstSet(6));
    try std.testing.expectEqual(@as(?usize, null), bs.findFirstSet(71));
    try std.testing.expectEqual(@as(?usize, 70), bs.findLastSet(bs.len() - 1));
    try std.testing.expectEqual(@as(?usize, 5), bs.findLastSet(69));
    try std.testing.expectEqual(@as(?usize, 70), bs.findLastSet(bs.len()));

    try std.testing.expectEqual(@as(?usize, 0), bs.findFirstUnset(0));
    try std.testing.expectEqual(@as(?usize, 64), bs.findFirstUnset(64));
    try std.testing.expectEqual(@as(?usize, 69), bs.findLastUnset(bs.len() - 1));
    bs.setAssumeCapacity(0);
    try std.testing.expectEqual(@as(?usize, 1), bs.findFirstUnset(0));
    try std.testing.expectEqual(@as(?usize, 69), bs.findLastUnset(70));
    bs.setAssumeCapacity(1);
    try std.testing.expectEqual(@as(?usize, 2), bs.findFirstUnset(2));
    try std.testing.expectEqual(@as(?usize, 69), bs.findLastUnset(69));

    try bs.resize(alloc, 3);
    bs.setAssumeCapacity(0);
    bs.setAssumeCapacity(1);
    bs.setAssumeCapacity(2);
    try std.testing.expectEqual(@as(?usize, null), bs.findFirstUnset(0)); // full
    try std.testing.expectEqual(@as(?usize, null), bs.findLastUnset(2));
}
