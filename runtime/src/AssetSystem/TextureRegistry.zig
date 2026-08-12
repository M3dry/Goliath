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
    ticket: base.Transport.Ticket = .none,
    default_image: DefaultKind = .white,
}) = .empty,
textures_free_list: std.ArrayList(u32) = .empty,

texture_pool: base.TexturePool,
texture_pool_capacity: u32,
default_images: [@typeInfo(DefaultKind).@"enum".fields.len]Image = @splat(.{}),
default_sampler: base.Sampler = .null_handle,
sampled_textures: std.MultiArrayList(struct {
    ref_count: u32 = 0,
    texture: u32 = std.math.maxInt(u32),
    sampler: base.Sampler = .null_handle,
    default_image: DefaultKind = .white,
}) = .empty,
sampled_texture_free_list: std.ArrayList(u32) = .empty,
pending_sampled_textures: std.ArrayList(u32) = .empty,

alloc: Allocator,

const TextureRegistry = @This();

pub const DefaultKind = enum(u8) {
    white,
    black,
    flat_normal,
    metallic_roughness,
};

const Blob = struct {
    sampler: base.Sampler.Description,
    texture_gid: Gid,
};

pub fn init(gc: *const base.GraphicsCtx, transport: *base.Transport, alloc: Allocator) !TextureRegistry {
    var self: TextureRegistry = .{
        .texture_pool = try .init(gc, 1000),
        .texture_pool_capacity = 1000,
        .alloc = alloc,
    };
    errdefer self.texture_pool.deinitNow(gc);

    try self.createDefaultImages(gc, transport);
    return self;
}

fn createDefaultImages(self: *TextureRegistry, gc: *const base.GraphicsCtx, transport: *base.Transport) !void {
    inline for (@typeInfo(DefaultKind).@"enum".fields, 0..) |kind_field, i| {
        try self.createDefaultImage(gc, transport, std.meta.stringToEnum(DefaultKind, kind_field.name).?, i);
    }

    self.default_sampler = try base.Sampler.init(gc, .{});
    errdefer self.default_sampler.deinitNow(gc);
}

fn createDefaultImage(self: *TextureRegistry, gc: *const base.GraphicsCtx, transport: *base.Transport, kind: DefaultKind, index: usize) !void {
    const fmt = base.vk.Format.r8g8b8a8_srgb;
    const pixel = switch (kind) {
        .white => [4]u8{ 255, 255, 255, 255 },
        .black => [4]u8{ 0, 0, 0, 255 },
        .flat_normal => [4]u8{ 128, 128, 255, 255 },
        .metallic_roughness => [4]u8{ 255, 255, 0, 255 },
    };

    var image = try base.Image2D.init(gc, gc.vma_alloc, "default_image", .{
        .format = fmt,
        .extent = .{ .width = 1, .height = 1 },
        .usage = .{ .transfer_dst_bit = true, .sampled_bit = true },
    });
    errdefer image.deinitNow(gc.vma_alloc);

    const upload_tick = try transport.uploadImage(
        false,
        fmt,
        .{ .width = 1, .height = 1, .depth = 1 },
        &pixel,
        null,
        image.handle,
        .{
            .aspect_mask = .{ .color_bit = true },
            .mip_level = 0,
            .base_array_layer = 0,
            .layer_count = 1,
        },
        .{ .x = 0, .y = 0, .z = 0 },
        .undefined,
        .shader_read_only_optimal,
        .{ .fragment_shader_bit = true, .compute_shader_bit = true },
        .{ .shader_read_bit = true },
    );
    errdefer transport.unqueue(upload_tick, false);

    // Startup, one-time: block until the pixel lands.
    while (!try transport.isReady(upload_tick)) {
        try transport.drain(gc);
        std.Thread.yield() catch {};
    }

    self.default_images[index].image = image;
    self.default_images[index].view = try base.ImageView.init(gc, .{
        .image = image.handle,
        .format = fmt,
        .subresource_range = .{
            .aspect_mask = .{ .color_bit = true },
            .base_mip_level = 0,
            .level_count = 1,
            .base_array_layer = 0,
            .layer_count = 1,
        },
    });
    errdefer self.default_images[index].view.deinitNow(gc);
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
    self.pending_sampled_textures.deinit(self.alloc);

    for (&self.default_images) |*img| {
        img.deinit(destroy_queue);
    }
    self.default_sampler.deinit(destroy_queue);

    self.texture_pool.deinit(destroy_queue);
}

pub fn new_texture(self: *TextureRegistry) !u32 {
    if (self.textures_free_list.pop()) |id| return id;

    _ = try self.textures.append(self.alloc, .{});
    return @intCast(self.textures.len - 1);
}

