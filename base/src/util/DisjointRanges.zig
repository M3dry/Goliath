const std = @import("std");

const Allocator = std.mem.Allocator;

pub const Range = struct {
    start: u32,
    end: u32,

    pub inline fn contains(self: Range, other: Range) bool {
        return self.in(other.start) and self.inLimit(other.end);
    }

    /// Returns the merged range of `self` and `other` when they overlap or touch.
    pub inline fn overlap(self: Range, other: Range) ?Range {
        if (self.inLimit(other.start) or self.inLimit(other.end)) {
            return .{ .start = @min(self.start, other.start), .end = @max(self.end, other.end) };
        }
        return null;
    }

    pub inline fn in(self: Range, point: u32) bool {
        return self.start <= point and self.end > point;
    }

    pub inline fn inLimit(self: Range, point: u32) bool {
        return self.start <= point and self.end >= point;
    }
};

pub const DisjointRanges = struct {
    ranges: std.ArrayList(Range) = .empty,

    pub fn add(self: *DisjointRanges, alloc: Allocator, range: Range) !void {
        try self.addImpl(false, alloc, range);
    }

    pub fn addAssumeCapacity(self: *DisjointRanges, range: Range) void {
        self.addImpl(true, undefined, range);
    }

    fn addImpl(self: *DisjointRanges, comptime assume_capacity: bool, alloc: Allocator, range: Range) if (assume_capacity) void else error{OutOfMemory}!void {
        if (range.start >= range.end) return;

        var merged = range;
        var first: usize = 0;
        while (first < self.ranges.items.len and self.ranges.items[first].end < merged.start) : (first += 1) {}

        if (first == self.ranges.items.len) {
            if (assume_capacity) {
                self.ranges.appendAssumeCapacity(merged);
            } else {
                try self.ranges.append(alloc, merged);
            }
            return;
        }

        if (merged.end < self.ranges.items[first].start) {
            if (assume_capacity) {
                self.ranges.insertAssumeCapacity(first, merged);
            } else {
                try self.ranges.insert(alloc, first, merged);
            }
            return;
        }

        var end = first;
        while (end < self.ranges.items.len and self.ranges.items[end].start <= merged.end) : (end += 1) {
            merged.start = @min(merged.start, self.ranges.items[end].start);
            merged.end = @max(merged.end, self.ranges.items[end].end);
        }

        if (assume_capacity) {
            self.ranges.replaceRangeAssumeCapacity(first, end - first, &.{merged});
        } else {
            try self.ranges.replaceRange(alloc, first, end - first, &.{merged});
        }
    }

    /// A single add or remove can increase the range count by at most one.
    pub fn ensureUnusedCapacity(self: *DisjointRanges, alloc: Allocator) !void {
        try self.ranges.ensureUnusedCapacity(alloc, 1);
    }

    /// Returns the first range that can satisfy `size`, consuming its prefix.
    pub fn takeFirstFit(self: *DisjointRanges, size: u32) ?Range {
        if (size == 0) return null;

        for (self.ranges.items, 0..) |*free_range, i| {
            if (free_range.end - free_range.start < size) continue;

            const result = Range{ .start = free_range.start, .end = free_range.start + size };

            if (result.end == free_range.end) {
                _ = self.ranges.orderedRemove(i);
            } else {
                free_range.start = result.end;
            }

            return result;
        }

        return null;
    }

    /// Returns the smallest range that can satisfy `size`, consuming its prefix.
    pub fn takeBestFit(self: *DisjointRanges, size: u32) ?Range {
        if (size == 0) return null;

        var best: ?usize = null;
        for (self.ranges.items, 0..) |free_range, i| {
            if (free_range.end - free_range.start < size) continue;
            if (best == null or free_range.end - free_range.start < self.ranges.items[best.?].end - self.ranges.items[best.?].start) best = i;
        }

        const i = best orelse return null;
        const free_range = &self.ranges.items[i];
        const result = Range{ .start = free_range.start, .end = free_range.start + size };

        if (result.end == free_range.end) {
            _ = self.ranges.orderedRemove(i);
        } else {
            free_range.start = result.end;
        }

        return result;
    }

    pub fn remove(self: *DisjointRanges, alloc: Allocator, range: Range) !void {
        try self.removeImpl(false, alloc, range);
    }

    pub fn removeAssumeCapacity(self: *DisjointRanges, range: Range) void {
        self.removeImpl(true, undefined, range);
    }

    fn removeImpl(self: *DisjointRanges, comptime assume_capacity: bool, alloc: Allocator, range: Range) if (assume_capacity) void else error{OutOfMemory}!void {
        if (range.start >= range.end) return;

        var first: usize = 0;
        while (first < self.ranges.items.len and self.ranges.items[first].end <= range.start) : (first += 1) {}

        if (first == self.ranges.items.len or self.ranges.items[first].start >= range.end) return;

        var end = first + 1;
        while (end < self.ranges.items.len and self.ranges.items[end].start < range.end) : (end += 1) {}

        const first_range = self.ranges.items[first];
        const last_range = self.ranges.items[end - 1];
        var replacement: [2]Range = undefined;
        var replacement_len: usize = 0;

        if (first_range.start < range.start) {
            replacement[replacement_len] = .{ .start = first_range.start, .end = range.start };
            replacement_len += 1;
        }

        if (last_range.end > range.end) {
            replacement[replacement_len] = .{ .start = range.end, .end = last_range.end };
            replacement_len += 1;
        }

        if (assume_capacity) {
            self.ranges.replaceRangeAssumeCapacity(first, end - first, replacement[0..replacement_len]);
        } else {
            try self.ranges.replaceRange(alloc, first, end - first, replacement[0..replacement_len]);
        }
    }

    pub fn update(self: *DisjointRanges, alloc: Allocator, from: u32, by: u32, comptime negative: bool) !void {
        if (by == 0) return;

        const boundary = if (negative) from else from + 1;
        var first: usize = 0;
        while (first < self.ranges.items.len and self.ranges.items[first].end <= boundary) : (first += 1) {}

        if (first == self.ranges.items.len) return;
        if (!negative and self.ranges.items[first].start < boundary) {
            const crossing = self.ranges.items[first];
            try self.ranges.ensureUnusedCapacity(alloc, 1);
            self.ranges.items[first].end = boundary;
            self.ranges.insertAssumeCapacity(first + 1, .{ .start = boundary + by, .end = crossing.end + by });
            first += 2;
        }

        std.debug.assert(!negative or self.ranges.items[first].start >= boundary);

        for (self.ranges.items[first..]) |*range| {
            if (negative) {
                range.start -= by;
                range.end -= by;
            } else {
                range.start += by;
                range.end += by;
            }
        }

        if (negative and first > 0 and first < self.ranges.items.len and self.ranges.items[first - 1].end >= self.ranges.items[first].start) {
            self.ranges.items[first - 1].end = @max(self.ranges.items[first - 1].end, self.ranges.items[first].end);
            _ = self.ranges.orderedRemove(first);
        }
    }

    pub fn deinit(self: *DisjointRanges, alloc: Allocator) void {
        self.ranges.deinit(alloc);
        self.ranges = .empty;
    }
};
