const std = @import("std");
const base = @import("base");
const Mesh = @import("../Mesh.zig");
const types = @import("Types.zig");
const resolver = @import("resolver.zig");
const Loader = @import("Loader.zig");
const Gid = types.Gid;

const Allocator = std.mem.Allocator;
const GeometryRegistry = @This();

geometries: std.MultiArrayList(struct {
    ref_count: u32 = 0,
    tickets: [2]base.Transport.Ticket = .{ .none, .none },
    buffer: base.Buffer = .empty,
}) = .empty,
free: std.ArrayList(u32) = .empty,

pub const IngestGeometry = struct {
    indexed_tangents: bool,
    stride: u31,
    position_offset: u32,
    normal_offset: u32,
    tangent_offset: u32,
    color0_offset: u32,
    texcoord0_offset: u32,
    texcoord1_offset: u32,
    texcoord2_offset: u32,
    texcoord3_offset: u32,
    joints0_offset: u32,
    weights0_offset: u32,
    data: []const u8,
};

const GeometryHeaderBlob = extern struct {
    stride: u32,
    position_offset: u32,
    normal_offset: u32,
    tangent_offset: u32,
    color0_offset: u32,
    texcoord0_offset: u32,
    texcoord1_offset: u32,
    texcoord2_offset: u32,
    texcoord3_offset: u32,
    joints0_offset: u32,
    weights0_offset: u32,

    pub const indexed_tangents_bit: u32 = 0x80000000;
    pub const stride_mask: u32 = 0x7FFFFFFF;

    pub fn indexedTangents(self: GeometryHeaderBlob) bool {
        return (self.stride & indexed_tangents_bit) != 0;
    }

    pub fn packTangentsBit(stride: u31, indexed_tangents: bool) u32 {
        var stride32: u32 = @intCast(stride);
        if (indexed_tangents) stride32 |= indexed_tangents_bit;
        return stride32;
    }
};

pub fn init() GeometryRegistry {
    return .{};
}

pub fn deinit(self: *GeometryRegistry, ctx: base.Ctx.Query(&.{ .destroy_queue }), alloc: Allocator) void {
    for (self.geometries.items(.buffer)) |*buf| {
        buf.deinit(.from(ctx));
    }
    self.geometries.deinit(alloc);

    self.free.deinit(alloc);
}

pub fn deinitNow(self: *GeometryRegistry, ctx: base.Ctx.Query(&.{ .vma_allocator }), alloc: Allocator) void {
    for (self.geometries.items(.buffer)) |*buf| {
        buf.deinitNow(.from(ctx));
    }
    self.geometries.deinit(alloc);

    self.free.deinit(alloc);
}

pub fn new(self: *GeometryRegistry, alloc: Allocator) !u32 {
    if (self.free.pop()) |free| return free;

    try self.geometries.append(alloc, .{});
    return @intCast(self.geometries.len - 1);
}

