const std = @import("std");
const base = @import("base");

const Allocator = std.mem.Allocator;

const Mesh = @This();

pub const GPUMeta = struct {
    mesh_desc_ix: u32,
    lod_offset: u32,
};

source: []const u8,
gpu_meta: ?GPUMeta, // null => mesh not loaded on the GPU in any way
lods: []Lod,
aabb: base.util.AABB,

pub fn init(alloc: Allocator, source: []const u8) !Mesh {
    var reader = std.Io.Reader.fixed(source);

    const hdr = try reader.takeStruct(Header, .little);
    if (!std.mem.eql(u8, &hdr.magic, "GMSH")) return error.InvalidMesh;
    if (hdr.version != 0) return error.UnsupportedVersion;

    const lods = try alloc.alloc(Lod, hdr.lod_count);
    errdefer alloc.free(lods);

    for (lods) |*lod| {
        const entry = try reader.takeStruct(LODEntry, .little);
        const geo_off = @as(usize, @intCast(entry.geometry_offset));
        if (geo_off + @sizeOf(GeometryMeta) > source.len) return error.InvalidMesh;

        var geo_reader = std.Io.Reader.fixed(source[geo_off..]);
        const meta = try geo_reader.takeStruct(GeometryMeta, .little);

        const vtx_start = geo_off + @sizeOf(GeometryMeta);
        const vtx_end = vtx_start + @as(usize, @intCast(meta.vertex_size));
        if (vtx_end > source.len) return error.InvalidMesh;

        lod.* = .{
            .geometry = .{
                .data = source[vtx_start..vtx_end],
                .geo = .{
                    .stride = meta.stride,
                    .position_offset = meta.position_offset,
                    .normal_offset = meta.normal_offset,
                    .tangent_offset = meta.tangent_offset,
                    .color0_offset = meta.color0_offset,
                    .texcoord0_offset = meta.texcoord0_offset,
                    .texcoord1_offset = meta.texcoord1_offset,
                    .texcoord2_offset = meta.texcoord2_offset,
                    .texcoord3_offset = meta.texcoord3_offset,
                },
            },
            .geometry_buffer = null,
            .on_gpu = false,
            .draw_count = entry.draw_count,
            .material_schema = entry.material_schema,
            .material_instance = entry.material_schema,
            .error_metric = entry.error_metric,
        };
    }

    return .{
        .source = source,
        .gpu_meta = null,
        .lods = lods,
        .aabb = .{
            .min = .{ hdr.aabb_min_x, hdr.aabb_min_y, hdr.aabb_min_z, 1 },
            .max = .{ hdr.aabb_max_x, hdr.aabb_max_y, hdr.aabb_max_z, 1 },
        },
    };
}

pub fn writeHeader(self: *const Mesh, w: *std.Io.Writer, geometry_offsets: []const isize) !void {
    try w.writeStruct(Header{
        .magic = .{ 'G', 'M', 'S', 'H' },
        .version = 0,
        .lod_count = @intCast(self.lods.len),
        .aabb_min_x = self.aabb.min[0],
        .aabb_min_y = self.aabb.min[1],
        .aabb_min_z = self.aabb.min[2],
        .aabb_max_x = self.aabb.max[0],
        .aabb_max_y = self.aabb.max[1],
        .aabb_max_z = self.aabb.max[2],
    }, .little);

    for (self.lods, geometry_offsets) |*lod, offset| {
        try w.writeStruct(LODEntry{
            .draw_count = lod.draw_count,
            .error_metric = lod.error_metric,
            .material_schema = lod.material_schema,
            .material_instancey = lod.material_instance,
            .geometry_offset = offset,
        }, .little);
    }
}

pub fn deinit(self: *const Mesh, alloc: Allocator) void {
    alloc.free(self.lods);
}