pub fn new_sampled_texture(self: *TextureRegistry, gc: *const base.GraphicsCtx, destroy_queue: *base.DestroyQueue) !u32 {
    if (self.sampled_texture_free_list.pop()) |id| return id;

    _ = try self.sampled_textures.append(self.alloc, .{});
    const id: u32 = @intCast(self.sampled_textures.len - 1);

    if (id >= self.texture_pool_capacity) try self.resizeTexturePool(gc, destroy_queue, @intFromFloat(@as(f64, @floatFromInt(self.texture_pool_capacity)) * 1.5));

    self.texture_pool.update(gc, id, self.default_images[0].view.handle, .shader_read_only_optimal, self.default_sampler.handle); // provisional; the role default is set at acquire

    return id;
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

pub fn acquire_sampled_texture(self: *TextureRegistry, gc: *const base.GraphicsCtx, transport: *base.Transport, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, id: u32, delta: u32) !void {
    std.debug.assert(delta > 0);
    const slice = self.sampled_textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    if (ref_count.* == 0) {
        const sampler = &slice.items(.sampler)[id];
        const dense = &slice.items(.texture)[id];

        const data = try loader.load(cold);

        // var reader = std.Io.Reader.fixed(data);
        // const blob = try reader.takeStruct(Blob, .little); - Can't use because takeStruct requires packed or extern on Blob
        const parsed_blob = try std.json.parseFromSlice(Blob, self.alloc, data, .{ .duplicate_field_behavior = .@"error" });
        defer parsed_blob.deinit();
        const blob = parsed_blob.value;

        sampler.* = try base.Sampler.init(gc, blob.sampler);
        errdefer sampler.deinitNow(gc);

        const kind, dense.* = resolver.lookup(resolved, blob.texture_gid) orelse return error.InvalidTextureGid;
        if (kind != .texture) return error.KindNotTexture;
        if (dense.* == std.math.maxInt(u32)) return error.TextureNotDense;

        const tex_slice = self.textures.slice();
        const role_default = tex_slice.items(.default_image)[dense.*];
        slice.items(.default_image)[id] = role_default;
        const ticket = tex_slice.items(.ticket)[dense.*];
        const view = tex_slice.items(.image)[dense.*].view.handle;

        if (try transport.isReady(ticket)) {
            // texture already on the GPU — bind now
            self.texture_pool.update(gc, id, view, .shader_read_only_optimal, sampler.handle);
        } else {
            // show the role's placeholder until the upload lands; tick()
            // applies the real binding.
            self.texture_pool.update(gc, id, self.default_images[@intFromEnum(role_default)].view.handle, .shader_read_only_optimal, self.default_sampler.handle);
            try self.pending_sampled_textures.append(self.alloc, id);
        }
    }

    ref_count.* += delta;
}

pub fn release_texture(self: *TextureRegistry, destroy_queue: *base.DestroyQueue, transport: *base.Transport, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);
    const slice = self.textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* >= delta);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

    const ticket = slice.items(.ticket)[id];
    transport.unqueue(ticket, true);

    var image = slice.items(.image)[id];
    image.deinit(destroy_queue);

    try self.textures_free_list.append(self.alloc, id);
    return .released;
}

pub fn release_sampled_texture(self: *TextureRegistry, gc: *const base.GraphicsCtx, destroy_queue: *base.DestroyQueue, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);
    const slice = self.sampled_textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* >= delta);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

    var sampler = slice.items(.sampler)[id];
    sampler.deinit(destroy_queue);

    try self.sampled_texture_free_list.append(self.alloc, id);
    // Drop any pending binding — the sampler is gone, and the slot must not be
    // re-bound to a destroyed handle by a later tick().
    var i: usize = 0;
    while (i < self.pending_sampled_textures.items.len) {
        if (self.pending_sampled_textures.items[i] == id) {
            _ = self.pending_sampled_textures.swapRemove(i);
        } else i += 1;
    }

    self.texture_pool.update(gc, id, self.default_images[@intFromEnum(slice.items(.default_image)[id])].view.handle, .shader_read_only_optimal, self.default_sampler.handle);

    return .released;
}

pub fn tick(self: *TextureRegistry, transport: *base.Transport, gc: *const base.GraphicsCtx) !void {
    const tex_slice = self.textures.slice();
    const sam_tex_slice = self.sampled_textures.slice();

    const tickets = tex_slice.items(.ticket);
    const images = tex_slice.items(.image);

    const textures = sam_tex_slice.items(.texture);
    const samplers = sam_tex_slice.items(.sampler);

    var i: usize = 0;
    while (i < self.pending_sampled_textures.items.len) {
        const id = self.pending_sampled_textures.items[i];
        const texture = textures[id];
        if (try transport.isReady(tickets[texture])) {
            self.texture_pool.update(gc, id, images[texture].view.handle, .shader_read_only_optimal, samplers[id].handle);
            _ = self.pending_sampled_textures.swapRemove(i);
        } else i += 1;
    }
}

fn resizeTexturePool(self: *TextureRegistry, gc: *const base.GraphicsCtx, destroy_queue: *base.DestroyQueue, new_cap: u32) !void {
    if (self.texture_pool_capacity >= new_cap) return;

    self.texture_pool.deinit(destroy_queue);
    self.texture_pool = try .init(gc, new_cap);
    errdefer self.texture_pool.deinitNow(destroy_queue);

    const slice = self.sampled_textures.slice();
    const images = self.textures.items(.image);
    for (0.., slice.items(.ref_count), slice.items(.sampler)) |id, ref_count, sampler| {
        if (ref_count == 0) {
            self.texture_pool.update(gc, @intCast(id), self.default_images[@intFromEnum(slice.items(.default_image)[id])].view.handle, .shader_read_only_optimal, self.default_sampler.handle);
        } else {
            self.texture_pool.update(gc, @intCast(id), images[id].view.handle, .shader_read_only_optimal, sampler.handle);
        }
    }
}

test {
    std.testing.refAllDecls(@This());
}
