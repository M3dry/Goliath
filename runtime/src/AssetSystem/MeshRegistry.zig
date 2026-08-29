const std = @import("std");
const base = @import("base");
const Mesh = @import("../Mesh.zig");
const types = @import("Types.zig");
const resolver = @import("resolver.zig");
const Loader = @import("Loader.zig");
const Gid = types.Gid;

const Allocator = std.mem.Allocator;
const GeometryRegistry = @import("GeometryRegistry.zig");
const MaterialRegistry = @import("MaterialRegistry.zig");
const MeshRegistry = @This();
const SmallBuffer = base.util.SmallBuffer;

const PatchKind = enum {
    geometry,
    material_instance,
    material_schema,
};

current_meshes: base.Buffer = .empty,
current_lods: base.Buffer = .empty,

staging_tickets: [2]base.Transport.Ticket = .{ .none, .none },
staging_meshes: base.Buffer = .empty,
staging_lods: base.Buffer = .empty,

upload_generation: u64 = 0,
stale: bool = false,
meshes: std.MultiArrayList(struct {
    ref_count: u32 = 0,
    desc: Mesh.GPUMeshDesc = undefined,
}) = .empty,
lods: std.MultiArrayList(struct {
    entries: Mesh.GPULODEntry,
    patch_lookup: std.EnumArray(PatchKind, Gid),
}) = .empty,

free: std.ArrayList(u32) = .empty,

pub const MeshDescBlob = struct {
    lods: []LodEntryBlob,
    aabb_min: [3]f32,
    aabb_max: [3]f32,
};

pub const LodEntryBlob = struct {
    geometry: Gid,
    vertex_count: u32,
    draw_count: u32,
    material_schema: Gid,
    material_instance: Gid,
    error_metric: f32,
};

pub fn init() MeshRegistry {
    return .{};
}

pub fn deinit(self: *MeshRegistry, ctx: base.Ctx.Query(&.{ .destroy_queue, .transport }), alloc: Allocator) void {
    self.meshes.deinit(alloc);
    self.lods.deinit(alloc);
    self.free.deinit(alloc);

    self.current_meshes.deinit(.from(ctx));
    self.current_lods.deinit(.from(ctx));

    ctx.view.transport.unqueue(self.staging_tickets[0], false);
    ctx.view.transport.unqueue(self.staging_tickets[1], false);

    self.staging_meshes.deinit(.from(ctx));
    self.staging_lods.deinit(.from(ctx));
}

pub fn deinitNow(self: *MeshRegistry, ctx: base.Ctx.Query(&.{ .vma_allocator }), alloc: Allocator) void {
    self.meshes.deinit(alloc);
    self.lods.deinit(alloc);
    self.free.deinit(alloc);

    self.current_meshes.deinitNow(.from(ctx));
    self.current_lods.deinitNow(.from(ctx));

    self.staging_meshes.deinitNow(.from(ctx));
    self.staging_lods.deinitNow(.from(ctx));
}

pub fn new(self: *MeshRegistry, alloc: Allocator) !u32 {
    if (self.free.pop()) |free| {
        self.meshes.set(free, .{});
        return free;
    }

    try self.meshes.append(alloc, .{});
    return @intCast(self.meshes.len - 1);
}

