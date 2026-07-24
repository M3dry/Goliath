const std = @import("std");
const base = @import("base");
const zmesh = @import("zmesh");

const Allocator = std.mem.Allocator;
const Mesh = @import("Mesh.zig");
const zcgltf = zmesh.io.zcgltf;

const MeshIO = @This();

source: []u8,

pub const GltfPrimitiveResult = struct {
    mesh_io: MeshIO,
    name: []u8,
    material_index: i32,
};

/// Converts a zmesh.Shape (indexed, with positions/normals/texcoords) into our GMSH binary format.
pub fn fromShape(alloc: Allocator, shape: zmesh.Shape) !MeshIO {
    const has_normals = shape.normals != null;
    const has_texcoords = shape.texcoords != null;
    const vertex_count: u32 = @intCast(shape.indices.len);
    const index_count: u32 = @intCast(shape.positions.len);
    const aabb = shape.computeAabb();

    var stride: u32 = 3;
    if (has_normals) stride += 3;
    if (has_texcoords) stride += 2;

    const pos_off: u32 = vertex_count;
    const norm_off: u32 = if (has_normals) pos_off + 3 else 0xFFFFFFFF;
    const tc0_off: u32 = blk: {
        if (!has_texcoords) break :blk 0xFFFFFFFF;
        break :blk pos_off + 3 + (if (has_normals) @as(u32, 3) else 0);
    };

    var geo = std.ArrayList(u8).empty;
    defer geo.deinit(alloc);

    const IndexType = @TypeOf(shape.indices[0]);
    switch (IndexType) {
        u32 => try geo.appendSlice(alloc, std.mem.sliceAsBytes(shape.indices)),
        u16 => {
            try geo.ensureUnusedCapacity(alloc, shape.indices.len * @sizeOf(u32));
            for (shape.indices) |idx| geo.appendAssumeCapacity(@as(u32, idx));
        },
        else => @compileError("MeshIO: unsupported index type"),
    }

    try geo.ensureUnusedCapacity(alloc, index_count * stride * @sizeOf(f32));
    for (shape.positions, 0..) |pos, i| {
        geo.appendSliceAssumeCapacity(std.mem.asBytes(&pos));
        if (has_normals) geo.appendSliceAssumeCapacity(std.mem.asBytes(&shape.normals.?[i]));
        if (has_texcoords) geo.appendSliceAssumeCapacity(std.mem.asBytes(&shape.texcoords.?[i]));
    }

    return packGeometry(alloc, vertex_count, stride, pos_off, norm_off, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
        .{ tc0_off, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF },
        .{ aabb[0], aabb[1], aabb[2] }, .{ aabb[3], aabb[4], aabb[5] }, geo.items);
}

