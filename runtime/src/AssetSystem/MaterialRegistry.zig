const std = @import("std");
const base = @import("base");
const Mesh = @import("../Mesh.zig");
const types = @import("Types.zig");
const resolver = @import("resolver.zig");
const Loader = @import("Loader.zig");
const Gid = types.Gid;

const Allocator = std.mem.Allocator;
const MaterialRegistry = @This();

schemas: std.MultiArrayList(struct {
    blob_size: usize = 0,
    /// assume to be sorted
    gid_texture_offsets: []usize = &.{},
    ref_count: u32 = 0,

    instance_blobs: std.ArrayList(u8) = .empty,
    free_instances: std.ArrayList(u32) = .empty,

    upload_generation: u64 = 0,
    buffer: base.Buffer = .empty,
    staging_ticket: base.Transport.Ticket = .none,
    staging_buffer: base.Buffer = .empty,
    stale: bool = false,
}) = .empty,
free_schemas: std.ArrayList(u32) = .empty,

instances: std.MultiArrayList(struct {
    ref_count: u32 = 0,
    blob: []u8 = &.{},
    schema: u32 = std.math.maxInt(u32),
    instance_dense: u32 = std.math.maxInt(u32),
}) = .empty,
free_instances: std.ArrayList(u32) = .empty,

const SchemaBlob = struct {
    blob_size: usize,
    gid_texture_offsets: []usize,
};

const InstanceBlob = struct {
    schema: Gid,
    blob: []u8,
};

pub const IngestSchema = struct {
    blob_size: usize,
    /// assumes that offsets are sorted in ascending order
    texture_offsets: []const usize,
};

pub const IngestInstance = struct {
    schema: Gid,
    blob: []u8,
    /// the `schema` can not be loaded in which case we couldn't retrieve the texture gids, thus this
    deps: []Dep,

    pub const Dep = struct {
        gid: Gid,
        necessary: bool
    };
};

pub fn init() MaterialRegistry {
    return .{};
}

pub fn deinit(self: *MaterialRegistry, ctx: base.Ctx.Query(&.{ .destroy_queue, .transport }), alloc: Allocator) void {
    const schemas_slice = self.schemas.slice();
    for (schemas_slice.items(.instance_blobs), schemas_slice.items(.free_instances), schemas_slice.items(.buffer), schemas_slice.items(.staging_buffer), schemas_slice.items(.staging_ticket), schemas_slice.items(.gid_texture_offsets)) |*blobs, *free, *buffer, *staging_buffer, staging_ticket, gid_texture_offsets| {
        ctx.view.transport.unqueue(staging_ticket, false);

        blobs.deinit(alloc);
        free.deinit(alloc);
        buffer.deinit(.from(ctx));
        staging_buffer.deinit(.from(ctx));
        alloc.free(gid_texture_offsets);
    }
    self.schemas.deinit(alloc);
    self.free_schemas.deinit(alloc);

    for (self.instances.items(.blob)) |blob| {
        alloc.free(blob);
    }
    self.instances.deinit(alloc);
    self.free_instances.deinit(alloc);
}

pub fn deinitNow(self: *MaterialRegistry, ctx: base.Ctx.Query(&.{ .vma_allocator, .transport }), alloc: Allocator) void {
    const schemas_slice = self.schemas.slice();
    for (schemas_slice.items(.instance_blobs), schemas_slice.items(.free_instances), schemas_slice.items(.buffer), schemas_slice.items(.staging_buffer), schemas_slice.items(.staging_ticket), schemas_slice.items(.gid_texture_offsets)) |*blobs, *free, *buffer, *staging_buffer, staging_ticket, gid_texture_offsets| {
        ctx.view.transport.unqueue(staging_ticket, false);

        blobs.deinit(alloc);
        free.deinit(alloc);
        buffer.deinitNow(.from(ctx));
        staging_buffer.deinitNow(.from(ctx));
        alloc.free(gid_texture_offsets);
    }
    self.schemas.deinit(alloc);
    self.free_schemas.deinit(alloc);

    for (self.instances.items(.blob)) |blob| {
        alloc.free(blob);
    }
    self.instances.deinit(alloc);
    self.free_instances.deinit(alloc);
}

pub fn newSchema(self: *MaterialRegistry, alloc: Allocator) !u32 {
    if (self.free_schemas.pop()) |free| {
        self.schemas.set(free, .{});
        return free;
    }

    try self.schemas.append(alloc, .{});
    return @intCast(self.schemas.len - 1);
}

pub fn newInstance(self: *MaterialRegistry, alloc: Allocator) !u32 {
    if (self.free_instances.pop()) |free| {
        self.instances.set(free, .{});
        return free;
    }

    try self.instances.append(alloc, .{});
    return @intCast(self.instances.len - 1);
}

