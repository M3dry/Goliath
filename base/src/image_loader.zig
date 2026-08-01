const std = @import("std");
const zigimg = @import("zigimg");
const Allocator = std.mem.Allocator;

pub const ImageData = struct {
    pixels: []u8,
    width: u32,
    height: u32,
    format: vk.Format = .r8g8b8a8_srgb,

    const vk = @import("vulkan");

    pub fn deinit(self: *ImageData, allocator: Allocator) void {
        allocator.free(self.pixels);
    }
};

pub fn loadFromFile(allocator: Allocator, io: std.Io, path: []const u8) !ImageData {
    var read_buffer: [4096]u8 = undefined;
    var img = try zigimg.Image.fromFilePath(allocator, io, path, &read_buffer);
    defer img.deinit(allocator);

    return loadFromImage(allocator, &img);
}

pub fn loadFromMemory(allocator: Allocator, data: []const u8) !ImageData {
    var img = try zigimg.Image.fromMemory(allocator, data);
    defer img.deinit(allocator);

    return loadFromImage(allocator, &img);
}

fn loadFromImage(allocator: Allocator, img: *zigimg.Image) !ImageData {
    try img.convert(allocator, .rgba32);

    const raw = img.rawBytes();
    const pixels = try allocator.dupe(u8, raw);

    return .{
        .pixels = pixels,
        .width = @intCast(img.width),
        .height = @intCast(img.height),
    };
}
