const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;
const Loader = @import("Loader.zig");
const resolver = @import("resolver.zig");
const types = @import("Types.zig");
const Gid = types.Gid;

const Image = struct {
    image: base.Image2D = .{},
    view: base.ImageView = .{},

    pub fn deinit(self: *Image, destroy_queue: *base.DestroyQueue) void {
        self.image.deinit(destroy_queue);
        self.view.deinit(destroy_queue);
    }
};

textures: std.MultiArrayList(struct {
    ref_count: u32 = 0,
    image: Image = .{},
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

const Blob = struct {
    sampler: base.Sampler.Description,
    texture_gid: Gid,
};

pub fn init(gc: *const base.GraphicsCtx, alloc: Allocator) !TextureRegistry {
    return .{
        .texture_pool = try .init(gc, 1000),
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
    return @intCast(self.sampled_textures.len - 1);
}

pub fn acquire_texture(self: *TextureRegistry, gc: *const base.GraphicsCtx, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, id: u32, delta: u32) !void {
    std.debug.assert(delta > 0);
    _ = gc;
    _ = resolved;

    const slice = self.textures.slice();
    const ref_count = &slice.items(.ref_count)[id];
    if (ref_count.* == 0) {
        const data = try loader.load(cold);
        _ = data;
    }

    ref_count.* += delta;
}

pub fn acquire_sampled_texture(self: *TextureRegistry, gc: *const base.GraphicsCtx, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, id: u32, delta: u32) !void {
    std.debug.assert(delta > 0);
    const slice = self.sampled_textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    if (ref_count.* == 0) {
        const data = try loader.load(cold);

        // var reader = std.Io.Reader.fixed(data);
        // const blob = try reader.takeStruct(Blob, .little); - Can't use because takeStruct requires packed or extern on Blob
        const parsed_blob = try std.json.parseFromSlice(Blob, self.alloc, data, .{ .duplicate_field_behavior = .@"error" });
        defer parsed_blob.deinit();
        const blob = parsed_blob.value;

        var sampler = try base.Sampler.init(gc, blob.sampler);
        errdefer sampler.deinitNow(gc);

        const kind, const dense = resolver.lookup(resolved, blob.texture_gid) orelse return error.InvalidTextureGid;
        if (kind != .texture) return error.KindNotTexture;

        const image = self.textures.items(.image)[dense];
        self.texture_pool.update(gc, id, image.view.handle, .shader_read_only_optimal, sampler.handle);
    }

    ref_count.* += delta;
}

pub fn release_texture(self: *TextureRegistry, destroy_queue: *base.DestroyQueue, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);
    const slice = self.textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* != 0);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

    var image = slice.items(.image)[id];
    image.deinit(destroy_queue);

    try self.textures_free_list.append(self.alloc, id);
    return .released;
}

pub fn release_sampled_texture(self: *TextureRegistry, destroy_queue: *base.DestroyQueue, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);
    const slice = self.sampled_textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* != 0);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

    var sampler = slice.items(.sampler)[id];
    sampler.deinit(destroy_queue);

    try self.textures_free_list.append(self.alloc, id);
    return .released;
}

test {
    std.testing.refAllDecls(@This());
}
