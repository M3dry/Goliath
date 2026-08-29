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
};

const InstanceBlob = struct {
    schema: Gid,
    blob: []u8,
};

pub const IngestSchema = struct {
    blob_size: usize,
};

pub const IngestInstance = struct {
    schema: Gid,
    blob: []u8,
};

pub fn init() MaterialRegistry {
    return .{};
}

pub fn deinit(self: *MaterialRegistry, ctx: base.Ctx.Query(&.{ .destroy_queue, .transport }), alloc: Allocator) void {
    const schemas_slice = self.schemas.slice();
    for (schemas_slice.items(.instance_blobs), schemas_slice.items(.free_instances), schemas_slice.items(.buffer), schemas_slice.items(.staging_buffer), schemas_slice.items(.staging_ticket)) |*blobs, *free, *buffer, *staging_buffer, staging_ticket| {
        ctx.view.transport.unqueue(staging_ticket, false);

        blobs.deinit(alloc);
        free.deinit(alloc);
        buffer.deinit(.from(ctx));
        staging_buffer.deinit(.from(ctx));
    }
    self.schemas.deinit(alloc);
    self.free_schemas.deinit(alloc);

    for (self.instances.items(.blob)) |blob| {
        alloc.free(blob);
    }
    self.instances.deinit(alloc);
    self.free_instances.deinit(alloc);
}

pub fn deinitNow(self: *MaterialRegistry, ctx: base.Ctx.Query(&.{ .vma_allocator }), alloc: Allocator) void {
    const schemas_slice = self.schemas.slice();
    for (schemas_slice.items(.instance_blobs), schemas_slice.items(.free_instances), schemas_slice.items(.buffer), schemas_slice.items(.staging_buffer)) |*blobs, *free, *buffer, *staging_buffer| {
        blobs.deinit(alloc);
        free.deinit(alloc);
        buffer.deinitNow(.from(ctx));
        staging_buffer.deinitNow(.from(ctx));
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
        self.schemas.set(free, .{});
        return free;
    }

    try self.schemas.append(alloc, .{});
    return @intCast(self.schemas.len - 1);
}

pub fn acquireSchema(self: *MaterialRegistry, alloc: Allocator, io: std.Io, loader: *Loader, cold: *const Loader.ColdAsset, id: u32, delta: u32) !types.AcquireReturn {
    std.debug.assert(delta > 0);

    const slice = self.schemas.slice();
    const ref_count = &slice.items(.ref_count)[id];
    var ret: types.AcquireReturn = .incr;
    if (ref_count.* == 0) {
        const data = try loader.load(io, cold);
        defer loader.unload(io, cold);

        var reader = std.Io.Reader.fixed(data);
        var json_reader = std.json.Reader.init(alloc, &reader);
        defer json_reader.deinit();

        const blob_parsed = try std.json.parseFromTokenSource(SchemaBlob, alloc, &json_reader, .{});
        defer blob_parsed.deinit();
        const blob = blob_parsed.value;

        slice.items(.blob_size)[id] = blob.blob_size;
        ret = .load;
    }

    ref_count.* += delta;
    return ret;
}

pub fn acquireInstance(self: *MaterialRegistry, alloc: Allocator, io: std.Io, loader: *Loader, resolved: resolver.Resolved, cold: *const Loader.ColdAsset, id: u32, delta: u32) !?u64 {
    std.debug.assert(delta > 0);

    const slice = self.instances.slice();
    const ref_count = &slice.items(.ref_count)[id];
    const upload_gen = if (ref_count.* == 0) outer: {
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
        std.debug.assert(schema_slice.items(.blob_size)[schema_dense] == instance.blob.len);

        const instance_blobs: *std.ArrayList(u8) = &schema_slice.items(.instance_blobs)[schema_dense];
        const instance_dense: u32, const blob = if (schema_slice.items(.free_instances)[schema_dense].pop()) |free| blk: {
            break :blk .{free, instance_blobs.items[instance.blob.len*free..][0..instance.blob.len]};
        } else blk: {
            const instance_dense = (instance_blobs.items.len / instance.blob.len);
            const b = try instance_blobs.addManyAsSlice(alloc, instance.blob.len);
            break :blk .{@intCast(instance_dense), b};
        };

        @memcpy(blob, instance.blob);
        schema_slice.items(.stale)[schema_dense] = true;

        slice.items(.blob)[id] = instance.blob;
        slice.items(.schema)[id] = schema_dense;
        slice.items(.instance_dense)[id] = instance_dense;

        break :outer schema_slice.items(.upload_generation)[schema_dense];
    } else null;

    ref_count.* += delta;

    return upload_gen;
}

pub fn releaseSchema(self: *MaterialRegistry, ctx: base.Ctx.Query(&.{ .destroy_queue }), alloc: Allocator, id: u32, delta: u32) !types.ReleaseReturn {
    std.debug.assert(delta > 0);

    const slice = self.schemas.slice();
    const ref_count = &slice.items(.ref_count)[id];

    std.debug.assert(ref_count.* >= delta);
    ref_count.* -= delta;

    if (ref_count.* != 0) return .kept;

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

    try stringify.write(SchemaBlob{
        .blob_size = schema.blob_size,
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
        .kind = .material_schema,
        .cold_asset = .{
            .location = location.loc,
            .offset = 0,
            .size = file_writer.logicalPos(),
        },
    };
    entry.deps.necessary.set(alloc, 0);
    (try entry.deps.gids.addOne(alloc)).* = instance.schema;

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

pub inline fn getInstanceDense(self: *MaterialRegistry, instance_id: u32) u32 {
    const slice = self.instances.slice();
    std.debug.assert(slice.len > instance_id);

    const instance_dense = slice.items(.instance_dense)[instance_id];
    std.debug.assert(blk: {
        const schema_slice = self.schemas.slice();
        break :blk schema_slice.len > instance_dense;
    });

    return instance_dense;
}

pub inline fn getUploadGeneration(self: *MaterialRegistry, schema_id: u32) u64 {
    return self.schemas.items(.upload_generation)[schema_id];
}

pub inline fn getInstanceSchema(self: *MaterialRegistry, instance_id: u32) u32 {
    return self.instances.items(.schema)[instance_id];
}

test {
    std.testing.refAllDecls(@This());
}