/// Converts a single glTF primitive to a MeshIO. The caller must keep `data`
/// alive (from parseAndLoadFile) for the duration of this call.
pub fn fromGltfPrimitive(
    alloc: Allocator,
    data: *zcgltf.Data,
    mesh_index: u32,
    prim_index: u32,
) !GltfPrimitiveResult {
    const mesh = &data.meshes.?[mesh_index];
    const prim = &mesh.primitives[prim_index];
    if (prim.type != .triangles) return error.UnsupportedPrimitiveType;

    const vertex_count: u32 = @intCast(prim.attributes[0].data.count);
    const has_indices = prim.indices != null;
    const draw_count: u32 = if (has_indices) @intCast(prim.indices.?.count) else vertex_count;

    var has_normals = false;
    var has_tangents = false;
    var has_colors = false;
    var has_joints = false;
    var has_weights = false;
    var has_texcoords = [_]bool{false} ** 4;

    for (prim.attributes[0..prim.attributes_count]) |attrib| {
        switch (attrib.type) {
            .normal => has_normals = true,
            .tangent => has_tangents = true,
            .texcoord => {
                if (attrib.index >= 0 and attrib.index < 4) has_texcoords[@intCast(attrib.index)] = true;
            },
            .color => {
                if (attrib.index == 0) has_colors = true;
            },
            .joints => {
                if (attrib.index == 0) has_joints = true;
            },
            .weights => {
                if (attrib.index == 0) has_weights = true;
            },
            else => {},
        }
    }

    var stride: u32 = 3;
    if (has_normals) stride += 3;
    if (has_tangents) stride += 4;
    if (has_colors) stride += 4;
    for (has_texcoords) |tc| {
        if (tc) stride += 2;
    }
    if (has_joints) stride += 2;
    if (has_weights) stride += 4;

    const pos_off: u32 = if (has_indices) draw_count else 0;
    var off = pos_off + 3;
    const norm_off: u32 = if (has_normals) off else 0xFFFFFFFF;
    if (has_normals) off += 3;
    const tang_off: u32 = if (has_tangents) off else 0xFFFFFFFF;
    if (has_tangents) off += 4;
    const col_off: u32 = if (has_colors) off else 0xFFFFFFFF;
    if (has_colors) off += 4;
    var tc_off = [_]u32{0xFFFFFFFF} ** 4;
    for (0..4) |i| {
        if (has_texcoords[i]) {
            tc_off[i] = off;
            off += 2;
        }
    }

    const joints0_off: u32 = if (has_joints) off else 0xFFFFFFFF;
    if (has_joints) off += 2;
    const weights0_off: u32 = if (has_weights) off else 0xFFFFFFFF;
    if (has_weights) off += 4;

    const index_bytes: usize = if (has_indices) draw_count * @sizeOf(u32) else 0;
    const vertex_bytes: usize = vertex_count * stride * @sizeOf(u32);

    var geo = std.ArrayList(u8).empty;
    defer geo.deinit(alloc);
    try geo.ensureTotalCapacity(alloc, index_bytes + vertex_bytes);

    if (has_indices) {
        const accessor = prim.indices.?;
        const count = accessor.unpackIndicesCount();
        const idx_buf = try alloc.alloc(u32, count);
        defer alloc.free(idx_buf);
        const indices = accessor.unpackIndices(idx_buf);

        geo.appendSliceAssumeCapacity(std.mem.sliceAsBytes(indices));
    }

    geo.items.len = index_bytes + vertex_bytes;
    const vertex_slice = std.mem.bytesAsSlice(u32, geo.items[index_bytes..]);

    for (prim.attributes[0..prim.attributes_count]) |attrib| {
        const accessor = attrib.data;
        const num_comp = accessor.type.numComponents();
        const attr_off: u32 = switch (attrib.type) {
            .position => pos_off,
            .normal => norm_off,
            .tangent => tang_off,
            .color => if (attrib.index == 0) col_off else 0xFFFFFFFF,
            .texcoord => if (attrib.index >= 0 and attrib.index < 4) tc_off[@intCast(attrib.index)] else 0xFFFFFFFF,
            .weights => if (attrib.index == 0) weights0_off else 0xFFFFFFFF,
            else => 0xFFFFFFFF,
        };
        if (attr_off == 0xFFFFFFFF) continue;

        const tmp = try alloc.alloc(f32, vertex_count * num_comp);
        defer alloc.free(tmp);
        _ = accessor.unpackFloats(tmp);

        const intra_off = attr_off - pos_off;
        for (0..vertex_count) |vi| {
            const byte_base = vi * stride + intra_off;
            for (0..num_comp) |c| {
                vertex_slice[byte_base + c] = @bitCast(tmp[vi * num_comp + c]);
            }
            if (attrib.type == .color) {
                for (num_comp..4) |c| vertex_slice[byte_base + c] = @bitCast(@as(f32, 1.0));
            }
        }
    }

    var aabb_min: [3]f32 = .{ std.math.floatMax(f32), std.math.floatMax(f32), std.math.floatMax(f32) };
    var aabb_max: [3]f32 = .{ -std.math.floatMax(f32), -std.math.floatMax(f32), -std.math.floatMax(f32) };
    if (has_joints) {
        for (prim.attributes[0..prim.attributes_count]) |attrib| {
            if (attrib.type == .joints and attrib.index == 0) {
                const accessor = attrib.data;
                const intra_off = joints0_off - pos_off;
                for (0..vertex_count) |vi| {
                    var joint_vals: [4]u32 = undefined;
                    _ = accessor.readUint(vi, &joint_vals);
                    const vbase = vi * stride + intra_off;
                    vertex_slice[vbase + 0] = @as(u32, @intCast(joint_vals[0])) | (@as(u32, @intCast(joint_vals[1])) << 16);
                    vertex_slice[vbase + 1] = @as(u32, @intCast(joint_vals[2])) | (@as(u32, @intCast(joint_vals[3])) << 16);
                }
                break;
            }
        }
    }

    for (prim.attributes[0..prim.attributes_count]) |attrib| {
        if (attrib.type == .position) {            const pos = attrib.data;
            if (pos.has_min != 0) aabb_min = .{ pos.min[0], pos.min[1], pos.min[2] };
            if (pos.has_max != 0) aabb_max = .{ pos.max[0], pos.max[1], pos.max[2] };
            break;
        }
    }

    const mesh_name = if (mesh.name) |n| std.mem.sliceTo(n, 0) else "mesh";
    const name = try std.fmt.allocPrint(alloc, "{s}/{}", .{ mesh_name, prim_index });

    const material_index: i32 = if (prim.material) |mat| blk: {
        const base_ptr = @intFromPtr(data.materials.?);
        const mat_ptr = @intFromPtr(mat);
        break :blk @as(i32, @intCast((mat_ptr - base_ptr) / @sizeOf(zcgltf.Material)));
    } else -1;

    const mesh_io = try packGeometry(alloc, draw_count, stride, pos_off, norm_off, tang_off, col_off,
        joints0_off, weights0_off, tc_off, aabb_min, aabb_max, geo.items);
    return .{ .mesh_io = mesh_io, .name = name, .material_index = material_index };
}

