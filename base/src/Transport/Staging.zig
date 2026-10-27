const std = @import("std");

const FreeFn = @import("Interface.zig").FreeFn;
const Allocator = std.mem.Allocator;
const RingBuffer = @import("../util.zig").RingBuffer;

const Staging = @This();

const FreeFnCtx = struct {
    staging: *Staging,
    page_ptr: [*]u8, // order in `pages` isn't guaranteed so we scan the full arraylist for a page pointer patch

    pub fn free_fn(ctx_: ?*anyopaque, _: *anyopaque) void {
        var ctx: FreeFnCtx = undefined;
        @memcpy(std.mem.asBytes(&ctx), @as([*]u8, @ptrCast(ctx_.?)));

        ctx.staging.decrPage(ctx.page_ptr);
    }
};

const SpecialFreeFnCtx = struct {
    staging: *Staging,
    slice_len: usize,

    pub fn free_fn(ctx_: ?*anyopaque, ptr_: *anyopaque) void {
        const ctx: *SpecialFreeFnCtx = @ptrCast(@alignCast(ctx_.?));

        const ptr = @as([*]u8, @ptrCast(ptr_));
        ctx.staging.alloc.free((ptr - @sizeOf(SpecialFreeFnCtx))[0..ctx.slice_len]);
    }
};

const Page = struct {
    occupancy: u32 = 0,
    ctx_count: u32 = 0,
    // at the end of the page the FreeFn context will be written, so writable area at a given point is [write_start..page_size - ctx_count*@sizeOf(FreeFnCtx)]
    page_ptr: [*]u8,
    write_start: usize = 0,

    pub inline fn fits(self: *Page, data_len: usize) ?bool {
        const avail = Staging.page_size - self.write_start - self.ctx_count * @sizeOf(FreeFnCtx);

        if (avail >= (data_len + @sizeOf(FreeFnCtx))) {
            return (avail - data_len - @sizeOf(FreeFnCtx)) == 0;
        }

        return null;
    }
};

const page_size: usize = 32 * std.math.pow(usize, 2, 20); //33554432, // 32 MiB

io: std.Io,
mutex: std.Io.Mutex = .init,
alloc: Allocator,
pages: std.ArrayList(Page) = .empty,
locked_start: usize = 0,

pub fn init(alloc: Allocator, io: std.Io) Staging {
    return .{
        .alloc = alloc,
        .io = io,
    };
}

pub fn deinit(self: *Staging) void {
    for (self.pages.items) |*page| {
        self.alloc.free(@as([]u8, page.page_ptr[0..Staging.page_size]));
    }
    self.pages.deinit(self.alloc);
}

pub fn stage(self: *Staging, data: []const u8) !struct {
    staged: []u8,
    free_fn: FreeFn,
    ctx: *anyopaque,
} {
    self.mutex.lockUncancelable(self.io);
    defer self.mutex.unlock(self.io);

    if (data.len + @sizeOf(FreeFnCtx) > Staging.page_size) {
        const page_alloc = try self.alloc.alignedAlloc(u8, .@"8", data.len + @sizeOf(SpecialFreeFnCtx));
        errdefer self.alloc.free(page_alloc);

        @memcpy(page_alloc[0..@sizeOf(SpecialFreeFnCtx)], std.mem.asBytes(&SpecialFreeFnCtx{
            .staging = self,
            .slice_len = data.len + @sizeOf(SpecialFreeFnCtx),
        }));

        const staged = page_alloc[@sizeOf(SpecialFreeFnCtx)..];
        @memcpy(staged, data);

        return .{
            .free_fn = SpecialFreeFnCtx.free_fn,
            .staged = staged,
            .ctx = page_alloc.ptr,
        };
    }

    const p, const lock = for (self.pages.items[0..self.locked_start]) |*page| {
        if (page.fits(data.len)) |lock| break .{ page, lock };
    } else .{ try self.newPage(), data.len == Staging.page_size - @sizeOf(FreeFnCtx) };

    const staged = p.page_ptr[p.write_start..][0..data.len];
    @memcpy(staged, data);

    p.write_start += data.len;
    p.occupancy += 1;
    p.ctx_count += 1;

    const ctx = p.page_ptr[Staging.page_size - p.ctx_count * @sizeOf(FreeFnCtx) ..];
    @memcpy(ctx, std.mem.asBytes(&FreeFnCtx{
        .staging = self,
        .page_ptr = p.page_ptr,
    }));

    if (lock) {
        // Page `p` must be in the range [0..locked_start), thus locked_start > 0 so - 1 won't underflow
        self.locked_start -= 1;
        std.mem.swap(Page, p, &self.pages.items[self.locked_start]);
    }

    return .{
        .free_fn = FreeFnCtx.free_fn,
        .staged = staged,
        .ctx = ctx,
    };
}

fn newPage(self: *Staging) !*Page {
    const page_alloc = try self.alloc.alloc(u8, Staging.page_size);
    errdefer self.alloc.free(page_alloc);

    const p = try self.pages.addOne(self.alloc);
    const new_p = &self.pages.items[self.locked_start];
    std.mem.swap(Page, p, new_p);
    self.locked_start += 1;

    new_p.* = .{
        .page_ptr = page_alloc.ptr,
    };

    return new_p;
}

fn decrPage(self: *Staging, page_ptr: [*]u8) void {
    self.mutex.lockUncancelable(self.io);
    defer self.mutex.unlock(self.io);

    for (self.pages.items, 0..) |*page, i| {
        if (page.page_ptr != page_ptr) continue;

        page.occupancy -= 1;
        if (page.occupancy != 0) continue;

        page.ctx_count = 0;
        page.write_start = 0;
        if (i >= self.locked_start) {
            std.mem.swap(Page, &self.pages.items[self.locked_start], page);
            self.locked_start += 1;
        }
    }
}

test {
    std.testing.refAllDecls(@This());
}