pub fn acquire(self: *MeshRegistry, ctx: base.Ctx.Query(&.{ .transport }), alloc: Allocator, io: std.Io, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, geometry_registry: *GeometryRegistry, material_registry: *MaterialRegistry, patcher: types.Patcher(.mesh), id: u32, delta: u32) !void {
    std.debug.assert(delta > 0);

    const mesh_slice = self.meshes.slice();
    const ref_count = &mesh_slice.items(.ref_count)[id];
    if (ref_count.* == 0) {
        const data = try loader.load(io, cold);
        defer loader.unload(io, cold);

        var reader = std.Io.Reader.fixed(data);
        var json_reader = std.json.Reader.init(alloc, &reader);
        defer json_reader.deinit();

        const mblob_parsed = try std.json.parseFromTokenSource(MeshDescBlob, alloc, &json_reader, .{});
        defer mblob_parsed.deinit();
        const mblob = mblob_parsed.value;

        const lod_offset = try self.allocLodSlots(alloc, @intCast(mblob.lods.len));
        errdefer self.freeLodSlots(lod_offset, @intCast(mblob.lods.len));

        mesh_slice.items(.desc)[id] = Mesh.GPUMeshDesc{
            .lod_offset = lod_offset,
            .lod_count = @intCast(mblob.lods.len),
            .aabb_min_x = mblob.aabb_min[0],
            .aabb_min_y = mblob.aabb_min[1],
            .aabb_min_z = mblob.aabb_min[2],
            .aabb_max_x = mblob.aabb_max[0],
            .aabb_max_y = mblob.aabb_max[1],
            .aabb_max_z = mblob.aabb_max[2],
        };

        const lods_slice = self.lods.slice();
        const entries = lods_slice.items(.entries)[lod_offset..][0..mblob.lods.len];
        const patch_lookups = lods_slice.items(.patch_lookup)[lod_offset..][0..mblob.lods.len];
        for (entries, patch_lookups, mblob.lods) |*entry, *patch_lookup, lblob| {
            const geo_kind, const geo_dense = resolver.lookup(resolved, lblob.geometry) orelse return error.InvalidGeometryGid;
            if (geo_kind != .geometry) return error.InvalidGeometryKind;

            const schema_kind, const schema_dense = resolver.lookup(resolved, lblob.material_schema) orelse return error.InvalidMaterialSchemaGid;
            if (schema_kind != .material_schema) return error.InvalidMaterialSchemaKind;

            const instance_kind, const instance_dense = resolver.lookup(resolved, lblob.material_instance) orelse return error.InvalidMaterialinstanceGid;
            if (instance_kind != .material_instance) return error.InvalidMaterialinstanceKind;

            entry.* = .{
                .buffer_address = if (geo_dense == std.math.maxInt(u32)) 0 else if (try geometry_registry.isReady(.from(ctx), geo_dense)) geometry_registry.getAddress(geo_dense) else blk: {
                    try patcher.addGeometry(.{ lblob.geometry, geo_dense });
                    break :blk 0;
                },
                .vertex_count = lblob.vertex_count,
                .draw_count = lblob.draw_count,
                .material_schema = schema_dense,
                .material_instance = material_registry.getInstanceDense(instance_dense),
                .error_metric = lblob.error_metric,
            };
            patch_lookup.* = .init(.{
                .geometry = lblob.geometry,
                .material_schema = lblob.material_schema,
                .material_instance = lblob.material_instance,
            });
        }

        self.stale = true;
    }

    ref_count.* += delta;
}

pub fn release(self: *MeshRegistry, alloc: Allocator, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);

    const slice = self.meshes.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* >= delta);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

    const desc = &slice.items(.desc)[id];
    self.freeLodSlots(desc.lod_offset, desc.lod_count);
    desc.lod_count = 0;
    desc.lod_offset = 0;

    try self.free.append(alloc, id);
    self.stale = true;

    return .released;
}

pub fn ingest(alloc: Allocator, io: std.Io, location: types.IngestLocation, mesh: MeshDescBlob) types.IngestError!types.Entry {
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

    try stringify.write(mesh);
    try file_writer.flush();

    var entry: types.Entry = .{
        .generation = 0,
        .kind = .mesh,
        .cold_asset = .{
            .location = location.loc,
            .offset = 0,
            .size = file_writer.logicalPos(),
        },
    };

    for (mesh.lods, 0..) |lod, i| {
        entry.deps.necessary.unset(3*i);
        entry.deps.necessary.unset(3*i + 1);
        entry.deps.necessary.unset(3*i + 2);

        (try entry.deps.gids.addOne(alloc)).* = lod.geometry;
        (try entry.deps.gids.addOne(alloc)).* = lod.material_schema;
        (try entry.deps.gids.addOne(alloc)).* = lod.material_instance;
    }

    return entry;
}