pub fn deinit(self: *const MeshIO, alloc: Allocator) void {
    alloc.free(self.source);
}

fn packGeometry(
    alloc: Allocator,
    vertex_count: u32,
    stride: u32,
    position_offset: u32,
    normal_offset: u32,
    tangent_offset: u32,
    color0_offset: u32,
    joints0_offset: u32,
    weights0_offset: u32,
    texcoord_offsets: [4]u32,
    aabb_min: [3]f32,
    aabb_max: [3]f32,
    geo_data: []const u8,
) !MeshIO {
    const hdr_size = @as(usize, @sizeOf(Mesh.Header));
    const lod_size = @as(usize, @sizeOf(Mesh.LODEntry));
    const meta_size = @as(usize, @sizeOf(Mesh.GeometryMeta));

    var buf = std.ArrayList(u8).empty;
    defer buf.deinit(alloc);
    try buf.ensureTotalCapacity(alloc, hdr_size + lod_size + meta_size + geo_data.len);

    buf.appendSliceAssumeCapacity(std.mem.asBytes(&[_]u8{ 'G', 'M', 'S', 'H' }));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u32, 0)));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(u32, 1)));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&aabb_min));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&aabb_max));

    buf.appendSliceAssumeCapacity(std.mem.asBytes(&vertex_count));
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&[_]u32{0})); // material schema
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&[_]u32{0})); // material instance
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&@as(f32, 0)));
    const geometry_offset: isize = @intCast(hdr_size + lod_size);
    buf.appendSliceAssumeCapacity(std.mem.asBytes(&geometry_offset));

    buf.appendSliceAssumeCapacity(std.mem.asBytes(&Mesh.GeometryMeta{
        .vertex_size = @intCast(geo_data.len),
        .stride = stride,
        .position_offset = position_offset,
        .normal_offset = normal_offset,
        .tangent_offset = tangent_offset,
        .color0_offset = color0_offset,
        .texcoord0_offset = texcoord_offsets[0],
        .texcoord1_offset = texcoord_offsets[1],
        .texcoord2_offset = texcoord_offsets[2],
        .texcoord3_offset = texcoord_offsets[3],
        .joints0_offset = joints0_offset,
        .weights0_offset = weights0_offset,
    }));

    buf.appendSliceAssumeCapacity(geo_data);

    const source = try buf.toOwnedSlice(alloc);
    errdefer alloc.free(source);
    return .{ .source = source };
}
