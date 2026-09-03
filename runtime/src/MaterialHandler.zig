const std = @import("std");
const base = @import("base");
const Visbuffer = @import("Visbuffer.zig");
const PbrShading = @import("PbrShading.zig");

const Self = @This();

/// Temporary stand-in for the asset loader: per-schema CPU arrays + GPU instance
/// buffers, MeshHandler-style. `append`/`flush` uploads; the loader will replace this.
schema_items: [Visbuffer.max_schemas]std.ArrayListUnmanaged(PbrShading.GPUPBRInstance) = .{std.ArrayListUnmanaged(PbrShading.GltfPBRInstance).empty} ** Visbuffer.max_schemas,
schema_bufs: [Visbuffer.max_schemas]base.Buffer = .{base.Buffer.empty} ** Visbuffer.max_schemas,
schema_tickets: [Visbuffer.max_schemas]base.Transport.Ticket = .{base.Transport.Ticket.none} ** Visbuffer.max_schemas,

pub fn append(self: *Self, alloc: std.mem.Allocator, schema: usize, instance: PbrShading.GPUPBRInstance) !void {
    try self.schema_items[schema].append(alloc, instance);
}

pub fn flush(self: *Self, ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .graphics_family, .transport_family, .transport, .destroy_queue })) !void {
    const dst_stage: base.vk.PipelineStageFlags2 = .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true };
    const dst_access: base.vk.AccessFlags2 = .{ .shader_storage_read_bit = true };

    for (0..Visbuffer.max_schemas) |schema| {
        ctx.view.transport.unqueue(self.schema_tickets[schema], false);
        self.schema_bufs[schema].deinit(.from(ctx));

        if (self.schema_items[schema].items.len == 0) {
            self.schema_bufs[schema] = .empty;
            self.schema_tickets[schema] = .none;
            continue;
        }

        self.schema_bufs[schema] = try base.Buffer.init(.from(ctx), .graphics, "Material instances", @as(u64, self.schema_items[schema].items.len) * @sizeOf(PbrShading.GPUPBRInstance), .{ .storage_buffer_bit = true, .transfer_dst_bit = true }, .gpu_only);
        self.schema_tickets[schema] = try ctx.view.transport.uploadBuffer(true, std.mem.sliceAsBytes(self.schema_items[schema].items), null, null, self.schema_bufs[schema].handle, 0, dst_stage, dst_access, true);
    }
}

pub fn deinit(self: *Self, alloc: std.mem.Allocator, ctx: base.Ctx.Query(&.{ .destroy_queue, .transport })) void {
    for (0..Visbuffer.max_schemas) |schema| {
        ctx.view.transport.unqueue(self.schema_tickets[schema], false);
        self.schema_bufs[schema].deinit(.from(ctx));
        self.schema_items[schema].deinit(alloc);
    }
}