pub fn patch(self: *MeshRegistry, target: u32, resolved: struct {Gid, u32}, geometry_reg: *GeometryRegistry, material_reg: *MaterialRegistry) !void {
    const desc = self.meshes.items(.desc)[target];

    const lods_slice = self.lods.slice();
    const patch_lookups = lods_slice.items(.patch_lookup)[desc.lod_offset..][0..desc.lod_count];
    var found = false;
    for (0.., patch_lookups) |lod_ix, *lookup| {
        var it = lookup.iterator();
        while (it.next()) |e| {
            if (e.value.* != resolved.@"0") continue;

            const lod_entry = &lods_slice.items(.entries)[desc.lod_offset + lod_ix];
            switch (e.key) {
                .geometry => lod_entry.buffer_address = if (resolved.@"1" == std.math.maxInt(u32)) 0 else geometry_reg.getAddress(resolved.@"1"),
                .material_instance => lod_entry.material_instance = material_reg.getInstanceDense(resolved.@"1"),
                .material_schema => lod_entry.material_schema = resolved.@"1",
            }
            found = true;
        }
    }
    std.debug.assert(found);

    self.stale = true;
}

pub fn tick(self: *MeshRegistry, ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .graphics_family, .transport_family, .transport, .destroy_queue })) !void {
    const transport: *base.Transport = ctx.view.transport;

    if (try transport.isReady(self.staging_tickets[0]) and try transport.isReady(self.staging_tickets[1])) {
        self.staging_tickets = .{ .none, .none };

        std.mem.swap(base.Buffer, &self.staging_meshes, &self.current_meshes);
        std.mem.swap(base.Buffer, &self.staging_lods, &self.current_lods);

        self.upload_generation += 1;
    }
    if (!self.stale) return;

    transport.unqueue(self.staging_tickets[0], false);
    transport.unqueue(self.staging_tickets[1], false);

    const descs = std.mem.sliceAsBytes(self.meshes.items(.desc));
    var meshes_first_use = false;
    if (self.staging_meshes.size < descs.len) {
        self.staging_meshes.deinit(.from(ctx));
        self.staging_meshes = try .init(.from(ctx), .graphics, "MeshRegistry: Meshes", descs.len, .{ .transfer_dst_bit = true, .storage_buffer_bit = true }, .gpu_only);
        meshes_first_use = true;
    }

    const lods = std.mem.sliceAsBytes(self.lods.items(.entries));
    var lods_first_use = false;
    if (self.staging_lods.size < lods.len) {
        self.staging_lods.deinit(.from(ctx));
        self.staging_lods = try .init(.from(ctx), .graphics, "MeshRegistry: Lods", lods.len, .{ .transfer_dst_bit = true, .storage_buffer_bit = true }, .gpu_only);
        lods_first_use = true;
    }

    errdefer self.staging_tickets = .{ .none, .none };
    self.staging_tickets[0] = try transport.uploadBuffer(false, descs, null, null, self.staging_meshes.handle, 0, .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true }, .{ .memory_read_bit = true, }, meshes_first_use);
    errdefer transport.unqueue(self.staging_tickets[0], false);

    self.staging_tickets[1] = try transport.uploadBuffer(false, lods, null, null, self.staging_lods.handle, 0, .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true }, .{ .memory_read_bit = true, }, lods_first_use);
    errdefer transport.unqueue(self.staging_tickets[1], false);

    self.stale = false;
}

// TODO: hole tracking + filling
fn allocLodSlots(self: *MeshRegistry, alloc: Allocator, lod_count: u32) !u32 {
    const ret = self.lods.len;

    try self.lods.ensureUnusedCapacity(alloc, lod_count);
    for (0..lod_count) |_| _ = self.lods.addOneAssumeCapacity();

    return @intCast(ret);
}

fn freeLodSlots(self: *MeshRegistry, lod_offset: u32, lod_count: u32) void {
    if (lod_offset + lod_count != self.lods.len) return;

    self.lods.shrinkRetainingCapacity(lod_offset);
}

test {
    std.testing.refAllDecls(@This());
}
