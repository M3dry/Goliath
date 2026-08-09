const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;
const Loader = @import("Loader.zig");

textures: std.MultiArrayList(struct {
    ref_count: u32 = 0,
    image: base.Image2D = .{},
}) = .empty,
textures_free_list: std.ArrayList(u32) = .empty,

texture_pool: base.TexturePool,
sampled_textures: std.MultiArrayList(struct {
    ref_count: u32 = 0,
    sampler: base.Sampler = .null_handle,
}) = .empty,
sampled_texture_free_list: std.ArrayList(u32) = .empty,

alloc: Allocator,

const TextureRegistry = @This();

pub fn init(gc: *const base.GraphicsCtx, alloc: Allocator) !TextureRegistry {
    return .{
        .texture_pool = try .init(gc.dev, 1000),
        .alloc = alloc,
    };
}

pub fn deinit(self: *TextureRegistry, destroy_queue: *base.DestroyQueue) void {
    const textures_slice = self.textures.slice();
    for (textures_slice.items(.ref_count), textures_slice.items(.image)) |ref_count, *tex| {
        if (ref_count == 0) continue;

        tex.deinit(destroy_queue);
    }
    self.textures.deinit(self.alloc);
    self.textures_free_list.deinit(self.alloc);

    const sampled_textures_slice = self.sampled_textures.slice();
    for (sampled_textures_slice.items(.ref_count), sampled_textures_slice.items(.sampler)) |ref_count, *sampler| {
        if (ref_count == 0) continue;

        sampler.deinit(destroy_queue);
    }
    self.sampled_textures.deinit(self.alloc);
    self.sampled_texture_free_list.deinit(self.alloc);

    self.texture_pool.deinit(destroy_queue);
}

pub fn new_texture(self: *TextureRegistry) !u32 {
    if (self.textures_free_list.pop()) |id| return id;

    _ = try self.textures.append(self.alloc, .{});
    return @intCast(self.textures.len - 1);
}

pub fn new_sampled_texture(self: *TextureRegistry) !u32 {
    if (self.sampled_texture_free_list.pop()) |id| return id;

    _ = try self.sampled_texture_free_list.addOne(self.alloc);
    _ = try self.sampled_textures.append(self.alloc, .{});
    return @intCast(self.sampled_texture_free_list.items.len - 1);
}

pub fn acquire_texture(self: *TextureRegistry, loader: *Loader, cold: *const Loader.ColdAsset, id: u32) !void {
    const slice = self.textures.slice();
    const ref_count = &slice.items(.ref_count)[id];
    if (ref_count.* == 0) {
        const data = try loader.load(cold);
        _ = data;
    }

    ref_count.* += 1;
}

pub fn acquire_sampled_texture(self: *TextureRegistry, loader: *Loader, cold: *const Loader.ColdAsset, id: u32) !void {
    const slice = self.sampled_textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    if (ref_count.* == 0) {
        const data = try loader.load(cold);
        _ = data;
    }

    ref_count.* += 1;
}

pub fn release_texture(self: *TextureRegistry, destroy_queue: *base.DestroyQueue, id: u32) !void {
    const slice = self.textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* != 0);
    ref_count.* -= 1;

    if (ref_count.* != 0) return;

    var image = slice.items(.image)[id];
    image.deinit(destroy_queue);

    try self.textures_free_list.append(self.alloc, id);
}

pub fn release_sampled_texture(self: *TextureRegistry, destroy_queue: *base.DestroyQueue, id: u32) !void {
    const slice = self.sampled_textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* != 0);
    ref_count.* -= 1;

    if (ref_count.* != 0) return;

    var sampler = slice.items(.sampler)[id];
    sampler.deinit(destroy_queue);

    try self.textures_free_list.append(self.alloc, id);
}

test {
    std.testing.refAllDecls(@This());
}