pub const Lod = struct {
    geometry: Geometry,
    geometry_buffer: ?struct {base.Buffer, base.Transport.Ticket},
    on_gpu: bool,

    draw_count: u32,
    material_schema: u32,
    material_instance: u32,
    error_metric: f32,

    pub fn initGeometryBuffer(self: *Lod, gc: *const base.GraphicsCtx, transport: *base.Transport) !struct{base.Buffer, base.Transport.Ticket} {
        const buf = try base.Buffer.init(gc, .graphics, "Geometry buffer", @sizeOf(GPUGeometry) + self.geometry.data.len, .{.transfer_dst_bit = true, .storage_buffer_bit = true}, .gpu_only);

        const dst_stage: base.vk.PipelineStageFlags2 = .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true };
        const dst_access: base.vk.AccessFlags2 = .{ .memory_read_bit = true, };
        const tick1 = try transport.uploadBuffer(false, std.mem.asBytes(&self.geometry.geo), null, buf.handle, 0, dst_stage, dst_access);
        errdefer transport.unqueue(tick1, false);

        const tick2 = try transport.uploadBuffer(false, self.geometry.data, null, buf.handle, @sizeOf(GPUGeometry), dst_stage, dst_access);
        errdefer transport.unqueue(tick2, false);

        self.geometry_buffer = .{buf, tick2};

        return .{buf, tick2};
    }

    /// Returns the total bytes written (GeometryMeta + data).
    pub fn writeGeometry(self: *const Lod, w: *std.Io.Writer) !isize {
        try w.writeStruct(GeometryMeta{
            .vertex_size = @intCast(self.geometry.data.len),
            .stride = self.geometry.stride,
            .position_offset = self.geometry.position_offset,
            .normal_offset = self.geometry.normal_offset,
            .tangent_offset = self.geometry.tangent_offset,
            .color0_offset = self.geometry.color0_offset,
            .texcoord0_offset = self.geometry.texcoord0_offset,
            .texcoord1_offset = self.geometry.texcoord1_offset,
            .texcoord2_offset = self.geometry.texcoord2_offset,
            .texcoord3_offset = self.geometry.texcoord3_offset,
        }, .little);
        try w.writeAll(self.geometry.data);

        return @sizeOf(GeometryMeta) + @as(isize, @intCast(self.geometry.data.len));
    }

    pub fn deinitGeometryBuffer(self: *Lod, destroy_queue: *base.DestroyQueue, transport: *base.Transport) void {
        if (self.geometry_buffer) |*buf| {
            transport.unqueue(buf.@"1", false);
            buf.@"0".deinit(destroy_queue);

            self.geometry_buffer = null;
        }
    }
};

pub const Geometry = struct {
    data: []const u8,
    geo: GPUGeometry,

    pub const indexed_tangents_bit: u32 = 0x80000000;
    pub const stride_mask: u32 = 0x7FFFFFFF;

    pub fn indexedTangents(self: Geometry) bool {
        return (self.stride & indexed_tangents_bit) != 0;
    }
};

pub const GPUGeometry = extern struct {
    stride: u32,
    position_offset: u32,
    normal_offset: u32,
    tangent_offset: u32,
    color0_offset: u32,
    texcoord0_offset: u32,
    texcoord1_offset: u32,
    texcoord2_offset: u32,
    texcoord3_offset: u32,
};

pub const GPUMeshDesc = struct {
    lod_offset: u32,
    lod_count: u32,
    current_lod: GPULODEntry,
    aabb_min_x: f32, aabb_min_y: f32, aabb_min_z: f32,
    aabb_max_x: f32, aabb_max_y: f32, aabb_max_z: f32,

    pub fn fromMesh(mesh: *const Mesh) GPUMeshDesc {
        const current_lod: GPULODEntry = if (mesh.lods.len == 1) .fromLod(&mesh.lods[0]) else .{};
        return .{
            .lod_offset = if (mesh.gpu_meta) |gpu_meta| gpu_meta.lod_offset else 0,
            .lod_count = @intCast(mesh.lods.len),
            .current_lod = current_lod,
            .aabb_max_x = mesh.aabb.max[0],
            .aabb_max_y = mesh.aabb.max[1],
            .aabb_max_z = mesh.aabb.max[2],
            .aabb_min_x = mesh.aabb.min[0],
            .aabb_min_y = mesh.aabb.min[1],
            .aabb_min_z = mesh.aabb.min[2],
        };
    }
};

pub const GPULODEntry = struct {
    buffer_address: u64 = 0,
    draw_count: u32 = 0,
    material_schema: u32 = 0,
    material_instance: u32 = 0,
    error_metric: f32 = 0,

    pub fn fromLod(lod: *const Lod) GPULODEntry {
        return .{
            .buffer_address = if (lod.geometry_buffer) |buf| buf.@"0".address else 0,
            .draw_count = lod.draw_count,
            .material_schema = lod.material_schema,
            .material_instance = lod.material_instance,
            .error_metric = lod.error_metric,
        };
    }
};

pub const Header = extern struct {
    magic: [4]u8,
    version: u32,
    lod_count: u32,
    aabb_min_x: f32,
    aabb_min_y: f32,
    aabb_min_z: f32,
    aabb_max_x: f32,
    aabb_max_y: f32,
    aabb_max_z: f32,
};

pub const LODEntry = extern struct {
    draw_count: u32,
    material_schema: u32,
    material_instance: u32,
    error_metric: f32,
    geometry_offset: isize,
};

pub const GeometryMeta = extern struct {
    vertex_size: u64,
    stride: u32,
    position_offset: u32,
    normal_offset: u32,
    tangent_offset: u32,
    color0_offset: u32,
    texcoord0_offset: u32,
    texcoord1_offset: u32,
    texcoord2_offset: u32,
    texcoord3_offset: u32,
};