pub fn acquireSchema(self: *MaterialRegistry, alloc: Allocator, io: std.Io, loader: *Loader, cold: *const Loader.ColdAsset, id: u32, delta: u32) !void {
    std.debug.assert(delta > 0);

    const slice = self.schemas.slice();
    const ref_count = &slice.items(.ref_count)[id];
    if (ref_count.* == 0) {
        const data = try loader.load(io, cold);
        defer loader.unload(io, cold);

        var reader = std.Io.Reader.fixed(data);
        var json_reader = std.json.Reader.init(alloc, &reader);
        defer json_reader.deinit();

        const blob = try std.json.parseFromTokenSourceLeaky(SchemaBlob, alloc, &json_reader, .{});
        errdefer alloc.free(blob.gid_texture_offsets);

        slice.items(.blob_size)[id] = blob.blob_size;
        slice.items(.gid_texture_offsets)[id] = blob.gid_texture_offsets;
    }

    ref_count.* += delta;
}

pub fn acquireInstance(self: *MaterialRegistry, alloc: Allocator, io: std.Io, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, id: u32, delta: u32) !?struct {u64, Gid} {
    std.debug.assert(delta > 0);

    const slice = self.instances.slice();
    const ref_count = &slice.items(.ref_count)[id];
    const ret = if (ref_count.* == 0) outer: {
        const data = try loader.load(io, cold);
        defer loader.unload(io, cold);

        var reader = std.Io.Reader.fixed(data);
        var json_reader = std.json.Reader.init(alloc, &reader);
        defer json_reader.deinit();

        const instance = try std.json.parseFromTokenSourceLeaky(InstanceBlob, alloc, &json_reader, .{});
        errdefer alloc.free(instance.blob);

        const schema_kind, const schema_dense = resolver.lookup(resolved, instance.schema) orelse return error.InvalidSchemaGid;
        if (schema_kind != .material_schema) return error.InvalidSchemaKind;

        const schema_slice = self.schemas.slice();
        std.debug.assert(schema_slice.len > schema_dense);

        const dense_blob_size: usize = schema_slice.items(.blob_size)[schema_dense];
        const instance_blobs: *std.ArrayList(u8) = &schema_slice.items(.instance_blobs)[schema_dense];
        const instance_dense: u32, const blob = if (schema_slice.items(.free_instances)[schema_dense].pop()) |free| blk: {
            break :blk .{free, instance_blobs.items[dense_blob_size*free..][0..dense_blob_size]};
        } else blk: {
            const instance_dense = (instance_blobs.items.len / dense_blob_size);
            const b = try instance_blobs.addManyAsSlice(alloc, dense_blob_size);
            break :blk .{@intCast(instance_dense), b};
        };

        const gid_texture_offsets = schema_slice.items(.gid_texture_offsets)[schema_dense];
        var dense_cursor: usize = 0;
        var cursor: usize = 0;
        var i: usize = 0;
        while (i <= gid_texture_offsets.len) {
            const off: usize = if (i == 0) 0 else gid_texture_offsets[i - 1];
            const next_off = if (i == gid_texture_offsets.len) instance.blob.len else gid_texture_offsets[i];
            const off_delta = next_off - if (i == 0) 0 else (off + @sizeOf(Gid));

            @memcpy(blob[dense_cursor..][0..off_delta], instance.blob[cursor..][0..off_delta]);
            cursor += off_delta;
            dense_cursor += off_delta;

            if (i != gid_texture_offsets.len) {
                var gid: Gid = undefined;
                @memcpy(std.mem.asBytes(&gid), instance.blob[cursor..][0..@sizeOf(Gid)]);
                cursor += @sizeOf(Gid);

                const kind, const dense = resolver.lookup(resolved, gid) orelse return error.InvalidTextureGid;
                if (kind != .sampled_texture) return error.InvalidTextureKind;
                @memcpy(blob[dense_cursor..][0..@sizeOf(u32)], std.mem.asBytes(&dense));
                dense_cursor += @sizeOf(u32);
            }

            i += 1;
        }
        schema_slice.items(.stale)[schema_dense] = true;

        slice.items(.blob)[id] = instance.blob;
        slice.items(.schema)[id] = schema_dense;
        slice.items(.instance_dense)[id] = instance_dense;

        break :outer .{schema_slice.items(.upload_generation)[schema_dense], instance.schema};
    } else null;

    ref_count.* += delta;

    return ret;
}

pub fn releaseSchema(self: *MaterialRegistry, ctx: base.Ctx.Query(&.{ .destroy_queue }), alloc: Allocator, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);

    const slice = self.schemas.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* >= delta);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

    alloc.free(slice.items(.gid_texture_offsets)[id]);
    slice.items(.instance_blobs)[id].deinit(alloc);
    slice.items(.buffer)[id].deinit(.from(ctx));
    slice.items(.staging_buffer)[id].deinit(.from(ctx));

    try self.free_schemas.append(alloc, id);

    return .released;
}

pub fn releaseInstance(self: *MaterialRegistry, alloc: Allocator, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);

    const slice = self.instances.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* >= delta);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

    alloc.free(slice.items(.blob)[id]);

    // TODO: free up the instance in its schema

    try self.free_instances.append(alloc, id);

    return .released;
}

