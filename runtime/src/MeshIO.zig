const std = @import("std");
const base = @import("base");
const zmesh = @import("zmesh");

const Allocator = std.mem.Allocator;
const Mesh = @import("Mesh.zig");

const MeshIO = @This();

/// Converts a zmesh.Shape (indexed, with positions/normals/texcoords) into
/// our GMSH binary format and returns a Mesh + its backing allocation.
/// The caller owns both; free the source (alloc.free) before deinit-ing Mesh.
pub fn fromShape(alloc: Allocator, shape: zmesh.Shape) !struct { Mesh, []u8 } {
    const has_normals = shape.normals != null;
    const has_texcoords = shape.texcoords != null;
    const vertex_count: u32 = @intCast(shape.indices.len);
    const index_count: u32 = @intCast(shape.positions.len);

    var stride: u32 = 3;
    if (has_normals) stride += 3;
    if (has_texcoords) stride += 2;

    const pos_off: u32 = vertex_count;
    const norm_off: u32 = if (has_normals) pos_off + 3 else 0xFFFFFFFF;
    const tc0_off: u32 = blk: {
        if (!has_texcoords) break :blk 0xFFFFFFFF;
        var off: u32 = pos_off + 3;
        if (has_normals) off += 3;
        break :blk off;
    };

    var geo = std.ArrayList(u8).empty;
    defer geo.deinit(alloc);

    // Indices (always u32 on GPU)
    const IndexType = @TypeOf(shape.indices[0]);
    switch (IndexType) {
        u32 => try geo.appendSlice(alloc, std.mem.sliceAsBytes(shape.indices)),
        u16 => {
            try geo.ensureUnusedCapacity(alloc, shape.indices.len * @sizeOf(u32));
            for (shape.indices) |idx| geo.appendAssumeCapacity(@as(u32, idx));
        },
        else => @compileError("MeshIO: unsupported index type"),
    }

    // Interleaved vertex data
    try geo.ensureUnusedCapacity(alloc, index_count * stride * @sizeOf(f32));
    for (shape.positions, 0..) |pos, i| {
        geo.appendSliceAssumeCapacity(std.mem.asBytes(&pos));
        if (has_normals) geo.appendSliceAssumeCapacity(std.mem.asBytes(&shape.normals.?[i]));
        if (has_texcoords) geo.appendSliceAssumeCapacity(std.mem.asBytes(&shape.texcoords.?[i]));
    }

    // AABB
    var aabb_min = [_]f32{ std.math.floatMax(f32), std.math.floatMax(f32), std.math.floatMax(f32) };
    var aabb_max = [_]f32{ -std.math.floatMax(f32), -std.math.floatMax(f32), -std.math.floatMax(f32) };
    for (shape.positions) |pos| {
        aabb_min[0] = @min(aabb_min[0], pos[0]);
        aabb_min[1] = @min(aabb_min[1], pos[1]);
        aabb_min[2] = @min(aabb_min[2], pos[2]);
        aabb_max[0] = @max(aabb_max[0], pos[0]);
        aabb_max[1] = @max(aabb_max[1], pos[1]);
        aabb_max[2] = @max(aabb_max[2], pos[2]);
    }

    // GMSH binary: [Header][LODEntry][GeometryMeta][geo.data]
    const hdr_size = @as(usize, @sizeOf(Mesh.Header));
    const lod_size = @as(usize, @sizeOf(Mesh.LODEntry));
    const meta_size = @as(usize, @sizeOf(Mesh.GeometryMeta));

    var buf = std.ArrayList(u8).empty;
    defer buf.deinit(alloc);
    try buf.ensureTotalCapacity(alloc, hdr_size + lod_size + meta_size + geo.items.len);

    // Header
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&[_]u8{ 'G', 'M', 'S', 'H' }));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u32, 0)));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u32, 1)));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&aabb_min));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&aabb_max));

    // LODEntry
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&vertex_count));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(f32, 0)));
    const geometry_offset: isize = @intCast(hdr_size + lod_size);
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&geometry_offset));

    // GeometryMeta
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u64, @intCast(geo.items.len))));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&stride));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&pos_off));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&norm_off));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u32, 0xFFFFFFFF))); // tangent
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&tc0_off));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u32, 0xFFFFFFFF))); // tc1
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u32, 0xFFFFFFFF))); // tc2
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u32, 0xFFFFFFFF))); // tc3

    // Vertex data (indices + interleaved vertices)
    buf.appendSliceAssumeCapacity(geo.items);

    const source = try buf.toOwnedSlice(alloc);
    errdefer alloc.free(source);

    const mesh = try Mesh.init(alloc, source);
    return .{ mesh, source };
}
