const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;
const Loader = @import("Loader.zig");
const resolver = @import("resolver.zig");
const types = @import("Types.zig");
const Gid = types.Gid;
const Entry = types.Entry;

const Image = struct {
    image: base.Image2D = .{},
    view: base.ImageView = .{},

    pub fn deinit(self: *Image, destroy_queue: *base.DestroyQueue) void {
        self.image.deinit(destroy_queue);
        self.view.deinit(destroy_queue);
    }

    pub fn deinitNow(self: *Image, gc: *const base.GraphicsCtx) void {
        self.image.deinitNow(gc);
        self.view.deinitNow(gc);
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

pub const IngestTexture = struct {
    default_image: DefaultKind,
    data: union(enum) {
        file_path: []const u8,
        uri: []const u8,
        encoded_blob: []const u8,
        decoded_blob: struct {
            format: base.vk.Format,
            width: u32,
            height: u32,
            blob: []const u8,
        },
    },
};

pub const IngestSampledTexture = struct {
    texture: Gid,
    sampler: base.Sampler.Description,
};

const TextureBlobHeader = extern struct {
    format: base.vk.Format,
    width: u32,
    height: u32,
    default_image: DefaultKind,
};

const SampledTextureBlob = struct {
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
    errdefer image.deinitNow(gc);

    const upload_tick = try transport.uploadImage(
        false,
        fmt,
        .{ .width = 1, .height = 1, .depth = 1 },
        &pixel,
        null,
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
    self.default_images[index].view = try base.ImageView.init(gc, .fromImage(&image));
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

pub fn deinitNow(self: *TextureRegistry, gc: *const base.GraphicsCtx) void {
    const textures_slice = self.textures.slice();
    for (textures_slice.items(.ref_count), textures_slice.items(.image)) |ref_count, *tex| {
        if (ref_count == 0) continue;

        tex.deinitNow(gc);
    }
    self.textures.deinit(self.alloc);
    self.textures_free_list.deinit(self.alloc);

    const sampled_textures_slice = self.sampled_textures.slice();
    for (sampled_textures_slice.items(.ref_count), sampled_textures_slice.items(.sampler)) |ref_count, *sampler| {
        if (ref_count == 0) continue;

        sampler.deinitNow(gc);
    }
    self.sampled_textures.deinit(self.alloc);
    self.sampled_texture_free_list.deinit(self.alloc);
    self.pending_sampled_textures.deinit(self.alloc);

    for (&self.default_images) |*img| {
        img.deinitNow(gc);
    }
    self.default_sampler.deinitNow(gc);

    self.texture_pool.deinitNow(gc);
}

pub fn newTexture(self: *TextureRegistry) !u32 {
    if (self.textures_free_list.pop()) |id| return id;

    _ = try self.textures.append(self.alloc, .{});
    return @intCast(self.textures.len - 1);
}

pub fn newSampledTexture(self: *TextureRegistry, gc: *const base.GraphicsCtx, destroy_queue: *base.DestroyQueue) !u32 {
    if (self.sampled_texture_free_list.pop()) |id| return id;

    _ = try self.sampled_textures.append(self.alloc, .{});
    const id: u32 = @intCast(self.sampled_textures.len - 1);

    if (id >= self.texture_pool_capacity) try self.resizeTexturePool(gc, destroy_queue, @intFromFloat(@as(f64, @floatFromInt(self.texture_pool_capacity)) * 1.5));

    self.texture_pool.update(gc, id, self.default_images[0].view.handle, .shader_read_only_optimal, self.default_sampler.handle); // provisional; the role default is set at acquire

    return id;
}

pub fn acquireTexture(self: *TextureRegistry, gc: *const base.GraphicsCtx, transport: *base.Transport, io: std.Io, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, id: u32, delta: u32) !void {
    std.debug.assert(delta > 0);
    _ = resolved;

    const slice = self.textures.slice();
    const ref_count = &slice.items(.ref_count)[id];
    if (ref_count.* == 0) {
        const data = try loader.load(io, cold);
        errdefer loader.unload(io, cold);

        var reader = std.Io.Reader.fixed(data);
        const blob = try reader.takeStruct(TextureBlobHeader, .little);
        const ticket = &slice.items(.ticket)[id];
        const image = &slice.items(.image)[id];

        slice.items(.default_image)[id] = blob.default_image;

        image.image = try base.Image2D.init(gc, gc.vma_alloc, "TextureRegistry image", .{
            .format = blob.format,
            .extent = .{
                .width = blob.width,
                .height = blob.height,
            },
            .usage = .{ .transfer_dst_bit = true, .sampled_bit = true },
        });
        errdefer image.image.deinitNow(gc);

        image.view = try base.ImageView.init(gc, .fromImage(&image.image));
        errdefer image.view.deinitNow(gc);

        var free_fn = try loader.make_transport_unload(io, self.alloc, cold);
        errdefer free_fn.deinit();

        ticket.* = try transport.uploadImage(
            false,
            blob.format,
            .{ .width = blob.width, .height = blob.height, .depth = 1 },
            data[@sizeOf(TextureBlobHeader)..],
            free_fn.free_fn,
            free_fn.ctx,
            image.image.handle,
            .{
                .aspect_mask = .{ .color_bit = true },
                .mip_level = 0,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .{ .x = 0, .y = 0, .z = 0},
            .undefined,
            .shader_read_only_optimal,
            .{ .fragment_shader_bit = true, .compute_shader_bit = true },
            .{ .shader_read_bit = true }
        );
        errdefer transport.unqueue(ticket, false);
    }

    ref_count.* += delta;
}

pub fn acquireSampledTexture(self: *TextureRegistry, gc: *const base.GraphicsCtx, transport: *base.Transport, io: std.Io, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, id: u32, delta: u32) !void {
    std.debug.assert(delta > 0);
    const slice = self.sampled_textures.slice();
    const ref_count = &slice.items(.ref_count)[id];

    if (ref_count.* == 0) {
        const sampler = &slice.items(.sampler)[id];
        const dense = &slice.items(.texture)[id];

        const data = try loader.load(io, cold);

        // var reader = std.Io.Reader.fixed(data);
        // const blob = try reader.takeStruct(Blob, .little); - Can't use because takeStruct requires packed or extern on Blob
        const parsed_blob = try std.json.parseFromSlice(SampledTextureBlob, self.alloc, data, .{ .duplicate_field_behavior = .@"error" });
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

pub fn releaseTexture(self: *TextureRegistry, destroy_queue: *base.DestroyQueue, transport: *base.Transport, id: u32, delta: u32) !types.ReleaseReturn {
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

pub fn releaseSampledTexture(self: *TextureRegistry, gc: *const base.GraphicsCtx, destroy_queue: *base.DestroyQueue, id: u32, delta: u32) !types.ReleaseReturn {
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

pub fn ingestTexture(io: std.Io, location: types.IngestLocation, tex: IngestTexture) types.IngestError!Entry {
    const file = try location.prefix_dir.createFile(io, location.path, .{});
    defer file.close(io);

    var writer_buffer: [1024]u8 = undefined;
    var file_writer = file.writer(io, &writer_buffer);
    const writer = &file_writer.interface;

    const blob_size = switch (tex.data) {
        .decoded_blob => |d| blk: {
            try writer.writeStruct(TextureBlobHeader{
                .format = d.format,
                .width = d.width,
                .height = d.height,
                .default_image = tex.default_image
            }, .little);

            // NOTE: potentially could replace with writer.write and manual loop that also calls io.checkCancel(), but don't think it's worth it
            try writer.writeAll(d.blob);

            break :blk d.blob.len;
        },
        .encoded_blob => unreachable,
        .file_path => unreachable,
        .uri => unreachable,
    };

    try file_writer.flush();

    const cold_asset: Loader.ColdAsset = .{
        .location = location.loc,
        .offset = 0,
        .size = @sizeOf(TextureBlobHeader) + blob_size,
    };

    return .{
        .generation = 0,
        .kind = .texture,
        .cold_asset = cold_asset,
    };
}

pub fn ingestSampledTexture(alloc: Allocator, io: std.Io, location: types.IngestLocation, tex: IngestSampledTexture) types.IngestError!Entry {
    const file = try location.prefix_dir.createFile(io, location.path, .{});
    defer file.close(io);

    var writer_buffer: [512]u8 = undefined;
    var file_writer = file.writer(io, &writer_buffer);
    var stringify: std.json.Stringify = .{
        .writer = &file_writer.interface,
        .options = .{
            .whitespace = .indent_2,
        }
    };

    try stringify.write(SampledTextureBlob{
        .sampler = tex.sampler,
        .texture_gid = tex.texture,
    });
    try file_writer.flush();

    var entry: Entry = .{
        .generation = 0,
        .kind = .sampled_texture,
        .cold_asset = .{
            .location = location.loc,
            .offset = 0,
            .size = file_writer.logicalPos(),
        },
    };
    try entry.deps.necessary.set(alloc, 0);
    (try entry.deps.gids.addOne(alloc)).* = tex.texture;

    return entry;
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