pub fn acquire(self: *GeometryRegistry, ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .graphics_family, .transport_family, .transport }), alloc: Allocator, io: std.Io, loader: *Loader, cold: *const Loader.ColdAsset, id: u32, delta: u32) !types.AcquireReturn {
    std.debug.assert(delta > 0);

    const slice = self.geometries.slice();
    const ref_count = &slice.items(.ref_count)[id];
    var ret: types.AcquireReturn = .incr;
    if (ref_count.* == 0) {
        const data = try loader.load(io, cold);
        errdefer loader.unload(io, cold);

        var free_fn = try loader.make_transport_unload(io, alloc, cold);
        errdefer free_fn.deinit();

        var reader = std.Io.Reader.fixed(data);
        const header = try reader.takeStruct(GeometryHeaderBlob, .little);
        const geo_data = data[@sizeOf(GeometryHeaderBlob)..];

        const buffer: *base.Buffer = &slice.items(.buffer)[id];
        const tickets: *[2]base.Transport.Ticket = &slice.items(.tickets)[id];

        buffer.* = try .init(.from(ctx), .graphics, "Geometry buffer", Mesh.gpu_geometry_header_size + geo_data.len, .{ .transfer_dst_bit = true, .storage_buffer_bit = true }, .gpu_only);
        errdefer buffer.deinitNow(.from(ctx));

        const gpu_header = try alloc.create(struct { Mesh.GPUGeometry, Allocator });
        errdefer alloc.destroy(gpu_header);
        gpu_header.@"0" = .{
            .indices_address = buffer.address + Mesh.gpu_geometry_header_size,
            .stride = header.stride,
            .position_offset = header.position_offset,
            .normal_offset = header.normal_offset,
            .tangent_offset = header.tangent_offset,
            .color0_offset = header.color0_offset,
            .texcoord0_offset = header.texcoord0_offset,
            .texcoord1_offset = header.texcoord1_offset,
            .texcoord2_offset = header.texcoord2_offset,
            .texcoord3_offset = header.texcoord3_offset,
            .joints0_offset = header.joints0_offset,
            .weights0_offset = header.weights0_offset,
        };
        gpu_header.@"1" = alloc;

        const transport: *base.Transport = ctx.view.transport;
        tickets[0] = try transport.uploadBuffer(false, std.mem.asBytes(&gpu_header.@"0")[0..Mesh.gpu_geometry_header_size], destroy_gpu_header, gpu_header, buffer.handle, 0, .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true }, .{ .memory_read_bit = true }, true);
        errdefer transport.unqueue(tickets[0], true);

        tickets[1] = try transport.uploadBuffer(false, geo_data, free_fn.free_fn, free_fn.ctx, buffer.handle, Mesh.gpu_geometry_header_size, .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true }, .{ .memory_read_bit = true }, true);
        errdefer transport.unqueue(tickets[1], false);
        ret = .load;
    }

    ref_count.* += delta;
    return ret;
}

fn destroy_gpu_header(ctx: ?*anyopaque, ptr: *anyopaque) void {
    const c: *struct { Mesh.GPUGeometry, Allocator } = @ptrCast(@alignCast(ctx.?));
    std.debug.assert(@intFromPtr(ptr) == @intFromPtr(c));

    const alloc = c.@"1";
    alloc.destroy(c);
}

pub fn release(self: *GeometryRegistry, ctx: base.Ctx.Query(&.{ .transport, .destroy_queue }), alloc: Allocator, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);
    const slice =  self.geometries.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* >= delta);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

    const tickets = &slice.items(.tickets)[id];
    ctx.view.transport.unqueue(tickets[0], true);
    ctx.view.transport.unqueue(tickets[1], true);
    tickets.* = .{ .none, .none };

    var buffer = &slice.items(.buffer)[id];
    buffer.deinit(.from(ctx));
    buffer.* = .empty;

    try self.free.append(alloc, id);
    return .released;
}

pub fn ingest(io: std.Io, location: types.IngestLocation, geo: IngestGeometry) types.IngestError!types.Entry {
    const file = try location.prefix_dir.createFile(io, location.path, .{});
    defer file.close(io);

    var writer_buffer: [1024]u8 = undefined;
    var file_writer = file.writer(io, &writer_buffer);
    const writer = &file_writer.interface;

    try writer.writeStruct(GeometryHeaderBlob{
        .stride = GeometryHeaderBlob.packTangentsBit(geo.stride, geo.indexed_tangents),
        .position_offset = geo.position_offset,
        .normal_offset = geo.normal_offset,
        .tangent_offset = geo.tangent_offset,
        .color0_offset = geo.color0_offset,
        .texcoord0_offset = geo.texcoord0_offset,
        .texcoord1_offset = geo.texcoord1_offset,
        .texcoord2_offset = geo.texcoord2_offset,
        .texcoord3_offset = geo.texcoord3_offset,
        .joints0_offset = geo.joints0_offset,
        .weights0_offset = geo.weights0_offset,
    }, .little);
    try writer.writeAll(geo.data);
    try file_writer.flush();

    return .{
        .generation = 0,
        .kind = .geometry,
        .cold_asset = .{
            .location = location.loc,
            .offset = 0,
            .size = file_writer.logicalPos(),
        },
    };
}

pub inline fn isReady(self: *GeometryRegistry, ctx: base.Ctx.Query(&.{ .transport }), id: u32) !bool {
    const slice = self.geometries.slice();
    const tickets = slice.items(.tickets)[id];

    return try ctx.view.transport.isReady(tickets[0]) and try ctx.view.transport.isReady(tickets[1]);
}

pub inline fn getAddress(self: *GeometryRegistry, id: u32) u64 {
    return self.geometries.items(.buffer)[id].address;
}

test {
    std.testing.refAllDecls(@This());
}
