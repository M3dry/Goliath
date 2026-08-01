const std = @import("std");
const base = @import("base");
const zmesh = @import("zmesh");

const Allocator = std.mem.Allocator;
const zcgltf = zmesh.io.zcgltf;

const Texture = @This();

/// KHR_texture_transform UV remap, applied in the shader.
pub const UVTransform = struct {
    offset: [2]f32,
    rotation: f32,
    scale: [2]f32,
    texcoord_override: ?u32,
};

/// A material's reference to a texture: which texture, which UV set, and
/// per-slot shading multipliers (normal/occlusion strength).
pub const TextureView = struct {
    texture_index: ?u32, // null => no texture bound
    texcoord: u32,
    scale: f32, // normalTexture.scale / occlusionTexture.scale, ignored elsewhere
    transform: ?UVTransform,
};

width: u32,
height: u32,
pixels: []u8, // decoded RGBA8, owned
sampler: base.Sampler.Description,

pub fn deinit(self: *const Texture, alloc: Allocator) void {
    alloc.free(self.pixels);
}

pub fn fromGltf(
    alloc: Allocator,
    io: std.Io,
    data: *zcgltf.Data,
    texture_index: u32,
    base_path: ?[]const u8,
) !?Texture {
    const textures = data.textures orelse return null;
    if (texture_index >= data.textures_count) return null;
    const gltf_texture = &textures[texture_index];

    const image = gltf_texture.image orelse {
        std.log.warn("texture {d}: no fallback image (basisu/webp only, unsupported)", .{texture_index});
        return null;
    };

    var image_data = try loadImage(alloc, io, image, base_path);
    errdefer image_data.deinit(alloc);

    return .{
        .width = image_data.width,
        .height = image_data.height,
        .pixels = image_data.pixels,
        .sampler = samplerFromGltf(gltf_texture.sampler),
    };
}

/// Converts an inline material texture view (pointer + transform) into an
/// indexed runtime view. The asset loader picks the target texture index.
pub fn parseTextureView(data: *const zcgltf.Data, view: zcgltf.TextureView) TextureView {
    var result = TextureView{
        .texture_index = null,
        .texcoord = @intCast(view.texcoord),
        .scale = view.scale,
        .transform = null,
    };

    if (view.texture) |texture| {
        if (data.textures) |textures| {
            result.texture_index = @intCast(
                (@intFromPtr(texture) - @intFromPtr(textures)) / @sizeOf(zcgltf.Texture),
            );
        }
    }

    if (view.has_transform != 0) {
        result.transform = .{
            .offset = view.transform.offset,
            .rotation = view.transform.rotation,
            .scale = view.transform.scale,
            .texcoord_override = if (view.transform.has_texcoord != 0) @intCast(view.transform.texcoord) else null,
        };
    }

    return result;
}

fn loadImage(alloc: Allocator, io: std.Io, image: *const zcgltf.Image, base_path: ?[]const u8) !base.image_loader.ImageData {
    if (image.buffer_view) |bv| {
        const ptr = bv.getData() orelse return error.ImageDataUnavailable;
        if (bv.size == 0) return error.ImageDataUnavailable;

        return base.image_loader.loadFromMemory(alloc, ptr[0..bv.size]);
    }

    const uri = std.mem.span(image.uri orelse return error.ImageDataUnavailable);

    if (std.mem.startsWith(u8, uri, "data:")) {
        const comma = std.mem.indexOfScalar(u8, uri, ',') orelse return error.InvalidDataUri;
        const decoder = std.base64.standard.Decoder;
        const payload = try alloc.alloc(u8, try decoder.calcSizeForSlice(uri[comma + 1 ..]));
        defer alloc.free(payload);
        try decoder.decode(payload, uri[comma + 1 ..]);

        return base.image_loader.loadFromMemory(alloc, payload);
    }

    const path = if (base_path) |bp|
        try std.fs.path.join(alloc, &.{ bp, uri })
    else
        try alloc.dupe(u8, uri);
    defer alloc.free(path);

    return base.image_loader.loadFromFile(alloc, io, path);
}

fn samplerFromGltf(sampler: ?*const zcgltf.Sampler) base.Sampler.Description {
    var desc = base.Sampler.Description{};
    const s = sampler orelse return desc;

    desc.mag_filter = switch (s.mag_filter) {
        .nearest => .nearest,
        else => .linear, // undefined/linear
    };

    switch (s.min_filter) {
        .nearest => desc.min_filter = .nearest,
        .linear => desc.min_filter = .linear,
        .nearest_mipmap_nearest => {
            desc.min_filter = .nearest;
            desc.mipmap_mode = .nearest;
            desc.max_lod = 0.25;
        },
        .nearest_mipmap_linear => {
            desc.min_filter = .nearest;
            desc.mipmap_mode = .linear;
            desc.max_lod = 0.25;
        },
        .linear_mipmap_nearest => {
            desc.min_filter = .linear;
            desc.mipmap_mode = .nearest;
            desc.max_lod = 0.25;
        },
        .linear_mipmap_linear => {
            desc.min_filter = .linear;
            desc.mipmap_mode = .linear;
            desc.max_lod = 0.25;
        },
        .undefined => {},
    }

    desc.address_mode_u = switch (s.wrap_s) {
        .clamp_to_edge => .clamp_to_edge,
        .mirrored_repeat => .mirrored_repeat,
        .repeat => .repeat,
    };
    desc.address_mode_v = switch (s.wrap_t) {
        .clamp_to_edge => .clamp_to_edge,
        .mirrored_repeat => .mirrored_repeat,
        .repeat => .repeat,
    };

    // ponytail: no mip chain yet, mipmap min_filters clamp to base level via max_lod.
    // Generate mips in Transport when shimmer matters; drop max_lod then.
    return desc;
}
