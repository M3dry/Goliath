const std = @import("std");
const base = @import("base");

const Mesh = @import("Mesh.zig");
const Allocator = std.mem.Allocator;

const MeshHandler = @This();

mesh_desc_buf: base.Buffer = .empty,
lod_entry_buf: base.Buffer = .empty,
ticket: base.Transport.Ticket = .none,
mesh_descs: std.ArrayListUnmanaged(Mesh.GPUMeshDesc) = .empty,
lod_entries: std.ArrayListUnmanaged(Mesh.GPULODEntry) = .empty,

pub const empty: MeshHandler = .{};

pub fn deinit(self: *MeshHandler, alloc: Allocator, ctx: base.Ctx.Query(&.{ .destroy_queue, .transport })) void {
    ctx.view.transport.unqueue(self.ticket, false);
    self.mesh_desc_buf.deinit(.from(ctx));
    self.lod_entry_buf.deinit(.from(ctx));

    self.mesh_descs.deinit(alloc);
    self.lod_entries.deinit(alloc);
}

/// Returns the mesh_desc_ix that goes into InstanceData.mesh_desc_ix.
/// Uploads all LOD geometry to GPU (via Lod.initGeometryBuffer) if not already on GPU.
/// Appends MeshDesc + LODEntry to the CPU staging arrays. Caller can batch
/// multiple registerMesh calls, then call flushDescriptorArrays() once.
pub fn registerMesh(self: *MeshHandler, mesh: *Mesh, alloc: Allocator, ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .graphics_family, .transport_family, .transport, .destroy_queue })) !void {
    if (mesh.gpu_meta != null) return error.MeshOnGPU;

    var mesh_desc = Mesh.GPUMeshDesc.fromMesh(mesh);
    mesh_desc.lod_offset = @intCast(self.lod_entries.items.len);
    try self.mesh_descs.append(alloc, mesh_desc);

    try self.lod_entries.ensureTotalCapacity(alloc, self.lod_entries.items.len + mesh.lods.len);

    mesh.gpu_meta = .{
        .lod_offset = mesh_desc.lod_offset,
        .mesh_desc_ix = @intCast(self.mesh_descs.items.len - 1),
    };

    var i: usize = 0;
    errdefer for (0..i) |j| mesh.lods[j].deinitGeometryBuffer(.from(ctx));
    for (mesh.lods) |*lod| {
        var gpu_lod = Mesh.GPULODEntry.fromLod(lod);
        if (gpu_lod.buffer_address == 0) {
            const buf, _ = try lod.initGeometryBuffer(.from(ctx));
            gpu_lod.buffer_address = buf.address;
        }
        lod.on_gpu = true;

        self.lod_entries.appendAssumeCapacity(gpu_lod);
        i += 1;
    }
}

/// Flushes the staging copies of MeshDesc[] and LODEntry[] to the GPU buffers.
/// Grows the GPU buffers if needed (old buffer queued for destruction).
/// The transport already batches internally, so calling this once per frame
/// (or once per loading batch) is fine.
pub fn flushDescriptorArrays(self: *MeshHandler, ctx: base.Ctx.Query(&.{ .device, .vma_allocator, .graphics_family, .transport_family, .transport, .destroy_queue })) !void {
    ctx.view.transport.unqueue(self.ticket, false);
    self.mesh_desc_buf.deinit(.from(ctx));
    self.lod_entry_buf.deinit(.from(ctx));

    self.mesh_desc_buf = try .init(.from(ctx), .graphics, "Mesh Descriptions", self.mesh_descs.items.len * @sizeOf(Mesh.GPUMeshDesc), .{.transfer_dst_bit = true, .storage_buffer_bit = true}, .gpu_only);
    self.lod_entry_buf = try .init(.from(ctx), .graphics, "LOD Entries", self.lod_entries.items.len * @sizeOf(Mesh.GPULODEntry), .{.transfer_dst_bit = true, .storage_buffer_bit = true}, .gpu_only);

    const dst_stage: base.vk.PipelineStageFlags2 = .{ .compute_shader_bit = true, .vertex_shader_bit = true, .fragment_shader_bit = true };
    const dst_access: base.vk.AccessFlags2 = .{ .memory_read_bit = true, };
    const tick1 = try ctx.view.transport.uploadBuffer(true, std.mem.sliceAsBytes(self.mesh_descs.items), null, null, self.mesh_desc_buf.handle, 0, dst_stage, dst_access, true);
        errdefer ctx.view.transport.unqueue(tick1, false);
    const tick2 = try ctx.view.transport.uploadBuffer(true, std.mem.sliceAsBytes(self.lod_entries.items), null, null, self.lod_entry_buf.handle, 0, dst_stage, dst_access, true);
        errdefer ctx.view.transport.unqueue(tick2, false);

    self.ticket = tick2;
}

/// Frees GPU resources for a registered mesh. The caller must ensure the
/// mesh is no longer referenced in any GPU descriptor or instance data.
pub fn unregisterMesh(self: *MeshHandler, mesh: *Mesh, ctx: base.Ctx.Query(&.{ .destroy_queue, .transport })) void {
    if (mesh.gpu_meta) |gpu_meta| {
        if (gpu_meta.lod_offset + mesh.lods.len == self.lod_entries.items.len) {
            self.lod_entries.shrinkRetainingCapacity(gpu_meta.lod_offset);
        }
        if (gpu_meta.mesh_desc_ix + 1 == self.mesh_descs.items.len) {
            self.mesh_descs.shrinkRetainingCapacity(gpu_meta.mesh_desc_ix);
        }
    }

    for (mesh.lods) |*lod| {
        lod.deinitGeometryBuffer(.from(ctx));
        lod.on_gpu = false;
    }
}