pub fn ingestSchema(io: std.Io, location: types.IngestLocation, schema: IngestSchema) types.IngestError!types.Entry {
    const file = try location.prefix_dir.createFile(io, location.path, .{});
    defer file.close(io);

    var writer_buffer: [512]u8 = undefined;
    var file_writer = file.writer(io, &writer_buffer);
    var stringify: std.json.Stringify = .{
        .writer = &file_writer.interface,
        .options = .{
            .whitespace = .indent_2,
        },
    };

    std.debug.assert(std.sort.isSorted(usize, schema.texture_offsets, {}, std.sort.asc(usize)));
    try stringify.write(SchemaBlob{
        .blob_size = schema.blob_size,
        .gid_texture_offsets = @constCast(schema.texture_offsets), // json write won't modify the array, and adding const to `gid_texture_offsets` introduces issues in acquire
    });
    try file_writer.flush();

    return .{
        .generation = 0,
        .kind = .material_schema,
        .cold_asset = .{
            .location = location.loc,
            .offset = 0,
            .size = file_writer.logicalPos(),
        },
    };
}

// TODO: store the instance blob in a more space efficient manner
pub fn ingestInstance(alloc: Allocator, io: std.Io, location: types.IngestLocation, instance: IngestInstance) types.IngestError!types.Entry {
    const file = try location.prefix_dir.createFile(io, location.path, .{});
    defer file.close(io);

    var writer_buffer: [512]u8 = undefined;
    var file_writer = file.writer(io, &writer_buffer);
    var stringify: std.json.Stringify = .{
        .writer = &file_writer.interface,
        .options = .{
            .whitespace = .indent_2,
        },
    };

    try stringify.write(InstanceBlob{
        .schema = instance.schema,
        .blob = instance.blob,
    });
    try file_writer.flush();

    var entry: types.Entry = .{
        .generation = 0,
        .kind = .material_instance,
        .cold_asset = .{
            .location = location.loc,
            .offset = 0,
            .size = file_writer.logicalPos(),
        },
    };
    try entry.deps.necessary.ensureCapacity(alloc, instance.deps.len);
    try entry.deps.gids.ensureCapacity(alloc, instance.deps.len);

    entry.deps.necessary.setAssumeCapacity(0);
    entry.deps.gids.addOneAssumeCapacity().* = instance.schema;

    for (1.., instance.deps) |i, dep| {
        if (dep.necessary) {
            entry.deps.necessary.setAssumeCapacity(i);
        } else {
            entry.deps.necessary.unset(i);
        }

        entry.deps.gids.addOneAssumeCapacity().* = dep.gid;
    }

    return entry;
}

pub fn tick(self: *MaterialRegistry, ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .graphics_family, .transport_family, .transport, .destroy_queue })) !void {
    const transport: *base.Transport = ctx.view.transport;

    const slice = self.schemas.slice();
    for (slice.items(.stale), 0..) |*stale, schema_id| {
        const ticket = &slice.items(.staging_ticket)[schema_id];
        const buffer = &slice.items(.buffer)[schema_id];
        const staging_buffer = &slice.items(.staging_buffer)[schema_id];
        if (try transport.isReady(ticket.*)) {
            ticket.* = .none;

            std.mem.swap(base.Buffer, buffer, staging_buffer);

            slice.items(.upload_generation)[schema_id] += 1;
        }

        if (!stale.*) continue;

        transport.unqueue(ticket.*, false);

        const blobs = slice.items(.instance_blobs)[schema_id].items;
        var first_use = false;
        if (staging_buffer.size < blobs.len) {
            staging_buffer.deinit(.from(ctx));
            staging_buffer.* = try .init(.from(ctx), .graphics, "MaterialRegistry buffer", blobs.len, .{ .transfer_dst_bit = true, .storage_buffer_bit = true }, .gpu_only);
            first_use = true;
        }

        errdefer ticket.* = .none;
        ticket.* = try transport.uploadBuffer(false, blobs, null, null, staging_buffer.handle, 0, .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true }, .{ .memory_read_bit = true }, first_use);
        errdefer transport.unqueue(ticket.*, false);

        stale.* = false;
    }
}

pub inline fn getInstanceDense(self: *MaterialRegistry, instance_id: u32) ?u32 {
    const slice = self.instances.slice();
    std.debug.assert(slice.len > instance_id);

    const instance_dense = slice.items(.instance_dense)[instance_id];
    const schema_slice = self.schemas.slice();
    if (schema_slice.len <= instance_dense) return null;

    return instance_dense;
}

pub inline fn getUploadGeneration(self: *MaterialRegistry, schema_id: u32) u64 {
    return self.schemas.items(.upload_generation)[schema_id];
}

pub inline fn getInstanceSchema(self: *MaterialRegistry, instance_id: u32) u32 {
    return self.instances.items(.schema)[instance_id];
}

pub inline fn getInstancesBuffer(self: *MaterialRegistry, schema_id: u32) base.Buffer {
    return self.schemas.items(.buffer)[schema_id];
}

test {
    std.testing.refAllDecls(@This());
}
